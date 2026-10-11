/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Drives a Godot game's engine (libgodot_android.so) the way Godot's Android activity does.
 *
 * A Godot export is the engine library and the game's data pack in the APK's assets. The Java side -- org.godotengine.godot
 * -- loads the library, hands it the activity and a few helpers (GodotIO for the device, file and directory access
 * handlers), calls setup() with the command line, and then a GLSurfaceView's thread calls newcontext(), resize() and,
 * every frame, step(). Input arrives as GodotLib calls on that thread. This does the same from C, with the helpers
 * answered in husk-tl-jni-godot.c.
 */
#ifndef HUSK_TL_GODOT_H
#define HUSK_TL_GODOT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tl_godot_config {
    const char *apk_path;
    const char *data_dir;        /* writable app data directory */
    const char *package_name;
    int width, height;           /* surface size in pixels */
    void *metal_layer;           /* CAMetalLayer to present into, or NULL (offscreen/host) */
    const char *angle_egl;       /* ANGLE's libEGL (or the single ANGLE dylib on the phone) */
    const char *angle_gles;      /* ANGLE's libGLESv2, or NULL */
    const char *frame_dir;       /* host tests: write frames here instead of presenting */
    int frame_every;
} tl_godot_config;

bool tl_godot_start(const tl_godot_config *cfg);
bool tl_godot_run(void);
unsigned long tl_godot_frames(void);
bool tl_godot_ended(void);
void tl_godot_stop(void);
void tl_godot_set_paused(bool paused);
void tl_godot_quit(void);

/* A touch in surface pixels, y down. phase 0 down, 1 move, 2 up, 3 cancel. Safe from any thread. */
void tl_godot_touch(int phase, int id, float x, float y);
/* An Android key code, pressed and released (4 is Back). */
void tl_godot_key(int keycode, bool down);

typedef struct tl_godot_perf { double fps, mean_ms, max_ms; } tl_godot_perf;
void tl_godot_perf_snapshot(tl_godot_perf *out);

/* The engine's major version, read from the library (3 or 4), once started. */
int tl_godot_major(void);

/* husk-tl-jni-godot.c */
void tl_godot_hle_install(const char *pkg, const char *apk, const char *data, int w, int h);

#ifdef __cplusplus
}
#endif

#endif
