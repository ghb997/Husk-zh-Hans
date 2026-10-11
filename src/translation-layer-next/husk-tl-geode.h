/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Geode, the Geometry Dash mod loader, loaded into the game as its Android launcher does (husk-tl-geode.c). */
#ifndef HUSK_TL_GEODE_H
#define HUSK_TL_GEODE_H

#include <stdbool.h>
#include "husk-tl-jni.h"

/* Before the game starts: Geode's library (or the release zip it comes in), the launcher APK (for libc++_shared.so), the game's
 * data folder and its versionCode (0: read it from the game's manifest). */
void tl_geode_configure(const char *geode_so, const char *launcher_apk, const char *data_dir, int version_code);
bool tl_geode_configured(void);
/* After the game's libraries are loaded and set up, before its first frame. Does nothing when Geode is not configured. */
bool tl_geode_load(jobj *activity);

#endif
