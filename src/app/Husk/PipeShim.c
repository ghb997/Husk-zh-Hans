/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include "fishhook.h"

/*
 * Implementation of pipe2 for iOS systems where libc does not export it.
 *
 * In macOS 15 / iOS 18 SDKs, pipe2 was introduced. When GLib was compiled
 * against those headers, it detected HAVE_PIPE2 and emitted weak calls to
 * pipe2(). On iOS 16 and 17 devices, libSystem does not implement pipe2,
 * so the weak symbol resolves to NULL at runtime, causing g_unix_open_pipe()
 * to branch to 0x0 and crash with SIGSEGV (signal 11) inside qemu_init().
 *
 * Using fishhook at process startup, we patch the GOT in all loaded images
 * (including libqemu-aarch64-softmmu.dylib) to point pipe2 to this implementation.
 */
__attribute__((visibility("default")))
int pipe2(int fds[2], int flags)
{
    if (!fds) {
        errno = EFAULT;
        return -1;
    }
    if (pipe(fds) < 0) {
        return -1;
    }
    if (flags & O_CLOEXEC) {
        if (fcntl(fds[0], F_SETFD, FD_CLOEXEC) < 0 ||
            fcntl(fds[1], F_SETFD, FD_CLOEXEC) < 0) {
            int err = errno;
            close(fds[0]);
            close(fds[1]);
            errno = err;
            return -1;
        }
    }
    if (flags & O_NONBLOCK) {
        int f0 = fcntl(fds[0], F_GETFL);
        int f1 = fcntl(fds[1], F_GETFL);
        if (f0 < 0 || f1 < 0 ||
            fcntl(fds[0], F_SETFL, f0 | O_NONBLOCK) < 0 ||
            fcntl(fds[1], F_SETFL, f1 | O_NONBLOCK) < 0) {
            int err = errno;
            close(fds[0]);
            close(fds[1]);
            errno = err;
            return -1;
        }
    }
    return 0;
}

__attribute__((constructor(101)))
static void husk_install_pipe_shim(void)
{
    struct rebinding rebindings[] = {
        {"pipe2", (void *)pipe2, NULL}
    };
    rebind_symbols(rebindings, 1);
    fprintf(stderr, "[pipe2-shim] pipe2 rebind hook installed\n");
}
