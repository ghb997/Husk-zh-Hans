/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Checks the resource table against a real APK, in the one way that cannot be
 * argued with: every drawable the APK actually contains must be reachable by
 * name, and the id that name gives must lead back to the same file.
 *
 *   res-test app.apk
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "husk-tl-internal.h"
#include "husk-tl-res.h"

int main(int argc, char **argv)
{
    const char *apk = argc > 1 ? argv[1] : "/Users/davi/Documents/FlappyBird_64bit.apk";

    tl_zip z;
    char err[128] = {0};
    if (!tl_zip_open(&z, apk, err, sizeof(err))) { fprintf(stderr, "zip: %s\n", err); return 2; }

    const tl_zip_entry *arsc = tl_zip_find(&z, "resources.arsc");
    if (!arsc) { fprintf(stderr, "no resources.arsc\n"); return 2; }

    const uint8_t *data = NULL; size_t len = 0; bool owned = false;
    if (!tl_zip_data(&z, arsc, 64 * 1024 * 1024, &data, &len, &owned, err, sizeof(err))) {
        fprintf(stderr, "arsc: %s\n", err); return 2;
    }
    printf("resources.arsc: %zu bytes\n", len);

    tl_res *r = tl_res_create(data, len);
    if (!r) { fprintf(stderr, "not a resource table\n"); return 1; }
    printf("entry slots across all configurations: %zu\n\n", tl_res_count(r));

    /* The names the game was previously hard-coded for, and the ones it was not. */
    const char *probes[] = { "atlas", "pipe_green", "number_0", "bluebird-downflap",
                             "yellowbird_midflap", "redbird-upflap", "settingsbutton",
                             "background", "foreground", "app_name", "drawable/background",
                             "nope_not_a_resource", NULL };
    for (int i = 0; probes[i]; i++) {
        uint32_t id = tl_res_find(r, "drawable", probes[i]);
        const char *file = id ? tl_res_file(r, id, 480) : NULL;
        const char *str = id ? NULL : NULL;
        if (!id) {
            uint32_t sid = tl_res_find(r, "string", probes[i]);
            if (sid) { id = sid; str = tl_res_string(r, sid); }
        }
        printf("  %-22s id=0x%08x  file=%s%s%s\n", probes[i], id,
               file ? file : "-", str ? "  string=" : "", str ? str : "");
    }

    /* The round trip: every drawable bitmap in the APK, by name and back. */
    int total = 0, found = 0, same = 0, shown = 0;
    for (size_t i = 0; i < z.count; i++) {
        const char *path = z.entries[i].name;
        if (strncmp(path, "res/drawable", 12)) continue;
        size_t pl = strlen(path);
        if (pl < 5 || (strcmp(path + pl - 4, ".png") && strcmp(path + pl - 5, ".webp"))) continue;

        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        char name[256];
        strncpy(name, base, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
        char *dot = strrchr(name, '.'); if (dot) *dot = 0;
        if (strstr(name, ".9")) continue;     /* nine-patch: name keeps its own dot */

        total++;
        uint32_t id = tl_res_find(r, "drawable", name);
        if (!id) {
            if (shown++ < 8) printf("  NOT FOUND by name: %s\n", path);
            continue;
        }
        found++;
        const char *back = tl_res_file(r, id, 480);
        const char *bb = back ? strrchr(back, '/') : NULL;
        bb = bb ? bb + 1 : back;
        if (bb && !strcmp(bb, base)) same++;
        else if (shown++ < 8) printf("  ROUND TRIP differs: %s -> id 0x%08x -> %s\n", path, id, back ? back : "(null)");
    }
    printf("\ndrawable bitmaps in the APK: %d\nresolved by name:           %d\nround trip lands on the same file: %d\n",
           total, found, same);

    tl_res_destroy(r);
    if (owned) free((void *)data);
    tl_zip_close(&z);
    return (total > 0 && same == total) ? 0 : 1;
}
