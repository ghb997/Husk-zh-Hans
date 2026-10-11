/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The attempt, driven from a Mac.
 *
 * Apple Silicon macOS is the same arm64 XNU the iPhone runs: 16 KiB pages,
 * MAP_JIT, pthread_jit_write_protect_np. Everything the loader needs from
 * the phone, this machine has, so the whole path -- APK, ELF, carve,
 * relocations, shim, guest constructors, the guest's own code -- is
 * testable right here before an IPA is built. This is the harness for it.
 *
 * Usage: attempt_host <apk-or-directory> [seconds]
 *
 * Build (from the repo root):
 *   clang -arch arm64 -o /tmp/attempt_host tests/translation-layer/attempt_host.c \
 *       src/translation-layer/{husk-tl-load,husk-tl-shim,husk-tl-elf,husk-tl-zip,husk-tl-json,husk-tl-scan,husk-tl-probe}.c \
 *       -I src/translation-layer -lz -framework Foundation
 *
 * Exit code: 0 refused, 1 loaded, 2 drew -- the same numbers the attempt
 * reports, so a CI run reads the same signal the phone gives.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "husk-tl.h"

static int ends_with(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lx = strlen(suffix);
    return ls >= lx && !strcmp(s + ls - lx, suffix);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <apk-or-directory> [seconds]\n", argv[0]);
        return 2;
    }
    int seconds = argc > 2 ? atoi(argv[2]) : 5;

    /* Collect APK paths: a file, or every .apk in a directory. */
    char *apks[64];
    int n = 0;
    if (ends_with(argv[1], ".apk")) {
        apks[n++] = argv[1];
    } else {
        DIR *d = opendir(argv[1]);
        if (!d) {
            perror(argv[1]);
            return 2;
        }
        struct dirent *e;
        while ((e = readdir(d)) && n < 64) {
            if (ends_with(e->d_name, ".apk")) {
                char path[1024];
                snprintf(path, sizeof(path), "%s/%s", argv[1], e->d_name);
                apks[n++] = strdup(path);
            }
        }
        closedir(d);
    }
    if (n == 0) {
        fprintf(stderr, "no APK found in %s\n", argv[1]);
        return 2;
    }

    const char *paths[64];
    for (int i = 0; i < n; i++) {
        paths[i] = apks[i];
    }

    int rc = husk_tl_attempt_start(paths, n, seconds);
    if (rc != 0) {
        fprintf(stderr, "attempt could not start\n");
        return 2;
    }
    while (!husk_tl_attempt_done(NULL)) {
        struct timespec t = { 0, 100 * 1000000 };
        nanosleep(&t, NULL);
    }
    int code = 0;
    husk_tl_attempt_done(&code);

    char *log = husk_tl_attempt_log();
    fputs(log, stdout);
    free(log);
    printf("\nframes: %d, exit: %d\n", husk_tl_attempt_frames(), code);
    printf("%s\n", code == 2 ? "== DREW ==" : code == 1 ? "== LOADED ==" : "== REFUSED ==");
    husk_tl_attempt_reset();
    return code;
}
