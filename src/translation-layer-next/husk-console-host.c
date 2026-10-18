/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "husk-console-host.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <mach/mach.h>
#if !TARGET_OS_OSX
#include <os/proc.h>
#endif
#endif

#include "husk-jit-arena.h"
#include "husk-tl-gamepad.h"

void tl_log_line(const char *fmt, ...);
/* The Android side's gate (husk-tl-unity-app.c). */
/* weak: the Mac harnesses are built without the app glue that defines it (tools/regress/build.sh) */
const char *husk_native_loaded_apk(void) __attribute__((weak_import));

static const HuskConsoleModuleV1 *g_module;
static HuskHostV1 g_host;

/* The app's keyboard for a game's text dialog (husk_console_set_text_requester); none: the module answers for the player. */
static husk_console_text_requester g_text_requester;
void husk_console_set_text_requester(husk_console_text_requester fn) { g_text_requester = fn; }
static int host_request_text(const char *title, const char *initial, uint32_t max_chars, void (*done)(void *, const char *), void *ctx)
{
    return g_text_requester ? g_text_requester(title, initial, max_chars, done, ctx) : -1;
}
static int g_log_fd = -1;
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;
static struct timespec g_t0;
static char g_game_id[128];
static pthread_t g_pad_thread;

/* ---- the host table ---- */

static void *host_jit_alloc(size_t bytes, void **rx_out) { return husk_jit_arena_alloc(bytes, rx_out); }
static int host_jit_free(void *rw, size_t bytes) { return husk_jit_arena_free(rw, bytes) ? 1 : 0; }

static uint64_t host_available_memory(void)
{
#if defined(__APPLE__) && !TARGET_OS_OSX
    return (uint64_t)os_proc_available_memory();
#else
    /* HUSK_MEM_LIMIT_MB: a phone's figure, to see whether a game fits there */
    const char *limit = getenv("HUSK_MEM_LIMIT_MB");
    return limit ? (uint64_t)atoll(limit) << 20 : (uint64_t)8 << 30;
#endif
}

static uint64_t host_footprint(void)
{
#if defined(__APPLE__)
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) return info.phys_footprint;
#endif
    return 0;
}

/* Address space in use and threads alive: iOS gives large mappings one 64 GB window, and a console game's threads each
 * reserve some of it. */
static void task_usage(uint64_t *virtual_bytes, unsigned *threads)
{
    *virtual_bytes = 0;
    *threads = 0;
#if defined(__APPLE__)
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) *virtual_bytes = info.virtual_size;
    thread_act_array_t list;
    mach_msg_type_number_t n = 0;
    if (task_threads(mach_task_self(), &list, &n) == KERN_SUCCESS) {
        for (mach_msg_type_number_t i = 0; i < n; i++) mach_port_deallocate(mach_task_self(), list[i]);
        vm_deallocate(mach_task_self(), (vm_address_t)list, n * sizeof(*list));
        *threads = n;
    }
#endif
}

/* iOS gives large mappings one window from 0x70_0000_0000. What is in it: bytes in use, the highest end, the largest
 * gap below that end, and the biggest users by VM tag (0 untagged, 240-255 app-specific, the rest Apple's). */
#define CONSOLE_WINDOW_BASE 0x7000000000ull
static void window_usage(char *out, size_t outlen)
{
    out[0] = 0;
#if defined(__APPLE__)
    uint64_t by_tag[256] = {0}, used = 0, top = CONSOLE_WINDOW_BASE, gap = 0, prev_end = CONSOLE_WINDOW_BASE;
    vm_address_t addr = (vm_address_t)CONSOLE_WINDOW_BASE;
    for (;;) {
        vm_size_t size = 0;
        vm_region_extended_info_data_t info;
        mach_msg_type_number_t count = VM_REGION_EXTENDED_INFO_COUNT;
        mach_port_t object = MACH_PORT_NULL;
        if (vm_region_64(mach_task_self(), &addr, &size, VM_REGION_EXTENDED_INFO, (vm_region_info_t)&info, &count, &object) != KERN_SUCCESS) break;
        if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object);
        uint64_t start = addr, end = addr + size;
        if (start > prev_end && start - prev_end > gap) gap = start - prev_end;
        prev_end = end;
        used += size;
        by_tag[info.user_tag & 0xff] += size;
        if (end > top) top = end;
        addr = (vm_address_t)end;
    }
    int n = snprintf(out, outlen, "window used %llu MiB, top 0x%llx, largest gap %llu MiB, tags", (unsigned long long)(used >> 20),
                     (unsigned long long)top, (unsigned long long)(gap >> 20));
    for (int k = 0; k < 5 && n > 0 && (size_t)n < outlen; k++) {
        int best = -1;
        for (int t = 0; t < 256; t++) if (by_tag[t] && (best < 0 || by_tag[t] > by_tag[best])) best = t;
        if (best < 0) break;
        n += snprintf(out + n, outlen - (size_t)n, " %d:%llu", best, (unsigned long long)(by_tag[best] >> 20));
        by_tag[best] = 0;
    }
#else
    (void)outlen;
#endif
}

static void host_log_line(const char *line)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    char buf[2048];
    int n = snprintf(buf, sizeof(buf), "[%8.3f] %s\n", (t.tv_sec - g_t0.tv_sec) + (t.tv_nsec - g_t0.tv_nsec) / 1e9, line);
    if (n < 0) return;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1, buf[n - 1] = '\n';
    pthread_mutex_lock(&g_log_lock);
    if (g_log_fd >= 0) { ssize_t w = write(g_log_fd, buf, (size_t)n); (void)w; }
    else { fwrite(buf, 1, (size_t)n, stderr); }
    pthread_mutex_unlock(&g_log_lock);
    tl_log_line("%s", line);   /* the app's log panel and diagnosis read this one */
}

/* ---- pads ----
 * Husk's controllers (slots 0-2) and the on-screen pad (slot 3) report through tl_pad_update, as for an Android game. Merged
 * here into one PS4 pad: buttons OR'd, the stick and trigger furthest from rest taken. */
static const uint32_t k_orbis_bit[TL_PAD_BUTTONS] = {
    [TL_PAD_A] = 0x4000, [TL_PAD_B] = 0x2000, [TL_PAD_X] = 0x8000, [TL_PAD_Y] = 0x1000,       /* cross circle square triangle */
    [TL_PAD_L1] = 0x400, [TL_PAD_R1] = 0x800, [TL_PAD_THUMBL] = 0x2, [TL_PAD_THUMBR] = 0x4,
    [TL_PAD_START] = 0x8, [TL_PAD_SELECT] = 0x100000, [TL_PAD_MODE] = 0,                       /* options, touchpad click */
    [TL_PAD_DPAD_UP] = 0x10, [TL_PAD_DPAD_DOWN] = 0x40, [TL_PAD_DPAD_LEFT] = 0x80, [TL_PAD_DPAD_RIGHT] = 0x20,
};

static float further(float a, float b) { return (a < 0 ? -a : a) >= (b < 0 ? -b : b) ? a : b; }
static uint8_t stick(float v) { v = v < -1 ? -1 : v > 1 ? 1 : v; return (uint8_t)((v + 1.0f) * 127.5f + 0.5f); }   /* y down, as the PS4 */
static uint8_t trigger(float v) { v = v < 0 ? 0 : v > 1 ? 1 : v; return (uint8_t)(v * 255.0f + 0.5f); }

static void host_log(const char *fmt, ...);

/* Every 5 s while a game runs: memory and address space, so a run that iOS ends without a word still says what was
 * running out. */
static void log_usage(void)
{
    uint64_t virtual_bytes;
    unsigned threads;
    task_usage(&virtual_bytes, &threads);
    host_log("console: footprint %llu MiB, %llu MiB available, %llu MiB address space, %u threads",
             (unsigned long long)(host_footprint() >> 20), (unsigned long long)(host_available_memory() >> 20),
             (unsigned long long)(virtual_bytes >> 20), threads);
    char window[256];
    window_usage(window, sizeof(window));
    if (window[0]) host_log("console: %s", window);
}

static void *pad_loop(void *arg)
{
    (void)arg;
    HuskPadV1 last;
    memset(&last, 0xff, sizeof(last));
    for (unsigned tick = 0;; tick++) {
        if (tick % 625 == 0) log_usage();   /* 8 ms ticks */
        tl_pad_state m = {0}, s;
        uint32_t buttons = 0;
        for (int slot = 0; slot < TL_PADS; slot++) {
            if (!tl_pad_read(slot, &s)) continue;
            buttons |= s.buttons;
            m.lx = further(m.lx, s.lx); m.ly = further(m.ly, s.ly); m.rx = further(m.rx, s.rx); m.ry = further(m.ry, s.ry);
            if (s.lt > m.lt) m.lt = s.lt;
            if (s.rt > m.rt) m.rt = s.rt;
        }
        HuskPadV1 pad;
        memset(&pad, 0, sizeof(pad));
        for (int b = 0; b < TL_PAD_BUTTONS; b++) if (buttons & (1u << b)) pad.buttons |= k_orbis_bit[b];
        pad.left_x = stick(m.lx); pad.left_y = stick(m.ly); pad.right_x = stick(m.rx); pad.right_y = stick(m.ry);
        pad.l2 = trigger(m.lt); pad.r2 = trigger(m.rt);
        if (memcmp(&pad, &last, sizeof(pad)) != 0 && g_module && g_module->state() == HUSK_CONSOLE_RUNNING) {
            g_module->set_pad(&pad);
            last = pad;
        }
        int state = g_module ? g_module->state() : HUSK_CONSOLE_IDLE;
        if (state == HUSK_CONSOLE_STOPPED || state == HUSK_CONSOLE_FAILED) break;
        usleep(8000);
    }
    return NULL;
}

static void host_log(const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    host_log_line(line);
    tl_log_line("%s", line);
}

/* ---- launching ---- */

/* A game that calls exit() ends the app with no fault to report. */
static void log_exit(void)
{
    if (g_module) host_log("console: the process is exiting (exit() with %s loaded)", g_game_id);
}

bool husk_console_launch(const husk_console_launch_args *a, char *err, size_t errlen)
{
    if (g_module) { snprintf(err, errlen, "当前进程中已有主机游戏正在运行"); return false; }
    if (!a || !a->module_path || !a->game || !a->user_dir || !a->metal_layer || a->width_px <= 0 || a->height_px <= 0) {
        snprintf(err, errlen, "缺少启动参数");
        return false;
    }
    /* One game per process: an Android game's engine holds the same JIT region. */
    if (husk_native_loaded_apk && husk_native_loaded_apk()) {
        snprintf(err, errlen, "当前会话已加载其他游戏，请关闭 Husk 后重新打开");
        return false;
    }
    clock_gettime(CLOCK_MONOTONIC, &g_t0);
    if (a->log_path) g_log_fd = open(a->log_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);

    void *lib = dlopen(a->module_path, RTLD_NOW | RTLD_LOCAL);
    if (!lib) { snprintf(err, errlen, "无法加载 %s：%s", a->module_path, dlerror()); return false; }
    husk_console_module_v1_fn entry = (husk_console_module_v1_fn)dlsym(lib, "husk_console_module_v1");
    const HuskConsoleModuleV1 *m = entry ? entry() : NULL;
    if (!m || m->version != HUSK_CONSOLE_ABI_VERSION || m->struct_size < offsetof(HuskConsoleModuleV1, install_package)) {
        snprintf(err, errlen, "%s 不是当前版本 Husk 支持的主机模块", a->module_path);
        return false;
    }

    /* The module's executable memory: Husk's region, capped at what the module declares. */
    if (!husk_jit_arena_open(a->region_bytes ? a->region_bytes : (size_t)192 << 20, m->jit_ceiling_bytes, err, errlen)) return false;
    if (husk_jit_arena_ceiling_bytes() < m->jit_ceiling_bytes) {
        snprintf(err, errlen, "JIT 内存区域为 %zu MiB；%s 需要 %zu MiB", husk_jit_arena_held_bytes() >> 20, m->platform,
                 m->jit_ceiling_bytes >> 20);
        return false;
    }

    memset(&g_host, 0, sizeof(g_host));
    g_host.struct_size = sizeof(g_host);
    g_host.version = HUSK_CONSOLE_ABI_VERSION;
    g_host.jit_alloc = host_jit_alloc;
    g_host.jit_free = host_jit_free;
    g_host.jit_delta = husk_jit_arena_delta();
    g_host.jit_rw_base = (uintptr_t)husk_jit_arena_rw_base();
    g_host.jit_size = husk_jit_arena_held_bytes();
    g_host.available_memory = host_available_memory;
    g_host.footprint = host_footprint;
    g_host.log_line = host_log_line;
    g_host.vulkan_library_path = a->vulkan_path;
    g_host.request_text = host_request_text;

    host_log("console: %s module, JIT region %zu MiB (ceiling %zu MiB), %zu MiB available", m->platform,
             husk_jit_arena_held_bytes() >> 20, husk_jit_arena_ceiling_bytes() >> 20, (size_t)(host_available_memory() >> 20));
    if (m->init(&g_host, a->user_dir) != 0) { snprintf(err, errlen, "%s 模块未能启动，请查看日志", m->platform); return false; }
    if (m->launch(a->game, a->game_args, a->metal_layer, a->width_px, a->height_px, a->scale) != 0) {
        snprintf(err, errlen, "%s 模块无法启动 %s", m->platform, a->game);
        return false;
    }
    snprintf(g_game_id, sizeof(g_game_id), "%s", a->game_id ? a->game_id : a->game);
    g_module = m;
    atexit(log_exit);
    pthread_create(&g_pad_thread, NULL, pad_loop, NULL);
    pthread_detach(g_pad_thread);
    return true;
}

const char *husk_console_loaded_game(void) { return g_module ? g_game_id : NULL; }

int husk_console_state(void) { return g_module ? g_module->state() : HUSK_CONSOLE_IDLE; }
uint64_t husk_console_frames(void) { return g_module ? g_module->frames() : 0; }
void husk_console_set_paused(bool paused) { if (g_module) g_module->set_paused(paused ? 1 : 0); }
void husk_console_set_pad(const HuskPadV1 *pad) { if (g_module) g_module->set_pad(pad); }
void husk_console_resize(int width_px, int height_px) { if (g_module) g_module->resize(width_px, height_px); }
void husk_console_stop(void) { if (g_module) g_module->stop(); }

bool husk_console_take_pending_restart(char *path, int path_size, char *args, int args_size)
{
    return g_module && g_module->take_pending_restart(path, path_size, args, args_size) != 0;
}

bool husk_console_install_package(const char *module_path, const char *package, const char *dest_dir,
                                  void (*progress)(void *ctx, uint64_t done, uint64_t total), void *ctx, char *err, size_t errlen)
{
    /* The same handle launch gets later: dlopen counts references to one image. */
    void *lib = dlopen(module_path, RTLD_NOW | RTLD_LOCAL);
    if (!lib) { snprintf(err, errlen, "无法加载 %s：%s", module_path, dlerror()); return false; }
    husk_console_module_v1_fn entry = (husk_console_module_v1_fn)dlsym(lib, "husk_console_module_v1");
    const HuskConsoleModuleV1 *m = entry ? entry() : NULL;
    if (!m || m->version != HUSK_CONSOLE_ABI_VERSION || m->struct_size < offsetof(HuskConsoleModuleV1, install_package) + sizeof(void *)
        || !m->install_package) {
        snprintf(err, errlen, "此版本的主机模块不支持安装游戏包");
        return false;
    }
    return m->install_package(package, dest_dir, progress, ctx, err, (int)errlen) == 0;
}
