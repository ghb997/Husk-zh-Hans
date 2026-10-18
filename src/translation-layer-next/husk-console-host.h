/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Running a console game through a console module (libhuskps4.dylib for PS4): what the app calls.
 *
 * The host loads the module, gives it Husk's JIT region through the arena (husk-jit-arena.h),
 * Husk's MoltenVK and a log sink, and starts the game on the module's own thread. Starting is
 * asynchronous; progress is in the log and in husk_console_state. One game per process: a module
 * cannot be unloaded.
 */
#ifndef HUSK_CONSOLE_HOST_H
#define HUSK_CONSOLE_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "husk-console-abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct husk_console_launch_args {
    const char *module_path;  /* .../Frameworks/libhuskps4.dylib */
    const char *game_id;      /* "ps4:CUSA07023": what husk_native_loaded_apk reports while it runs */
    const char *game;         /* eboot.bin, or the folder holding it */
    const char *game_args;    /* extra launch arguments joined by '\n', or NULL */
    const char *user_dir;     /* the module's settings, saves, system modules, caches */
    const char *log_path;     /* the game's native-run.log; NULL logs to stderr only */
    const char *vulkan_path;  /* MoltenVK to load directly, or NULL */
    void *metal_layer;        /* CAMetalLayer */
    int width_px, height_px;
    float scale;              /* pixels per point */
    size_t region_bytes;      /* the JIT region to make on a Mac (the phone uses StikDebug's) */
} husk_console_launch_args;

/* Returns false with a reason in err when the game could not be started. */
bool husk_console_launch(const husk_console_launch_args *args, char *err, size_t errlen);

int husk_console_state(void);       /* enum HuskConsoleState; HUSK_CONSOLE_IDLE before launch */
uint64_t husk_console_frames(void);
void husk_console_set_paused(bool paused);
void husk_console_set_pad(const HuskPadV1 *pad);
void husk_console_resize(int width_px, int height_px);
void husk_console_stop(void);
/* After the game asked to start another executable: its path and args, for the next launch. */
bool husk_console_take_pending_restart(char *path, int path_size, char *args, int args_size);
/* The game_id of the console game started in this process, or NULL. */
/* A game asked for text (its keyboard dialog): show a keyboard with title and initial and call done(ctx, text) once, on any
 * thread, with the UTF-8 text or NULL when the player cancelled. Return 0 when the request was taken. */
typedef int (*husk_console_text_requester)(const char *title, const char *initial, uint32_t max_chars,
                                           void (*done)(void *ctx, const char *text), void *ctx);
void husk_console_set_text_requester(husk_console_text_requester fn);
const char *husk_console_loaded_game(void);

/* Extracts a package file (a PS4 .pkg) into dest_dir through the module at module_path, on the caller's thread; no game needs
 * to be running. false with a message in err when the module refuses it (a retail package) or the extraction failed. */
bool husk_console_install_package(const char *module_path, const char *package, const char *dest_dir,
                                  void (*progress)(void *ctx, uint64_t done, uint64_t total), void *ctx, char *err, size_t errlen);

#ifdef __cplusplus
}
#endif

#endif
