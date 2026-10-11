/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Drives a game built on Google's GameActivity (the Android Game Development Kit) the way its Java shell does.
 *
 * Minecraft is one huge native library behind a thin Java activity. GameActivity.java loads the library, hands it the
 * activity, the data directories and the asset manager through initializeNativeCode, and from then on reports the
 * activity's life -- start, resume, a surface, focus, touches -- through native calls on that same object. The game
 * runs its own threads and makes its own EGL context on the surface it is given. This does the same from C, against
 * the Java world husk-tl-jni-hle.c and husk-tl-jni-minecraft.c provide.
 */
#ifndef HUSK_TL_GAMEACTIVITY_H
#define HUSK_TL_GAMEACTIVITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tl_ga_config {
    const char *apk_path;
    const char *data_dir;        /* writable app data directory */
    const char *package_name;    /* com.mojang.minecraftpe */
    int width, height;           /* surface size in pixels (a landscape game: width > height) */
    void *metal_layer;           /* CAMetalLayer to present into, or NULL (offscreen/host) */
    const char *angle_egl;       /* path to ANGLE's libEGL (or the single ANGLE dylib on the phone) */
    const char *angle_gles;      /* path to ANGLE's libGLESv2, or NULL */
    const char *frame_dir;       /* host tests: write frames here instead of presenting */
    int frame_every;
} tl_ga_config;

/* Load the libraries and run the activity's onCreate. Returns false (with the reason logged) on failure. */
bool tl_ga_start(const tl_ga_config *cfg);

/* onStart, onResume, the surface and focus: the game takes it from there on its own threads. */
bool tl_ga_run(void);

/* A touch in surface pixels, y down. phase 0 down, 1 move, 2 up, 3 cancel all. Safe from any thread. */
void tl_ga_touch(int phase, int id, float x, float y);

/* onPause/onResume (and the surface going away and coming back). Safe from any thread. */
void tl_ga_set_paused(bool paused);

/* Run `fn(arg)` on the activity's UI thread, as Activity.runOnUiThread does. */
void tl_ga_post(void (*fn)(void *), void *arg);

/* Whether the caller is the activity's UI thread. */
bool tl_ga_is_ui_thread(void);

/* Frames the game has presented. */
unsigned long tl_ga_frames(void);

/* The soft keyboard (GameTextInput): the game's field as it says it is, the keyboard shown or hidden, and what is typed. */
void tl_ga_set_keyboard_handler(void (*hook)(int action));      /* 1 = show, 2 = hide */
void tl_ga_text_state(const char *utf8, int sel_start, int sel_end);
void tl_ga_keyboard(bool show);
void tl_ga_ime_options(int ime_options);
void tl_ga_insert_text(const char *utf8);
void tl_ga_delete_backward(void);
void tl_ga_editor_action(void);
void tl_ga_text_copy(char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
