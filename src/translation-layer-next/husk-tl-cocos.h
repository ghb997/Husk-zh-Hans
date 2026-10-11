/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Drives a cocos2d-x game's native libraries the way its Java shell does.
 *
 * Cocos2d-x games (Geometry Dash) are one native library behind a GLSurfaceView: the activity
 * loads the library, hands it the APK's path, and once the surface exists a GL thread calls
 * nativeInit(width, height) and then nativeRender() for every frame, with input arriving as
 * nativeTouches*() calls on that same thread. This does the same from C against the Java world
 * husk-tl-jni-hle.c and husk-tl-jni-cocos.c provide.
 */
#ifndef HUSK_TL_COCOS_H
#define HUSK_TL_COCOS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tl_cocos_config {
    const char *apk_path;
    const char *data_dir;        /* writable app data directory */
    const char *package_name;    /* e.g. com.robtopx.geometryjump */
    int width, height;           /* surface size in pixels; Geometry Dash is a landscape game, so width > height */
    void *metal_layer;           /* CAMetalLayer to present into, or NULL (offscreen/host) */
    const char *angle_egl;       /* path to ANGLE's libEGL (or the single ANGLE dylib on the phone) */
    const char *angle_gles;      /* path to ANGLE's libGLESv2, or NULL */
    const char *frame_dir;       /* host tests: write frames here instead of presenting */
    int frame_every;             /* with frame_dir: save every Nth frame; negative keeps only latest.bmp */
} tl_cocos_config;

/* Load the libraries and run the activity's startup. Returns false (with the reason logged) on failure. */
bool tl_cocos_start(const tl_cocos_config *cfg);

/* Start the GL thread: it makes the EGL context, calls nativeInit, then renders until stopped. */
bool tl_cocos_run(void);

unsigned long tl_cocos_frames(void);

/* A touch in surface pixels, y down. phase 0 down, 1 move, 2 up, 3 cancel all. Safe from any thread. */
/* Route a connected controller to this engine (husk-tl-gamepad.h). */
void tl_cocos_register_pad_sink(void);
void tl_cocos_touch(int phase, int id, float x, float y);

/* Text typed on the soft keyboard, a backspace, and a key (Android key codes: 4 is Back), delivered to the game on its GL thread. */
void tl_cocos_insert_text(const char *utf8);
void tl_cocos_delete_backward(void);
void tl_cocos_key_down(int keycode);
/* Ask what text the game's field holds (to seed the keyboard's own display); `cb` runs on the GL thread. */
void tl_cocos_request_content_text(void (*cb)(const char *utf8));

typedef struct tl_cocos_perf { double fps, mean_ms, max_ms; } tl_cocos_perf;
void tl_cocos_perf_snapshot(tl_cocos_perf *out);

/* Pause and resume rendering the way the activity's onPause/onResume do. Safe from any thread. */
void tl_cocos_set_paused(bool paused);

void tl_cocos_resume_sound(void);               /* JniToCpp.resumeSound; call on the GL thread */

/* The game's own animation interval, in seconds (Cocos2dxRenderer.setAnimationInterval): the pace of the render loop. */
void tl_cocos_set_interval(double seconds);

/* The game's own state after the GL thread died or the game asked to quit. */
bool tl_cocos_ended(void);

void tl_cocos_stop(void);

/* ------------------------------------------------------------ host hooks */

/* A label the game wants drawn (Cocos2dxBitmap.createTextBitmapShadowStroke). The rasteriser calls tl_cocos_deliver_bitmap. */
typedef struct tl_cocos_text_request {
    const char *text, *font;
    int size, align, width, height;
    float r, g, b;
    bool shadow, stroke;
    float shadow_dx, shadow_dy, shadow_blur, stroke_r, stroke_g, stroke_b, stroke_w;
} tl_cocos_text_request;
typedef void (*tl_cocos_text_fn)(const tl_cocos_text_request *req);
void tl_cocos_set_text_rasteriser(tl_cocos_text_fn fn);
void tl_cocos_text_install(void);                 /* the CoreText rasteriser (husk-tl-cocos-text.c) */
/* Hand the engine a label's pixels (premultiplied RGBA, top row first), as Cocos2dxBitmap.nativeInitBitmapDC does. */
void tl_cocos_deliver_bitmap(int width, int height, const uint8_t *rgba);

/* Audio from FMOD's output thread: interleaved 16-bit samples; the hook blocks until it has taken them. */
extern void (*tl_cocos_audio_hook)(const int16_t *samples, int frames, int channels, int rate);
/* The game's soft keyboard: the handler is told 1 = show it, 2 = hide it (action 0, toggle, is not used). */
extern void (*tl_cocos_keyboard_hook)(int action);
/* A URL the game wants opened. */
extern void (*tl_cocos_open_url_hook)(const char *url);

#ifdef __cplusplus
}
#endif

#endif /* HUSK_TL_COCOS_H */
