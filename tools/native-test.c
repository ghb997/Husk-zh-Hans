/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host harness for the native half of the translation layer: loads an APK's
 * arm64 libraries with the guest linker, on a Mac, with the same two-view
 * executable memory the phone provides.
 *
 *   native-test <apk> [lib.so ...]       load these (default: every arm64 lib)
 *
 * Environment:
 *   TL_INIT=1      also run constructors
 *   TL_VERBOSE=2   log every unresolved import
 *   TL_SKIP=a.so,b.so   leave these out
 */
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/ucontext.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "husk-tl-internal.h"
#include "husk-tl-ld.h"
#include "husk-tl-xmem.h"

void tl_log_line(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap);
}

#ifdef NATIVE_TEST_STUB_BIONIC
void *tl_bionic_find(const char *name) { (void)name; return NULL; }
bool tl_bionic_is_system_lib(const char *s)
{
    static const char *sys[] = { "liblog.so", "libm.so", "libdl.so", "libc.so", "libGLESv3.so", "libGLESv2.so",
        "libEGL.so", "libandroid.so", "libz.so", "libmediandk.so", "libvulkan.so", "libOpenSLES.so", NULL };
    for (int i = 0; sys[i]; i++) if (!strcmp(sys[i], s)) return true;
    return false;
}
#endif


/* A crash in guest code, described in guest terms: library, function, offset, and which
 * view of the executable region a faulting address was in. */
static void describe(const char *label, const void *addr)
{
    const char *lib = NULL; const void *sa = NULL;
    const char *sym = tl_ld_symbol_at(addr, &lib, &sa);
    if (lib) fprintf(stderr, "  %-6s %p  %s  %s+%#lx\n", label, addr, lib, sym ? sym : "?", sa ? (unsigned long)((const char *)addr - (const char *)sa) : 0ul);
    else fprintf(stderr, "  %-6s %p\n", label, addr);
}

static void on_crash(int sig, siginfo_t *info, void *uctx)
{
    ucontext_t *uc = uctx;
    _STRUCT_ARM_THREAD_STATE64 *ss = &uc->uc_mcontext->__ss;
    fprintf(stderr, "\n=== CRASH: signal %d, fault address %p ===\n", sig, info->si_addr);
    describe("pc", (void *)ss->__pc);
    describe("lr", (void *)ss->__lr);
    if (info->si_addr) {
        tl_lib *L = tl_ld_lib_of(info->si_addr);
        const char *view = "outside the guest region";
        if (L) view = tl_xmem_contains(info->si_addr) ? "inside a guest image" : "?";
        fprintf(stderr, "  fault  %p is %s%s\n", info->si_addr, view,
                (L && (const uint8_t *)info->si_addr >= (const uint8_t *)tl_ld_sym(L, "") ) ? "" : "");
        describe("fault", info->si_addr);
    }
    uint64_t *fp = (uint64_t *)ss->__fp;
    for (int i = 0; i < 12 && fp && ((uintptr_t)fp & 7) == 0 && (uintptr_t)fp > 0x100000000ull; i++) {
        describe("frame", (void *)fp[1]);
        fp = (uint64_t *)fp[0];
    }
    fprintf(stderr, "  x0=%#llx x1=%#llx x2=%#llx x3=%#llx x8=%#llx x9=%#llx x19=%#llx x20=%#llx x21=%#llx\n",
            ss->__x[0], ss->__x[1], ss->__x[2], ss->__x[3], ss->__x[8], ss->__x[9], ss->__x[19], ss->__x[20], ss->__x[21]);
    fflush(stderr);
    _exit(139);
}

static void install_crash_handler(void)
{
    static uint8_t altstack[65536];
    stack_t ss = { .ss_sp = altstack, .ss_size = sizeof(altstack), .ss_flags = 0 };
    sigaltstack(&ss, NULL);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = on_crash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGTRAP, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}

int main(int argc, char **argv)
{
    install_crash_handler();
    if (argc < 2) { fprintf(stderr, "usage: %s <apk> [lib.so ...]\n", argv[0]); return 2; }
    const char *v = getenv("TL_VERBOSE");
    tl_ld_set_verbosity(v ? atoi(v) : 1);
    if (!tl_ld_add_apk(argv[1])) return 1;

    tl_zip z; char err[160];
    if (!tl_zip_open(&z, argv[1], err, sizeof(err))) { fprintf(stderr, "%s\n", err); return 1; }
    const char *names[64]; int n = 0;
    static char store[64][96];
    if (argc > 2) {
        for (int i = 2; i < argc && n < 64; i++) names[n++] = argv[i];
    } else {
        for (size_t i = 0; i < z.count && n < 64; i++) {
            const char *p = z.entries[i].name;
            if (!strncmp(p, "lib/arm64-v8a/", 14) && strstr(p, ".so")) {
                snprintf(store[n], sizeof(store[n]), "%s", p + 14);
                names[n] = store[n]; n++;
            }
        }
    }
    if (getenv("TL_SKIP")) {
        char *skip = strdup(getenv("TL_SKIP")); int k = 0;
        for (int i = 0; i < n; i++) {
            bool drop = false;
            for (char *save = skip, *tok; (tok = strsep(&save, ",")) != NULL;) if (!strcmp(tok, names[i])) drop = true;
            free(skip); skip = strdup(getenv("TL_SKIP"));
            if (!drop) names[k++] = names[i];
        }
        free(skip);
        n = k;
    }
    int failed = 0;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < n; i++) {
        tl_lib *L = tl_ld_load(names[i]);
        if (!L) { fprintf(stderr, "FAILED to load %s\n", names[i]); failed++; }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    fprintf(stderr, "loaded %d libraries in %.2fs; region %zu MiB used; %zu unresolved imports\n",
            n - failed, (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9,
            tl_xmem_used() >> 20, tl_ld_unresolved_count());
    if (getenv("TL_INIT")) {
        for (int i = 0; i < n; i++) {
            tl_lib *L = tl_ld_find_lib(names[i]);
            if (L) tl_ld_init(L);
        }
    }
    return failed ? 1 : 0;
}
