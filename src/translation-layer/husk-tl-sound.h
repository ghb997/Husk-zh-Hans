/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SoundPool's sounds, decoded and mixed to the speakers (husk-tl-sound.c). */
#ifndef HUSK_TL_SOUND_H
#define HUSK_TL_SOUND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Decode a sound (Ogg Vorbis) and keep it; returns its id, or 0. */
int  tl_sound_load(const uint8_t *data, size_t len);
/* Play a loaded sound: volumes 0..1, loops (-1 forever, 0 once, n more times), playback rate. Returns a stream id, or 0. */
int  tl_sound_play(int id, float left, float right, int loops, float rate);
void tl_sound_stop(int stream);
void tl_sound_stop_all(void);
void tl_sound_set_paused(bool paused);

#endif
