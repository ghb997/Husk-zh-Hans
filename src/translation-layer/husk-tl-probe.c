/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The questions only the device can answer.
 *
 * Running an Android library natively on iOS rests on a few facts about XNU
 * that no document states and no simulator reproduces. Each check here
 * measures one, and each is chosen because its answer changes the design:
 *
 *   tpidr    Android's arm64 code reads its stack-protector cookie from the
 *            thread register, TPIDR_EL0 + 0x28, in nearly every function. If
 *            iOS leaves that register to us and keeps it across context
 *            switches, Android code runs as it is. If not, every one of those
 *            reads has to be rewritten at load time.
 *   x18      Reserved on both platforms, but code built with shadow call
 *            stacks (and some older libraries) keeps live values in it.
 *   carve    A library's code must execute and its data must be writable,
 *            at a fixed distance from each other -- the code finds its data
 *            PC-relatively. So a library needs ordinary memory directly beside
 *            executable memory. Whether XNU allows that inside a MAP_JIT
 *            mapping, or allows a MAP_JIT page to be placed beside ordinary
 *            memory, decides how the loader lays images out.
 *
 * Every check that executes generated code does so under a signal guard and
 * only after asking the kernel whether the page is executable, the same
 * double guard Husk's MAP_JIT test uses: a check must not become the thing
 * that killed the app.
 */
#include "husk-tl.h"
#include "husk-tl-internal.h"

#ifdef __APPLE__
#include <TargetConditionals.h>
#include <dlfcn.h>
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#endif

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#ifndef MAP_ANON
#define MAP_ANON MAP_ANONYMOUS
#endif

static void result(tl_json *j, const char *id, const char *title, const char *status,
                   const char *fmt, ...) __attribute__((format(printf, 5, 6)));
static void result(tl_json *j, const char *id, const char *title, const char *status,
                   const char *fmt, ...)
{
    char detail[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(detail, sizeof(detail), fmt, ap);
    va_end(ap);
    fprintf(stderr, "[husk-tl] check %s: %s -- %s\n", id, status, detail);

    tl_json_begin_object(j);
    tl_json_key(j, "id");       tl_json_string(j, id);
    tl_json_key(j, "title");    tl_json_string(j, title);
    tl_json_key(j, "status");   tl_json_string(j, status);
    tl_json_key(j, "detail");   tl_json_string(j, detail);
    tl_json_end_object(j);
}

#if defined(__aarch64__)
/* Said before each check that could take the process down, so that if one
 * ever does, the console's last line names it. */
static void starting(const char *id)
{
    fprintf(stderr, "[husk-tl] check %s: starting\n", id);
}

/* Long enough, one time in three, to be taken off the CPU. */
static void pause_briefly(unsigned i)
{
    if (i % 3 == 0) {
        struct timespec ts = { 0, 200 * 1000 };
        nanosleep(&ts, NULL);
    } else {
        sched_yield();
    }
}
#endif

/* ---------------------------------------------------------------- tpidr */

#if defined(__aarch64__)

#define TPIDR_THREADS   4
#define TPIDR_ROUNDS    150

static inline uint64_t tpidr_get(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(v));
    return v;
}

static inline void tpidr_set(uint64_t v)
{
    __asm__ volatile("msr tpidr_el0, %0" : : "r"(v) : "memory");
}

typedef struct tpidr_run {
    uint64_t initial;
    uint64_t block[8];      /* what it points at while it is ours */
    bool     wrote;
    uint32_t rounds;
    uint32_t lost;
    uint64_t seen;          /* the first value that was not ours */
} tpidr_run;

/* Point the register at a block of our own -- never left dangling, in case
 * anything does dereference it -- and see whether it is still ours after the
 * thread has been switched out and back many times. A register someone else
 * already set is only watched, never written. */
static void *tpidr_worker(void *arg)
{
    tpidr_run *t = arg;
    t->initial = tpidr_get();
    uint64_t want = t->initial;
    if (t->initial == 0) {
        want = (uint64_t)(uintptr_t)t->block;
        tpidr_set(want);
        t->wrote = true;
    }
    for (unsigned i = 0; i < TPIDR_ROUNDS; i++) {
        pause_briefly(i);
        uint64_t v = tpidr_get();
        t->rounds++;
        if (v != want) {
            if (t->lost++ == 0) t->seen = v;
            if (t->wrote) tpidr_set(want);      /* so later rounds measure afresh */
        }
    }
    if (t->wrote) tpidr_set(t->initial);
    return NULL;
}

static volatile uint64_t g_sig_tpidr;
static volatile sig_atomic_t g_sig_seen;

static void tpidr_on_signal(int sig)
{
    (void)sig;
    g_sig_tpidr = tpidr_get();
    g_sig_seen = 1;
}

/* Signal handlers run Android code too -- crash reporters, and the runtimes
 * that stop threads with signals for garbage collection. Returns false if no
 * signal arrived to measure with. */
static bool tpidr_in_signal(uint64_t want, uint64_t *in_handler, uint64_t *after)
{
    struct sigaction sa, prev;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = tpidr_on_signal;
    sigemptyset(&sa.sa_mask);
    /* SIGWINCH: nothing on iOS has a terminal, so nothing else wants it. */
    sigaction(SIGWINCH, &sa, &prev);
    sigset_t set, old;
    sigemptyset(&set);
    sigaddset(&set, SIGWINCH);
    pthread_sigmask(SIG_UNBLOCK, &set, &old);

    uint64_t initial = tpidr_get();
    g_sig_seen = 0;
    tpidr_set(want);
    pthread_kill(pthread_self(), SIGWINCH);
    for (int i = 0; i < 100 && !g_sig_seen; i++) {
        struct timespec ts = { 0, 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
    *after = tpidr_get();
    tpidr_set(initial);

    pthread_sigmask(SIG_SETMASK, &old, NULL);
    sigaction(SIGWINCH, &prev, NULL);
    *in_handler = g_sig_tpidr;
    return g_sig_seen != 0;
}

static void check_tpidr(tl_json *j)
{
    static const char *title = "线程寄存器（TPIDR_EL0）";
    tpidr_run runs[TPIDR_THREADS];
    pthread_t threads[TPIDR_THREADS];
    memset(runs, 0, sizeof(runs));

    starting("tpidr");
    uint64_t here = tpidr_get();
    int started = 0;
    for (int i = 0; i < TPIDR_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, tpidr_worker, &runs[i]) == 0) {
            started++;
        } else {
            break;
        }
    }
    for (int i = 0; i < started; i++) {
        pthread_join(threads[i], NULL);
    }
    if (started == 0) {
        result(j, "tpidr", title, "skip", "无法创建用于测量的线程。");
        return;
    }

    uint32_t rounds = 0, lost = 0;
    uint64_t in_use = here, seen = 0;
    for (int i = 0; i < started; i++) {
        rounds += runs[i].rounds;
        if (runs[i].initial && !in_use) in_use = runs[i].initial;
        if (runs[i].lost && !lost) seen = runs[i].seen;
        lost += runs[i].lost;
    }

    if (in_use) {
        result(j, "tpidr", title, "fail",
               "已被占用：Husk 操作前其值为 0x%llx，说明系统"
               "正在使用它。Android 代码会从此寄存器读取栈保护值，"
               "需要将保护值放在它指向的位置，或改写所有读取指令。",
               (unsigned long long)in_use);
        return;
    }
    if (lost) {
        result(j, "tpidr", title, "fail",
               "值未保留：在 %d 个线程中设置后，上下文切换后读回 0x%llx，"
               "%u/%u 次读取丢失。必须在加载时改写 Android 代码中"
               "对此寄存器的所有读取操作。",
               started, (unsigned long long)seen, lost, rounds);
        return;
    }

    uint64_t block[8] = { 0 };
    uint64_t want = (uint64_t)(uintptr_t)block, in_handler = 0, after = 0;
    if (!tpidr_in_signal(want, &in_handler, &after)) {
        result(j, "tpidr", title, "pass",
               "未被占用，并在 %u 次上下文切换、%d 个线程中保留。"
               "（未收到可用于检查信号处理函数的信号。）", rounds, started);
    } else if (in_handler != want || after != want) {
        result(j, "tpidr", title, "warn",
               "在 %u 次上下文切换中保留，但信号处理函数中读到"
               "0x%llx，之后为 0x%llx。Android 代码可以运行，"
               "但进入信号处理函数时需要重新设置寄存器。",
               rounds, (unsigned long long)in_handler, (unsigned long long)after);
    } else {
        result(j, "tpidr", title, "pass",
               "可由 Husk 使用：在 %u 次上下文切换、%d 个线程以及"
               "信号处理函数中均保留。Android 代码可以直接从中"
               "读取栈保护值。", rounds, started);
    }
}

#else

static void check_tpidr(tl_json *j)
{
    result(j, "tpidr", "线程寄存器（TPIDR_EL0）", "skip", "仅适用于 arm64。");
}

#endif

/* ------------------------------------------------------------------ x18 */

#if defined(__APPLE__) && defined(__aarch64__)

/* The compiler never allocates x18 on Apple platforms, so between these asm
 * statements nothing but the kernel can change it. (On Linux it is an ordinary
 * scratch register, which is why this check is Apple-only.) */
static uint32_t x18_run(uint32_t rounds, uint64_t *seen)
{
    const uint64_t canary = 0x4855534B78313821ull;
    uint64_t saved, v;
    uint32_t lost = 0;
    __asm__ volatile("mov %0, x18" : "=r"(saved));
    __asm__ volatile("mov x18, %0" : : "r"(canary));
    for (uint32_t i = 0; i < rounds; i++) {
        pause_briefly(i);
        __asm__ volatile("mov %0, x18" : "=r"(v));
        if (v != canary) {
            if (lost++ == 0) *seen = v;
            __asm__ volatile("mov x18, %0" : : "r"(canary));
        }
    }
    __asm__ volatile("mov x18, %0" : : "r"(saved));
    return lost;
}

static void check_x18(tl_json *j)
{
    static const char *title = "平台寄存器（x18）";
    uint64_t seen = 0;
    const uint32_t rounds = 300;
    starting("x18");
    uint32_t lost = x18_run(rounds, &seen);
    if (lost == 0) {
        result(j, "x18", title, "pass",
               "在 %u 次上下文切换中保留。使用 x18 保存数据的库，"
               "包括影子调用栈和部分旧版本库，可以正常运行。", rounds);
    } else {
        result(j, "x18", title, "fail",
               "被系统改写：切换后读到 0x%llx（%u/%u 次）。当前"
               "Android 编译器生成的代码不会使用 x18，"
               "但启用影子调用栈的库或将 x18 用作临时寄存器的旧库，"
               "可能会随机出错。", (unsigned long long)seen, lost, rounds);
    }
}

#else

static void check_x18(tl_json *j)
{
    result(j, "x18", "平台寄存器（x18）", "skip",
           "仅适用于编译器保留 x18 的 Apple arm64 平台。");
}

#endif

/* ------------------------------------------------------ executable memory */

#if defined(__aarch64__)

#if defined(__APPLE__)
/* pthread_jit_write_protect_np is marked unavailable in the iOS SDK, but
 * libsystem exports it; looked up at run time so nothing links against it. */
typedef void (*write_protect_fn)(int);
typedef int  (*write_protect_supported_fn)(void);
static write_protect_fn g_write_protect;

static void jit_prepare(void)
{
    write_protect_supported_fn supported =
        (write_protect_supported_fn)dlsym(RTLD_DEFAULT, "pthread_jit_write_protect_supported_np");
    if (supported && supported()) {
        g_write_protect = (write_protect_fn)dlsym(RTLD_DEFAULT, "pthread_jit_write_protect_np");
    }
}

static void jit_writable(bool on)
{
    if (g_write_protect) g_write_protect(on ? 0 : 1);
}

static void flush_icache(void *p, size_t n) { sys_icache_invalidate(p, n); }

static bool page_executable(void *p)
{
    vm_address_t addr = (vm_address_t)p;
    vm_size_t size = 0;
    natural_t depth = 0;
    vm_region_submap_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;
    if (vm_region_recurse_64(mach_task_self(), &addr, &size, &depth,
                             (vm_region_recurse_info_t)&info, &count) != KERN_SUCCESS) {
        return false;
    }
    /* vm_region answers for the region at or after the address. */
    return addr <= (vm_address_t)p && (info.protection & VM_PROT_EXECUTE) != 0;
}

#define JIT_FLAGS (MAP_PRIVATE | MAP_ANON | MAP_JIT)
#else
static void jit_prepare(void) {}
static void jit_writable(bool on) { (void)on; }
static void flush_icache(void *p, size_t n) { __builtin___clear_cache((char *)p, (char *)p + n); }
static bool page_executable(void *p) { (void)p; return true; }
#define JIT_FLAGS (MAP_PRIVATE | MAP_ANON)
#endif

static sigjmp_buf g_guard_jump;
static volatile sig_atomic_t g_guard_armed;
static pthread_t g_guard_thread;
static struct sigaction g_prev_bus, g_prev_segv, g_prev_ill;

static void guard_handler(int sig)
{
    if (g_guard_armed && pthread_equal(pthread_self(), g_guard_thread)) {
        siglongjmp(g_guard_jump, 1);
    }
    /* Not ours: a fault on another thread inside the window. Put back whoever
     * handled it before and return; the instruction faults again and reaches
     * them, as it would have without us. */
    const struct sigaction *prev = sig == SIGBUS ? &g_prev_bus
                                 : sig == SIGSEGV ? &g_prev_segv : &g_prev_ill;
    sigaction(sig, prev, NULL);
}

static void guard_arm(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = guard_handler;
    sigemptyset(&sa.sa_mask);
    g_guard_thread = pthread_self();
    sigaction(SIGBUS, &sa, &g_prev_bus);
    sigaction(SIGSEGV, &sa, &g_prev_segv);
    sigaction(SIGILL, &sa, &g_prev_ill);
    g_guard_armed = 1;
}

static void guard_disarm(void)
{
    g_guard_armed = 0;
    sigaction(SIGBUS, &g_prev_bus, NULL);
    sigaction(SIGSEGV, &g_prev_segv, NULL);
    sigaction(SIGILL, &g_prev_ill, NULL);
}

/* adrp x1, target -- the way compiled code finds its own data. */
static uint32_t adrp_x1(const void *pc, const void *target)
{
    int64_t delta = (int64_t)(((uintptr_t)target & ~(uintptr_t)0xFFF)
                            - ((uintptr_t)pc & ~(uintptr_t)0xFFF)) >> 12;
    uint32_t imm = (uint32_t)delta & 0x1FFFFF;
    return 0x90000000u | ((imm & 3u) << 29) | ((imm >> 2) << 5) | 1u;
}

/* A function that increments the 64-bit counter at `data` and returns it,
 * addressing it exactly as a library addresses its globals. Assembled into
 * `code` for execution at `pc`. `data` must be page-aligned, so the low 12
 * bits are zero and no add is needed. */
static void counter_fn(uint32_t code[5], const void *pc, const void *data)
{
    code[0] = adrp_x1(pc, data);
    code[1] = 0xF9400020u;      /* ldr x0, [x1]      */
    code[2] = 0x91000400u;      /* add x0, x0, #1    */
    code[3] = 0xF9000020u;      /* str x0, [x1]      */
    code[4] = 0xD65F03C0u;      /* ret               */
}

static const uint32_t kReturn7[2] = { 0x528000E0u, 0xD65F03C0u };   /* mov w0, #7; ret */

/*
 * Variant A: take executable memory, then replace one page in the middle of
 * it with ordinary memory. The loader would carve a library's writable pages
 * out of its JIT region this way.
 */
static void check_carve(tl_json *j, size_t page)
{
    static const char *title = "在 JIT 内存中分隔相邻的代码与数据";
    starting("carve");
    uint8_t *r = mmap(NULL, 3 * page, PROT_READ | PROT_WRITE | PROT_EXEC, JIT_FLAGS, -1, 0);
    if (r == MAP_FAILED) {
        result(j, "carve", title, "fail", "无可执行内存映射：%s。", strerror(errno));
        return;
    }
    uint8_t *data = r + page, *tail = r + 2 * page;
    const char *volatile status = "fail";
    char why[512] = "";
    guard_arm();
    if (sigsetjmp(g_guard_jump, 1) == 0) {
        uint32_t fn[5];
        counter_fn(fn, r, data);
        jit_writable(true);
        memcpy(r, fn, sizeof(fn));
        memcpy(tail, kReturn7, sizeof(kReturn7));
        jit_writable(false);
        flush_icache(r, sizeof(fn));
        flush_icache(tail, sizeof(kReturn7));

        void *d = mmap(data, page, PROT_READ | PROT_WRITE,
                       MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0);
        if (d != data) {
            snprintf(why, sizeof(why), "内核不允许替换 JIT 映射中的页面"
                     "（%s）。", d == MAP_FAILED ? strerror(errno) : "moved");
        } else if (!page_executable(r) || !page_executable(tail)) {
            snprintf(why, sizeof(why), "替换一个页面后，"
                     "相邻两侧页面失去了执行权限。");
        } else {
            *(volatile uint64_t *)data = 41;
            uint64_t got = ((uint64_t (*)(void))(void *)r)();
            int seven = ((int (*)(void))(void *)tail)();
            if (got == 42 && *(volatile uint64_t *)data == 42 && seven == 7) {
                status = "pass";
            } else {
                snprintf(why, sizeof(why), "代码已执行，但返回结果错误"
                         "(%llu, %d).", (unsigned long long)got, seven);
            }
        }
    } else {
        snprintf(why, sizeof(why), "写入或执行代码时发生错误。");
    }
    guard_disarm();
    munmap(r, 3 * page);

    if (!strcmp(status, "pass")) {
        result(j, "carve", title, "pass",
               "MAP_JIT 映射中间的页面已替换为普通内存。"
               "前方代码仍能执行，并通过 PC 相对寻址写入该页面；"
               "后方代码也能正常执行，说明库的代码与数据"
               "可以相邻放置。");
    } else {
        result(j, "carve", title, "fail", "%s", why);
    }
}

/*
 * Variant B: take ordinary memory, then put an executable page at the front
 * of it. The other way round -- if XNU refuses A, the loader can lay each
 * image out in plain memory and drop code pages into it.
 */
static void check_place(tl_json *j, size_t page)
{
    static const char *title = "将代码放置在数据之前";
    starting("place");
    uint8_t *r = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (r == MAP_FAILED) {
        result(j, "place", title, "fail", "无法预留内存：%s。", strerror(errno));
        return;
    }
    uint8_t *data = r + page;
    const char *volatile status = "fail";
    char why[512] = "";

    void *c = mmap(r, page, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_FIXED | JIT_FLAGS, -1, 0);
    if (c != r) {
        snprintf(why, sizeof(why), "内核不允许在指定地址"
                 "映射可执行内存（%s）。", c == MAP_FAILED ? strerror(errno) : "moved");
    } else {
        guard_arm();
        if (sigsetjmp(g_guard_jump, 1) == 0) {
            uint32_t fn[5];
            counter_fn(fn, r, data);
            jit_writable(true);
            memcpy(r, fn, sizeof(fn));
            jit_writable(false);
            flush_icache(r, sizeof(fn));
            *(volatile uint64_t *)data = 99;
            if (!page_executable(r)) {
                snprintf(why, sizeof(why), "页面已映射，但没有执行权限。");
            } else {
                uint64_t got = ((uint64_t (*)(void))(void *)r)();
                if (got == 100 && *(volatile uint64_t *)data == 100) {
                    status = "pass";
                } else {
                    snprintf(why, sizeof(why), "代码已执行，但返回 %llu。",
                             (unsigned long long)got);
                }
            }
        } else {
            snprintf(why, sizeof(why), "写入或执行代码时发生错误。");
        }
        guard_disarm();
    }
    munmap(r, 2 * page);

    if (!strcmp(status, "pass")) {
        result(j, "place", title, "pass",
               "已在普通内存前方的指定地址映射可执行页面，"
               "其中的代码可以通过 PC 相对寻址写入该内存。");
    } else {
        result(j, "place", title, "fail", "%s", why);
    }
}

#endif /* __aarch64__ */

#if defined(__aarch64__)
static void check_dualmap(tl_json *j, size_t page, tl_dual_mapping *stik)
{
    static const char *title = "在调试器授权的内存中相邻放置代码与数据";
    if (!stik || !stik->rw_addr || !stik->rx_addr || stik->size < 2 * page) {
        result(j, "dualmap", title, "skip",
               "当前进程中没有可用的调试器授权双重映射。");
        return;
    }
    starting("dualmap");
    size_t test_off = stik->size - 2 * page;
    uint8_t *rw = stik->rw_addr + test_off;
    uint8_t *rx = stik->rx_addr + test_off;

    uint32_t code[2] = { 0x52800540u, 0xD65F03C0u }; /* movz w0, #42; ret */
    memcpy(rw, code, sizeof(code));
    sys_icache_invalidate(rx, sizeof(code));

    guard_arm();
    const char *status = "fail";
    char why[512] = "";
    if (sigsetjmp(g_guard_jump, 1) == 0) {
        int (*fn)(void) = (int (*)(void))(void *)rx;
        int ans = fn();
        if (ans == 42) {
            status = "pass";
        } else {
            snprintf(why, sizeof(why), "代码通过 RX 别名执行，但返回 %d，预期为 42。", ans);
        }
    } else {
        snprintf(why, sizeof(why), "从 RX 别名执行代码时发生错误。");
    }
    guard_disarm();

    if (!strcmp(status, "pass")) {
        result(j, "dualmap", title, "pass",
               "StikDebug 双重映射 JIT 内存已生效（rw=%p，rx=%p，大小=%zu MiB）。"
               "写入 RW 别名的代码可以通过 RX 别名正常执行。",
               (void *)stik->rw_addr, (void *)stik->rx_addr, stik->size / (1024 * 1024));
    } else {
        result(j, "dualmap", title, "fail", "%s", why);
    }
}
#endif

static void check_memory(tl_json *j, bool may_execute, size_t page)
{
#if defined(__aarch64__)
    if (may_execute) {
        jit_prepare();
        check_carve(j, page);
        check_place(j, page);
    } else {
        const char *why = "在 TXM 设备（A17 Pro 及更新机型）或禁止非特权 MAP_JIT"
                          "的设备上跳过，改用 StikDebug 双重映射。";
        result(j, "carve", "在 JIT 内存中分隔相邻的代码与数据", "skip", "%s", why);
        result(j, "place", "将代码放置在数据之前", "skip", "%s", why);
    }

    tl_dual_mapping *stik = tl_find_stikdebug_prewarmed();
    if (stik && stik->rw_addr && stik->rx_addr) {
        check_dualmap(j, page, stik);
    } else {
        result(j, "dualmap", "在调试器授权的内存中相邻放置代码与数据", "skip",
               "尚未获取 StikDebug JIT 内存区域。请附加 StikDebug 后启动 QEMU 或尝试运行应用。");
    }
#else
    (void)may_execute;
    (void)page;
    result(j, "carve", "在 JIT 内存中分隔相邻的代码与数据", "skip", "仅适用于 arm64。");
    result(j, "place", "将代码放置在数据之前", "skip", "仅适用于 arm64。");
    result(j, "dualmap", "在调试器授权的内存中相邻放置代码与数据", "skip", "仅适用于 arm64。");
#endif
}

char *husk_tl_run_checks(bool may_execute)
{
    tl_json j;
    tl_json_init(&j);
    tl_json_begin_array(&j);

    long page = sysconf(_SC_PAGESIZE);
    result(&j, "page", "页面大小", "info",
           "%ld 字节。%s", page,
           page > 4096 ? " 对于按 4 KiB 页面构建的库，会重新安排内存布局；"
                         "可在应用报告中查看是否兼容。" : "");

    check_tpidr(&j);
    check_x18(&j);
    check_memory(&j, may_execute, page > 0 ? (size_t)page : 16384);

    tl_json_end_array(&j);
    return tl_json_finish(&j);
}
