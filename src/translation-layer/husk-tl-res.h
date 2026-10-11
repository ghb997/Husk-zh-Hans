/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * An app's resource table, read from its own resources.arsc.
 *
 * Android never looks a resource up by file name. Code asks for an id -- either
 * a constant the compiler baked in (R.drawable.bird) or one it finds at run time
 * from a name (Resources.getIdentifier("bird", "drawable", pkg)) -- and the
 * table says what that id is for the device it is running on: which file, which
 * string, which screen density.
 *
 * This layer used to answer those questions from a short list of ids invented
 * to suit one game. Everything outside the list resolved to zero, so a game
 * that asked for any sprite not on it got nothing back, and the layer could run
 * exactly the app it had been tested against.
 *
 * Nothing here needs more than the file's own bytes, and every offset and
 * length is checked before it is followed: the input is a file somebody else's
 * tooling built, and a truncated or hostile table must produce "not found",
 * never a read off the end of the buffer.
 */
#ifndef HUSK_TL_RES_H
#define HUSK_TL_RES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tl_res tl_res;

/*
 * Parse a resources.arsc image. `data` must stay valid, unmodified, for as long
 * as the table does: strings handed back point into it. Returns NULL if the
 * buffer is not a resource table.
 */
tl_res *tl_res_create(const uint8_t *data, size_t len);
void    tl_res_destroy(tl_res *r);

/*
 * The id of the resource called `name` of kind `type` ("drawable", "string",
 * "layout", ...), or 0 if there is none. `type` may be NULL when `name` carries
 * its own kind, as in "drawable/bird".
 */
uint32_t tl_res_find(const tl_res *r, const char *type, const char *name);

/*
 * The file a resource id points at, as a path inside the APK
 * ("res/drawable/bird.png"), for a screen of `density_dpi`; NULL if the id is
 * not a file reference. The exact density wins, then the nearest higher one --
 * Android scales down rather than up -- then the highest below. The string
 * belongs to the table.
 */
const char *tl_res_file(const tl_res *r, uint32_t id, int density_dpi);

/* A plain string resource's value, preferring the default locale, or NULL. */
const char *tl_res_string(const tl_res *r, uint32_t id);

/* Entry slots across every configuration of every type. For diagnostics. */
size_t tl_res_count(const tl_res *r);

#ifdef __cplusplus
}
#endif

#endif /* HUSK_TL_RES_H */
