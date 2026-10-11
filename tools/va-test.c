/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Checks the variadic adapter and the printf engine by calling them the way
 * guest code does: with AAPCS64's register assignment, not Darwin's.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "husk-tl-va.h"

/* A shim like any other: snprintf(buf, n, fmt, ...). */
int tl_vai_test_snprintf(tl_va_frame *f)
{
    tl_va_list ap;
    tl_va_start(f, 3, 0, &ap);
    return tl_format((char *)f->gp[0], (size_t)f->gp[1], (const char *)f->gp[2], &ap);
}
TL_VA_STUB(tl_va_test_snprintf, tl_vai_test_snprintf);
extern void tl_va_test_snprintf(void);

/* Call `fn` with x0-x7 = gp, d0-d7 = fp, and `nstack` 8-byte stack arguments. */
uint64_t guest_call(void *fn, const uint64_t *gp, const double *fp, const uint64_t *stack, int nstack);
__asm__(
    ".text\n.p2align 2\n_guest_call:\n"
    "  stp x29, x30, [sp, #-16]!\n"
    "  mov x29, sp\n"
    "  stp x19, x20, [sp, #-16]!\n"
    "  mov x16, x0\n"            /* fn */
    "  mov x9, x1\n"             /* gp */
    "  mov x10, x2\n"            /* fp */
    "  mov x11, x3\n"            /* stack */
    "  mov w12, w4\n"            /* nstack */
    "  add w13, w12, #1\n"
    "  and w13, w13, #~1\n"      /* round up to even so sp stays 16-aligned */
    "  sub sp, sp, x13, lsl #3\n"
    "  mov x14, #0\n"
    "1: cmp w14, w12\n"
    "  b.ge 2f\n"
    "  ldr x15, [x11, x14, lsl #3]\n"
    "  str x15, [sp, x14, lsl #3]\n"
    "  add w14, w14, #1\n"
    "  b 1b\n"
    "2: ldp d0, d1, [x10, #0]\n"
    "  ldp d2, d3, [x10, #16]\n"
    "  ldp d4, d5, [x10, #32]\n"
    "  ldp d6, d7, [x10, #48]\n"
    "  ldp x0, x1, [x9, #0]\n"
    "  ldp x2, x3, [x9, #16]\n"
    "  ldp x4, x5, [x9, #32]\n"
    "  ldp x6, x7, [x9, #48]\n"
    "  blr x16\n"
    "  mov sp, x29\n"
    "  sub sp, sp, #16\n"
    "  ldp x19, x20, [sp], #16\n"
    "  ldp x29, x30, [sp], #16\n"
    "  ret\n");

static int failures;
static void check(const char *what, const char *got, const char *want)
{
    if (strcmp(got, want)) { printf("FAIL %s\n   got:  [%s]\n   want: [%s]\n", what, got, want); failures++; }
    else printf("ok   %s\n", what);
}

static union { double d; uint64_t u; } cv;
static uint64_t D(double d) { cv.d = d; return cv.u; }

int main(void)
{
    char buf[512];
    uint64_t gp[8] = {0}; double fp[8] = {0};
    uint64_t st[16];

    /* 1: mixed ints, a string and a double */
    gp[0] = (uint64_t)buf; gp[1] = sizeof(buf); gp[2] = (uint64_t)"%d %s %.2f %lld";
    gp[3] = 42; gp[4] = (uint64_t)"hi"; gp[5] = 1ull << 40; fp[0] = 3.14159;
    int n = (int)guest_call((void *)tl_va_test_snprintf, gp, fp, st, 0);
    check("mixed int/string/double/long long", buf, "42 hi 3.14 1099511627776");
    if (n != 24) { printf("FAIL length %d\n", n); failures++; }

    /* 2: ten ints -> five on the stack */
    memset(gp, 0, sizeof gp);
    gp[0] = (uint64_t)buf; gp[1] = sizeof(buf); gp[2] = (uint64_t)"%d %d %d %d %d %d %d %d %d %d";
    for (int i = 0; i < 5; i++) gp[3 + i] = (uint64_t)(i + 1);
    for (int i = 0; i < 5; i++) st[i] = (uint64_t)(i + 6);
    guest_call((void *)tl_va_test_snprintf, gp, fp, st, 5);
    check("ints spilling onto the stack", buf, "1 2 3 4 5 6 7 8 9 10");

    /* 3: ten doubles -> two on the stack, interleaved with an int */
    memset(gp, 0, sizeof gp); memset(fp, 0, sizeof fp);
    gp[0] = (uint64_t)buf; gp[1] = sizeof(buf); gp[2] = (uint64_t)"%.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %d %.1f";
    for (int i = 0; i < 8; i++) fp[i] = i + 0.5;
    gp[3] = 77;
    st[0] = D(8.5); st[1] = D(9.5);
    /* args: 8 doubles in d0-d7, then the 9th double and the int: the int takes x3, the 9th double goes to the stack, the 10th (the last %.1f) is the next stack slot */
    st[0] = D(8.5); st[1] = D(9.5);
    guest_call((void *)tl_va_test_snprintf, gp, fp, st, 2);
    check("doubles spilling onto the stack", buf, "0.5 1.5 2.5 3.5 4.5 5.5 6.5 7.5 8.5 77 9.5");

    /* 4: formatting breadth */
    memset(gp, 0, sizeof gp); memset(fp, 0, sizeof fp);
    gp[0] = (uint64_t)buf; gp[1] = sizeof(buf);
    gp[2] = (uint64_t)"[%5d][%-6s][%05d][%x][%c][%%][%ld][%zu][%.3s][%*d][%p][%8.3f]";
    gp[3] = 42; gp[4] = (uint64_t)"ab"; gp[5] = 7; gp[6] = 0xbeef; gp[7] = 'Z';
    st[0] = (uint64_t)-5; st[1] = 123456789012ull; st[2] = (uint64_t)"truncated"; st[3] = 6; st[4] = 99; st[5] = 0x1234;
    fp[0] = 2.71828;
    guest_call((void *)tl_va_test_snprintf, gp, fp, st, 6);
    check("flags, widths, lengths, %*d, %p", buf, "[   42][ab    ][00007][beef][Z][%][-5][123456789012][tru][    99][0x1234][   2.718]");

    /* 5: truncation returns the full length */
    memset(gp, 0, sizeof gp);
    char small[8];
    gp[0] = (uint64_t)small; gp[1] = sizeof(small); gp[2] = (uint64_t)"%s"; gp[3] = (uint64_t)"0123456789";
    n = (int)guest_call((void *)tl_va_test_snprintf, gp, fp, st, 0);
    check("truncated output", small, "0123456");
    if (n != 10) { printf("FAIL truncated length %d\n", n); failures++; }

    /* 6: a guest va_list struct */
    struct { uint64_t g[8]; unsigned __int128 v[8]; uint64_t s[2]; } area = { .g = {0}, .s = { 11, 22 } };
    area.g[6] = 5; area.g[7] = 6;
    tl_va_list ap = { .stack = area.s, .gr_top = &area.g[8], .vr_top = &area.v[8], .gr_offs = -16, .vr_offs = 0 };
    tl_format(buf, sizeof buf, "%d %d %d %d", &ap);
    check("guest va_list struct", buf, "5 6 11 22");

    printf(failures ? "=== va test FAILED (%d) ===\n" : "=== va test passed ===\n", failures);
    return failures != 0;
}
