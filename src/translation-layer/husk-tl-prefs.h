/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SharedPreferences: a typed key/value store.
 *
 * This replaces a stand-in that answered every getInt with the high score,
 * whatever the key and whatever default the app passed. Apps keep their
 * settings here and read each one with a default they chose -- "game speed,
 * default 1", "score multiplier, default 1", "moving pipes start at 2000" -- and
 * a store that ignores the key and the default hands every integer setting back
 * as zero. Flappy Bird's score multiplier read as zero, so no score ever
 * counted, and its pipe-movement thresholds read as zero, so the pipes moved
 * from the first frame.
 *
 * The rule a getter follows is Android's: the stored value if there is one, the
 * caller's default if there is not. A value stored as a different type counts as
 * absent; Android throws there, and the default is the nearest harmless thing.
 */
#ifndef HUSK_TL_PREFS_H
#define HUSK_TL_PREFS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tl_prefs tl_prefs;

tl_prefs *tl_prefs_create(void);
void tl_prefs_destroy(tl_prefs *p);

/*
 * Keep this store in a file: load it now if the file exists, and write it back
 * on every tl_prefs_save(). Without a path the store lives in memory only, which
 * is what a test wants. Returns false if the file exists and cannot be read.
 */
bool tl_prefs_attach(tl_prefs *p, const char *path);

/* Write to the attached file, atomically. A no-op returning true with no path. */
bool tl_prefs_save(const tl_prefs *p);

bool    tl_prefs_contains(const tl_prefs *p, const char *key);
int32_t tl_prefs_get_int(const tl_prefs *p, const char *key, int32_t def);
int64_t tl_prefs_get_long(const tl_prefs *p, const char *key, int64_t def);
float   tl_prefs_get_float(const tl_prefs *p, const char *key, float def);
bool    tl_prefs_get_bool(const tl_prefs *p, const char *key, bool def);
/* The stored string, or `def`. Owned by the store; valid until that key changes. */
const char *tl_prefs_get_string(const tl_prefs *p, const char *key, const char *def);

void tl_prefs_put_int(tl_prefs *p, const char *key, int32_t v);
void tl_prefs_put_long(tl_prefs *p, const char *key, int64_t v);
void tl_prefs_put_float(tl_prefs *p, const char *key, float v);
void tl_prefs_put_bool(tl_prefs *p, const char *key, bool v);
void tl_prefs_put_string(tl_prefs *p, const char *key, const char *v);   /* copied; NULL removes */

void tl_prefs_remove(tl_prefs *p, const char *key);
void tl_prefs_clear(tl_prefs *p);

int tl_prefs_count(const tl_prefs *p);

#ifdef __cplusplus
}
#endif

#endif /* HUSK_TL_PREFS_H */
