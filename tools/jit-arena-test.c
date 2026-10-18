/*
 * jit-arena-test: checks husk-jit-arena on the Mac.
 *   cc -O1 -Wall -o /tmp/jit-arena-test tools/jit-arena-test.c src/translation-layer-next/husk-jit-arena.c && /tmp/jit-arena-test
 */
#include "../src/translation-layer-next/husk-jit-arena.h"

#include <libkern/OSCacheControl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void tl_log_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("  log: ", stdout);
    vprintf(fmt, ap);
    fputc('\n', stdout);
    va_end(ap);
}

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static int run_const(void *rw, void *rx, unsigned value)
{
    /* movz w0, #value ; ret */
    uint32_t code[2] = { 0x52800000u | ((value & 0xffffu) << 5), 0xD65F03C0u };
    memcpy(rw, code, sizeof(code));
    sys_icache_invalidate(rx, sizeof(code));
    return ((int (*)(void))rx)();
}

int main(void)
{
    const size_t P = HUSK_JIT_ARENA_PAGE, MiB = 1u << 20;
    char err[256] = "";
    CHECK(!husk_jit_arena_is_open());
    CHECK(husk_jit_arena_alloc(P, NULL) == NULL);
    if (!husk_jit_arena_open(64 * MiB, 32 * MiB, err, sizeof(err))) { printf("FAIL open: %s\n", err); return 1; }
    CHECK(husk_jit_arena_held_bytes() == 64 * MiB);
    CHECK(husk_jit_arena_ceiling_bytes() == 32 * MiB);
    CHECK(!husk_jit_arena_open(64 * MiB, 0, err, sizeof(err)) || husk_jit_arena_held_bytes() == 64 * MiB);

    /* alloc, constant delta, code written through rw runs through rx */
    void *rxa, *rxb, *rxc;
    uint8_t *a = husk_jit_arena_alloc(1, &rxa);           /* rounds up to one page */
    uint8_t *b = husk_jit_arena_alloc(3 * P, &rxb);
    uint8_t *c = husk_jit_arena_alloc(2 * P + 1, &rxc);   /* three pages */
    CHECK(a && b && c);
    CHECK((intptr_t)rxa - (intptr_t)a == husk_jit_arena_delta());
    CHECK((intptr_t)rxb - (intptr_t)b == husk_jit_arena_delta());
    CHECK((intptr_t)rxc - (intptr_t)c == husk_jit_arena_delta());
    CHECK(b == a + P && c == b + 3 * P);
    CHECK(husk_jit_arena_used_bytes() == 7 * P);
    CHECK(run_const(a, rxa, 0x1234) == 0x1234);
    CHECK(run_const(c + 2 * P, (uint8_t *)rxc + 2 * P, 0x77) == 0x77);
    CHECK(run_const(a, rxa, 0x4321) == 0x4321);   /* rewrite after running */

    /* translation with range checks */
    CHECK(husk_jit_arena_rx_of(b + 100) == (uint8_t *)rxb + 100);
    CHECK(husk_jit_arena_rw_of((uint8_t *)rxb + 100) == b + 100);
    CHECK(husk_jit_arena_rx_of(a - 1) == NULL);
    CHECK(husk_jit_arena_rw_of((uint8_t *)rxa + 64 * MiB) == NULL);
    CHECK(husk_jit_arena_rw_of(&failures) == NULL);

    /* free zeroes; neighbours coalesce; first fit reuses the hole */
    CHECK(husk_jit_arena_free(a, 1));
    CHECK(husk_jit_arena_free(b, 3 * P));
    CHECK(((uint32_t *)a)[0] == 0 && ((uint32_t *)a)[1] == 0);
    CHECK(husk_jit_arena_used_bytes() == 3 * P);
    void *rxd;
    uint8_t *d = husk_jit_arena_alloc(4 * P, &rxd);
    CHECK(d == a);
    CHECK(run_const(d + 3 * P, (uint8_t *)rxd + 3 * P, 9) == 9);

    /* bad frees change nothing */
    size_t used = husk_jit_arena_used_bytes();
    CHECK(!husk_jit_arena_free(d + 1, P));               /* not page aligned */
    CHECK(!husk_jit_arena_free(c + 3 * P, P));           /* never allocated */
    CHECK(!husk_jit_arena_free(&failures, P));           /* outside the region */
    CHECK(husk_jit_arena_used_bytes() == used);
    CHECK(husk_jit_arena_free(c, 3 * P));
    CHECK(!husk_jit_arena_free(c, 3 * P));               /* double free */

    /* the ceiling refuses; freeing makes room again */
    size_t before = husk_jit_arena_refusals();
    void *big = husk_jit_arena_alloc(32 * MiB - 4 * P, NULL);
    CHECK(big != NULL);
    CHECK(husk_jit_arena_alloc(P, NULL) == NULL);
    CHECK(husk_jit_arena_refusals() == before + 1);
    CHECK(husk_jit_arena_high_water_bytes() == 32 * MiB);
    CHECK(husk_jit_arena_free(big, 32 * MiB - 4 * P));
    CHECK(husk_jit_arena_alloc(16 * MiB, NULL) != NULL);

    printf("%s (%d failures), high water %zu KiB\n", failures ? "FAIL" : "PASS", failures, husk_jit_arena_high_water_bytes() >> 10);
    return failures ? 1 : 0;
}
