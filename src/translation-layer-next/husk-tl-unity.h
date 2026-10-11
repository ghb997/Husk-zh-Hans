/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Drives a Unity player's native libraries the way UnityPlayer.java does.
 *
 * Unity's engine is native code with a thin Java shell. The shell loads libmain.so,
 * hands the engine its Context, creates a surface, and then calls nativeRender() on a
 * thread of its own, forever. This does the same from C: it runs the sequence UnityPlayer
 * runs, on the same kinds of thread, against the Java world husk-tl-jni-hle.c provides.
 */
#ifndef HUSK_TL_UNITY_H
#define HUSK_TL_UNITY_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tl_unity_config {
    const char *apk_path;
    const char *data_dir;        /* writable app data directory */
    const char *package_name;    /* e.g. com.kiloo.subwaysurf */
    int width, height;           /* screen size in pixels */
    void *metal_layer;           /* CAMetalLayer to present into, or NULL (offscreen/host) */
    const char *angle_egl;       /* path to ANGLE's libEGL (or the single ANGLE dylib on the phone) */
    const char *angle_gles;      /* path to ANGLE's libGLESv2, or NULL */
    const char *frame_dir;       /* host tests: write frames here instead of presenting */
    int frame_every;
} tl_unity_config;

/* Load the engine and run its startup. Returns false (with the reason logged) on failure. */
bool tl_unity_start(const tl_unity_config *cfg);

/* Start the UnityMain thread and begin rendering frames. */
bool tl_unity_run(void);

/* Frames rendered so far. */
unsigned long tl_unity_frames(void);

/*
 * A touch, in screen pixels with y down. phase 0 begins it, 1 moves it, 2 ends it (3 cancels all).
 * `id` names the finger. Safe to call from any thread.
 */
/* Send a connected controller's buttons and sticks to this engine (husk-tl-gamepad.h). */
void tl_unity_register_pad_sink(void);
void tl_unity_touch(int phase, int id, float x, float y);

/* How the last stretch went: frames per second, and the mean and worst time inside one nativeRender call. Resets what it measured. */
typedef struct tl_unity_perf { double fps, mean_ms, max_ms; } tl_unity_perf;
void tl_unity_perf_snapshot(tl_unity_perf *out);

/* Pause the engine the way UnityPlayer.onPause does (and resume it). Safe from any thread. */
void tl_unity_set_paused(bool paused);

/* Send a signal to the UnityMain thread (diagnostics: a backtrace handler in the host). */
void tl_unity_poke(int signo);

/* Ask the engine to stop and wait for UnityMain to finish. */
void tl_unity_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* HUSK_TL_UNITY_H */
