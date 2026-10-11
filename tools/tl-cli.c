/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <mach/mach.h>
#include "husk-tl.h"

typedef struct {
    uint8_t *rw_addr;
    uint8_t *rx_addr;
    size_t   size;
} tl_dual_mapping;

static tl_dual_mapping g_cli_dual = {0};

tl_dual_mapping *husk_ios_jit_get_mapping(void)
{
    return g_cli_dual.rx_addr ? &g_cli_dual : NULL;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [--dualmap] <path-to-apk>\n", argv[0]);
        return 1;
    }

    bool use_dualmap = false;
    const char *apk_path = argv[1];
    if (!strcmp(argv[1], "--dualmap")) {
        use_dualmap = true;
        if (argc < 3) {
            fprintf(stderr, "Usage: %s [--dualmap] <path-to-apk>\n", argv[0]);
            return 1;
        }
        apk_path = argv[2];
    }

    if (use_dualmap) {
        size_t sz = 16 * 1024 * 1024;
        vm_address_t rx = 0;
        kern_return_t kr = vm_allocate(mach_task_self(), &rx, sz, VM_FLAGS_ANYWHERE);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "Failed to allocate mock region (%d)\n", (int)kr);
            return 1;
        }
        vm_address_t rw = 0;
        vm_prot_t cur = 0, max = 0;
        kr = vm_remap(mach_task_self(), &rw, sz, 0, VM_FLAGS_ANYWHERE,
                      mach_task_self(), rx, FALSE,
                      &cur, &max, VM_INHERIT_NONE);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "Failed to vm_remap mock JIT region (%d)\n", (int)kr);
            return 1;
        }
        kr = vm_protect(mach_task_self(), rx, sz, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "Failed to vm_protect RX (%d)\n", (int)kr);
            return 1;
        }
        kr = vm_protect(mach_task_self(), rw, sz, FALSE, VM_PROT_READ | VM_PROT_WRITE);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "Failed to vm_protect RW alias (%d)\n", (int)kr);
            return 1;
        }
        g_cli_dual.rx_addr = (uint8_t *)rx;
        g_cli_dual.rw_addr = (uint8_t *)rw;
        g_cli_dual.size = sz;
        printf("=== Mock StikDebug Dual Mapping: rx=%p rw=%p (diff=%+lld) ===\n",
               g_cli_dual.rx_addr, g_cli_dual.rw_addr,
               (long long)(g_cli_dual.rw_addr - g_cli_dual.rx_addr));
    }

    const char *apks[] = { apk_path };
    printf("=== Starting Husk Translation Layer Attempt ===\n");
    printf("Target APK: %s\n", apk_path);

    int rc = husk_tl_attempt_start(apks, 1, 5);
    if (rc != 0) {
        fprintf(stderr, "Error: husk_tl_attempt_start failed (%d)\n", rc);
        return 1;
    }

    int exit_code = 0;
    while (!husk_tl_attempt_done(&exit_code)) {
        char *log = husk_tl_attempt_log();
        if (log && *log) {
            fputs(log, stdout);
            fflush(stdout);
        }
        usleep(50 * 1000);
    }

    char *log = husk_tl_attempt_log();
    if (log && *log) {
        fputs(log, stdout);
        fflush(stdout);
    }

    printf("=== Attempt finished (exit_code=%d, frames=%d) ===\n",
           exit_code, husk_tl_attempt_frames());
    husk_tl_attempt_reset();
    return 0;
}
