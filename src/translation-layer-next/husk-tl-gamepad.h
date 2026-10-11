/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Game controllers, as Android shows them to a game: an InputDevice the game can ask about, KeyEvents for the
 * buttons and joystick MotionEvents for the sticks, triggers and D-pad.
 *
 * The app reads the controller (iOS's GameController, which also does the Bluetooth pairing) and reports its state here;
 * this works out what changed and tells the engine that is running. A controller is shown as an Xbox Wireless
 * Controller, the layout Android games are written against: A/B/X/Y, bumpers, stick clicks, Start/Select/Mode, the
 * left stick on X/Y, the right on Z/RZ, the triggers on LTRIGGER/RTRIGGER (and BRAKE/GAS) and the D-pad on the hat.
 */
#ifndef HUSK_TL_GAMEPAD_H
#define HUSK_TL_GAMEPAD_H

#include <stdbool.h>
#include <stdint.h>

#include "husk-tl-jni.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TL_PADS 4

/* The bits of tl_pad_state.buttons. */
enum { TL_PAD_A, TL_PAD_B, TL_PAD_X, TL_PAD_Y, TL_PAD_L1, TL_PAD_R1, TL_PAD_THUMBL, TL_PAD_THUMBR, TL_PAD_START, TL_PAD_SELECT, TL_PAD_MODE,
       TL_PAD_DPAD_UP, TL_PAD_DPAD_DOWN, TL_PAD_DPAD_LEFT, TL_PAD_DPAD_RIGHT, TL_PAD_BUTTONS };

typedef struct tl_pad_state {
    uint32_t buttons;                    /* 1 << TL_PAD_* */
    float lx, ly, rx, ry;                /* -1..1; y is positive downwards, as Android reports it */
    float lt, rt;                        /* 0..1 */
} tl_pad_state;

/* Where the events go: the engine that is running. Each gets its own reference to the event and releases it. */
typedef struct tl_pad_sink {
    void (*key)(jobj *ev, int device, int action, int keycode, int64_t down_ms, int64_t event_ms);
    void (*motion)(jobj *ev, int device, int source, int64_t down_ms, int64_t event_ms);
} tl_pad_sink;

void tl_pad_set_sink(const tl_pad_sink *sink);
void tl_pad_connect(int slot, const char *name);
void tl_pad_disconnect(int slot);
void tl_pad_update(int slot, const tl_pad_state *state);        /* any thread; unchanged state sends nothing */
bool tl_pad_connected(int slot);

/* the objects in husk-tl-jni-input.c */
jobj *tl_input_key_event(int action, int keycode, int device, int source, int repeat, int64_t down_ms, int64_t event_ms);
bool tl_input_event_axes(const jobj *ev, float *out48);        /* the axes of a joystick event, by Android axis number; false if it is not one */
jobj *tl_input_joystick_event(int device, int source, int64_t down_ms, int64_t event_ms, const float *axes);

#ifdef __cplusplus
}
#endif

#endif
