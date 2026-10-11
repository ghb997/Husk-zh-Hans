/* Reads instruction words (hex, one per line) and prints each with x18 replaced by x9, or "-" if it does not name x18. */
#include <stdio.h>
#include <stdlib.h>
#include "husk-tl-a64.h"
int main(void)
{
    char line[64];
    while (fgets(line, sizeof(line), stdin)) {
        uint32_t w = (uint32_t)strtoul(line, NULL, 16);
        bool known;
        bool uses = a64_uses_gpr(w, 18, &known);
        if (!known) { printf("UNKNOWN %08x\n", w); continue; }
        if (!uses) { printf("- %08x\n", w); continue; }
        printf("%08x %08x\n", w, a64_subst_gpr(w, 18, 9));
    }
    return 0;
}
