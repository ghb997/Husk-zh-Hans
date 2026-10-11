/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "husk-tl-a64.h"

bool a64_gpr_fields(uint32_t w, uint8_t s[5], int *n)
{
    int k = 0;
#define F(shift) (s[k++] = (uint8_t)(shift))
    *n = 0;

    if ((w & 0x1C000000u) == 0x10000000u) {                         /* data processing, immediate */
        switch ((w >> 23) & 7u) {
        case 0: case 1: F(0); break;                                /* adr, adrp */
        case 2: F(0); F(5); break;                                  /* add/sub immediate */
        case 4: F(0); F(5); break;                                  /* logical immediate */
        case 5: F(0); break;                                        /* move wide */
        case 6: F(0); F(5); break;                                  /* bitfield */
        case 7: F(0); F(5); F(16); break;                           /* extract */
        default: return false;                                      /* add/sub with tags */
        }
    } else if ((w & 0x0E000000u) == 0x0A000000u) {                  /* data processing, register */
        if      ((w & 0x1F000000u) == 0x0A000000u) { F(0); F(5); F(16); }          /* logical, shifted */
        else if ((w & 0x1F000000u) == 0x0B000000u) { F(0); F(5); F(16); }          /* add/sub, shifted or extended */
        else if ((w & 0x1FE00000u) == 0x1A000000u) { F(0); F(5); F(16); }          /* with carry */
        else if ((w & 0x1FE00000u) == 0x1A400000u) { F(5); if (!(w & 0x800u)) F(16); }   /* conditional compare */
        else if ((w & 0x1FE00000u) == 0x1A800000u) { F(0); F(5); F(16); }          /* conditional select */
        else if ((w & 0x1F000000u) == 0x1B000000u) { F(0); F(5); F(10); F(16); }   /* three source */
        else if ((w & 0x5FE00000u) == 0x1AC00000u) { F(0); F(5); F(16); }          /* two source */
        else if ((w & 0x5FE00000u) == 0x5AC00000u) { F(0); F(5); }                 /* one source */
        else return false;
    } else if ((w & 0x3A000000u) == 0x28000000u) {                  /* load/store pair */
        if (!((w >> 26) & 1u)) { F(0); F(10); }
        F(5);
    } else if ((w & 0x3B000000u) == 0x18000000u) {                  /* load literal */
        if (!((w >> 26) & 1u) && ((w >> 30) & 3u) != 3u) F(0);
    } else if ((w & 0x3B000000u) == 0x38000000u || (w & 0x3B000000u) == 0x39000000u) {   /* load/store register */
        unsigned v = (w >> 26) & 1u, size = (w >> 30) & 3u, opc = (w >> 22) & 3u;
        bool prfm = !v && size == 3 && opc == 2;
        if (!v && !prfm) F(0);
        F(5);
        if (!((w >> 24) & 1u) && ((w >> 21) & 1u)) {
            unsigned b = (w >> 10) & 3u;
            if (b == 2 || b == 0) F(16);                            /* register offset; atomic memory operation */
            else return false;                                      /* pointer-authenticated */
        }
    } else if ((w & 0x3F000000u) == 0x08000000u) {                  /* exclusive, acquire/release, compare-and-swap */
        F(0); F(5); F(10); F(16);
    } else if ((w & 0xBE000000u) == 0x0C000000u) {                  /* SIMD load/store structure */
        F(5);
        if ((w >> 23) & 1u) F(16);                                  /* post-indexed by register */
    } else if ((w & 0x0E000000u) == 0x0E000000u) {                  /* SIMD and floating point */
        if ((w & 0x7F20FC00u) == 0x1E200000u) {                     /* floating point <-> integer */
            switch ((w >> 16) & 7u) {
            case 0: case 1: case 4: case 5: case 6: F(0); break;
            default: F(5); break;                                   /* scvtf, ucvtf, fmov to fp */
            }
        } else if ((w & 0xFFFFFC00u) == 0x1E7E0000u) {              /* fjcvtzs */
            F(0);
        } else if ((w & 0x7F200000u) == 0x1E000000u) {              /* fixed-point conversion */
            switch ((w >> 16) & 7u) {
            case 2: case 3: F(5); break;
            default: F(0); break;
            }
        } else if ((w & 0x9FE08400u) == 0x0E000400u) {              /* advanced SIMD copy */
            switch ((w >> 11) & 0xFu) {
            case 1: case 3: F(5); break;                            /* dup, ins from a general register */
            case 5: case 7: F(0); break;                            /* smov, umov */
            default: break;
            }
        }
    } else if ((w & 0xFFC00000u) == 0xD5000000u) {                  /* system */
        F(0);
    } else if ((w & 0x7E000000u) == 0x34000000u || (w & 0x7E000000u) == 0x36000000u) {   /* cbz, tbz */
        F(0);
    } else if ((w & 0xFE000000u) == 0xD6000000u) {                  /* br, blr, ret */
        F(5);
    } else if ((w & 0x7C000000u) == 0x14000000u || (w & 0xFF000010u) == 0x54000000u || (w & 0xFFE0001Fu) == 0xD4000001u) {
        /* b, bl, b.cond, svc: no registers */
    } else {
        return false;
    }
    *n = k;
    return true;
#undef F
}

uint32_t a64_gpr_mask(uint32_t insn)
{
    uint8_t s[5]; int n; uint32_t m = 0;
    if (!a64_gpr_fields(insn, s, &n)) return 0;
    for (int i = 0; i < n; i++) m |= 1u << ((insn >> s[i]) & 31u);
    return m;
}

bool a64_uses_gpr(uint32_t insn, unsigned reg, bool *known)
{
    uint8_t s[5]; int n;
    bool ok = a64_gpr_fields(insn, s, &n);
    if (known) *known = ok;
    if (!ok) return ((insn & 31u) == reg) || (((insn >> 5) & 31u) == reg) || (((insn >> 10) & 31u) == reg) || (((insn >> 16) & 31u) == reg);
    for (int i = 0; i < n; i++) if (((insn >> s[i]) & 31u) == reg) return true;
    return false;
}

uint32_t a64_subst_gpr(uint32_t insn, unsigned from, unsigned to)
{
    uint8_t s[5]; int n;
    if (!a64_gpr_fields(insn, s, &n)) return insn;
    uint32_t out = insn;
    for (int i = 0; i < n; i++)
        if (((insn >> s[i]) & 31u) == from) out = (out & ~(31u << s[i])) | ((to & 31u) << s[i]);
    return out;
}
