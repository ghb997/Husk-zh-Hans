/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Drives Rockstar's Android port of GTA San Andreas (com.rockstargames.gtasa, 2.x) the way its Java wrapper does.
 *
 * The game is one native library (libGame.so) behind Rockstar's "oswrapper": GameNative declares what the library implements (implOnActivityCreated,
 * implOnInitialSetup, implOnSurfaceChanged, implOnDrawFrame, touches, controllers ...) and a thread of the wrapper's calls them in order, drawing a frame each time round
 * its loop. The library makes its own EGL context on the window, and calls back into one Java object, GamePlatformServices, for what only Android can answer: the
 * splash screen, HTTP, the intro movies, the Rockstar Social Club gate. This does the same from C, against the Java world husk-tl-jni-hle.c and this file provide.
 */
#ifndef HUSK_TL_GTASA_H
#define HUSK_TL_GTASA_H

#include <stdbool.h>

#include "husk-tl-gameactivity.h"          /* tl_ga_config: the same description of the game and its surface */

#ifdef __cplusplus
extern "C" {
#endif

/* Load the library and set up the Java side. Returns false (with the reason logged) on failure. */
bool tl_gta_start(const tl_ga_config *cfg);

/* The game thread: the activity's set-up calls, the surface, then a frame per turn of its loop. */
bool tl_gta_run(void);

/* A touch in surface pixels, y down. phase 0 down, 1 move, 2 up, 3 cancel. Safe from any thread. */
void tl_gta_touch(int phase, int id, float x, float y);

void tl_gta_set_paused(bool paused);
unsigned long tl_gta_frames(void);

#ifdef __cplusplus
}
#endif

#endif
