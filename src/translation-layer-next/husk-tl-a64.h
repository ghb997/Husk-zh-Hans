/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Just enough of the AArch64 encoding to know which fields of an instruction name a
 * general-purpose register.
 *
 * Why the loader needs this. Apple reserves x18 and the kernel zeroes it whenever a
 * thread takes an interrupt or a page fault, while Android compilers treat it as an
 * ordinary scratch register and keep live values in it. Any guest instruction that
 * names x18 therefore has to be rewritten, and the rewrite is a substitution of one
 * register for another in exactly the fields that are registers -- an immediate
 * whose bits happen to read 18 must be left alone, and so must a prefetch
 * operation or the NZCV mask of a conditional compare.
 */
#ifndef HUSK_TL_A64_H
#define HUSK_TL_A64_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The bit positions (0, 5, 10 or 16) of the fields of `insn` that are general-purpose
 * registers, in `shifts`, and how many in `*n`. Returns false for an encoding class
 * this decoder does not know, in which case nothing can be said about its fields.
 * Fields that hold register 31 (the zero register or sp) are listed like any other.
 */
bool a64_gpr_fields(uint32_t insn, uint8_t shifts[5], int *n);

/*
 * Whether any general-purpose register field of `insn` is `reg`. `*known` is false
 * when the encoding is not understood and a field merely looks like `reg`.
 */
bool a64_uses_gpr(uint32_t insn, unsigned reg, bool *known);

/* `insn` with every general-purpose register field equal to `from` changed to `to`. */
uint32_t a64_subst_gpr(uint32_t insn, unsigned from, unsigned to);

/* The set of register numbers named by general-purpose fields of `insn`, as a bit mask. */
uint32_t a64_gpr_mask(uint32_t insn);

#ifdef __cplusplus
}
#endif

#endif
