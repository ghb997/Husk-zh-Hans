/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Sound for the games Husk runs on its own Java interpreter (Flappy Bird): android.media.SoundPool, played for real.
 *
 * A game loads its short sounds once (res/raw/wing.ogg and the like) and plays them by id whenever something happens.
 * Each is decoded here when it is loaded -- Ogg Vorbis through stb_vorbis, since iOS has no Vorbis decoder of its own --
 * and kept as 16-bit PCM. Playing starts a voice; one mixer thread adds every playing voice together, a block at a time,
 * and hands the block to the same speaker output the native runtime's games use (tl_cocos_audio_hook), which blocks while
 * the speakers are full and so paces the mixer.
 */
#include "husk-tl-sound.h"

#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.inc"
#pragma clang diagnostic pop

void tl_log_line(const char *fmt, ...);
extern void (*tl_cocos_audio_hook)(const int16_t *samples, int frames, int channels, int rate);
void tl_audio_install(void);

#define MAX_SOUNDS 128
#define MAX_VOICES 24
#define RATE 44100
#define BLOCK 512

typedef struct { int16_t *pcm; int frames, channels, rate; } sound;
typedef struct { bool on; int sound, stream, loops; double pos, step; float left, right; } voice;

static struct {
    pthread_mutex_t lock;
    pthread_t thread;
    bool running, paused;
    sound sounds[MAX_SOUNDS];
    int nsounds;
    voice voices[MAX_VOICES];
    int next_stream;
} S = { .lock = PTHREAD_MUTEX_INITIALIZER, .next_stream = 1 };

int tl_sound_load(const uint8_t *data, size_t len)
{
    int channels = 0, rate = 0;
    short *pcm = NULL;
    int frames = stb_vorbis_decode_memory(data, (int)len, &channels, &rate, &pcm);
    if (frames <= 0 || !pcm || channels < 1 || channels > 2) {
        free(pcm);
        tl_log_line("sound: could not decode a %zu-byte sound (only Ogg Vorbis is read)", len);
        return 0;
    }
    pthread_mutex_lock(&S.lock);
    int id = 0;
    if (S.nsounds < MAX_SOUNDS) {
        S.sounds[S.nsounds] = (sound){ pcm, frames, channels, rate };
        id = ++S.nsounds;
    } else free(pcm);
    pthread_mutex_unlock(&S.lock);
    if (id) tl_log_line("sound: loaded sound %d, %d frames, %d Hz, %d channel(s)", id, frames, rate, channels);
    return id;
}

static void *mixer_main(void *arg)
{
    (void)arg;
    pthread_setname_np("SoundPool");
    static int16_t out[BLOCK * 2];
    float mix[BLOCK * 2];
    while (1) {
        memset(mix, 0, sizeof(mix));
        bool any = false;
        pthread_mutex_lock(&S.lock);
        if (!S.paused) {
            for (int v = 0; v < MAX_VOICES; v++) {
                voice *vo = &S.voices[v];
                if (!vo->on) continue;
                const sound *so = &S.sounds[vo->sound - 1];
                any = true;
                for (int f = 0; f < BLOCK; f++) {
                    int at = (int)vo->pos;
                    if (at >= so->frames) {
                        if (vo->loops != 0) { if (vo->loops > 0) vo->loops--; vo->pos -= so->frames; at = (int)vo->pos; }
                        else { vo->on = false; break; }
                    }
                    float l = so->pcm[(size_t)at * so->channels] / 32768.0f;
                    float r = so->channels == 2 ? so->pcm[(size_t)at * 2 + 1] / 32768.0f : l;
                    mix[f * 2] += l * vo->left;
                    mix[f * 2 + 1] += r * vo->right;
                    vo->pos += vo->step;
                }
            }
        }
        pthread_mutex_unlock(&S.lock);
        for (int i = 0; i < BLOCK * 2; i++) {
            float s = mix[i];
            if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
            out[i] = (int16_t)lrintf(s * 32767.0f);
        }
        /* Silence still goes out while nothing plays: the output stays open and a new sound starts at once. */
        if (tl_cocos_audio_hook) tl_cocos_audio_hook(out, BLOCK, 2, RATE);
        else usleep((useconds_t)(1e6 * BLOCK / RATE));
        (void)any;
    }
    return NULL;
}

int tl_sound_play(int id, float left, float right, int loops, float rate)
{
    pthread_mutex_lock(&S.lock);
    if (id < 1 || id > S.nsounds) { pthread_mutex_unlock(&S.lock); return 0; }
    if (!S.running) {
        if (!tl_cocos_audio_hook) tl_audio_install();
        S.running = pthread_create(&S.thread, NULL, mixer_main, NULL) == 0;
        if (S.running) pthread_detach(S.thread);
    }
    int slot = -1;
    for (int v = 0; v < MAX_VOICES; v++) if (!S.voices[v].on) { slot = v; break; }
    if (slot < 0) slot = 0;                                       /* every voice busy: the oldest slot gives way */
    int stream = S.next_stream++;
    const sound *so = &S.sounds[id - 1];
    if (rate <= 0) rate = 1.0f;
    S.voices[slot] = (voice){ true, id, stream, loops, 0.0, (double)so->rate / RATE * rate,
                              left < 0 ? 0 : left > 1 ? 1 : left, right < 0 ? 0 : right > 1 ? 1 : right };
    pthread_mutex_unlock(&S.lock);
    static int said;
    if (said++ < 3) tl_log_line("sound: playing sound %d (stream %d, volume %.2f/%.2f, rate %.2f)", id, stream, left, right, rate);
    return stream;
}

void tl_sound_stop(int stream)
{
    pthread_mutex_lock(&S.lock);
    for (int v = 0; v < MAX_VOICES; v++) if (S.voices[v].on && S.voices[v].stream == stream) S.voices[v].on = false;
    pthread_mutex_unlock(&S.lock);
}

void tl_sound_set_paused(bool paused)
{
    pthread_mutex_lock(&S.lock);
    S.paused = paused;
    pthread_mutex_unlock(&S.lock);
}

void tl_sound_stop_all(void)
{
    pthread_mutex_lock(&S.lock);
    for (int v = 0; v < MAX_VOICES; v++) S.voices[v].on = false;
    pthread_mutex_unlock(&S.lock);
}
