/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The Java side of FMOD on Android, implemented in C. FMOD (the audio engine in Geometry Dash and Minecraft) has no
 * Android audio API it can reach here, so it falls back to its Java output: an AudioTrack that its mixer thread
 * writes into. The write blocks for as long as the audio takes to play, which is what paces the mixer; the samples go
 * to the host's audio output when the app has installed one (tl_cocos_audio_hook), and are dropped otherwise.
 */
#define _DARWIN_C_SOURCE
#include "husk-tl-jni.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "husk-tl-bionic.h"

void (*tl_cocos_audio_hook)(const int16_t *samples, int frames, int channels, int rate);

static jvalue vl(void *p) { jvalue v; v.j = 0; v.l = p; return v; }
static jvalue vi(int i) { jvalue v; v.j = 0; v.i = i; return v; }
static jvalue vz(int z) { jvalue v; v.j = 0; v.z = z != 0; return v; }
static void Noop(tl_jcall *c) { (void)c; }
static void RetTrue(tl_jcall *c) { c->ret = vz(1); }
static void RetFalse(tl_jcall *c) { c->ret = vz(0); }

static void FMOD_sampleRate(tl_jcall *c) { c->ret = vi(48000); }
static void FMOD_blockSize(tl_jcall *c) { c->ret = vi(1024); }
static void FMOD_fd(tl_jcall *c) { c->ret = vi(-1); }
static void FMOD_assets(tl_jcall *c) { c->ret = vl(tl_jni_new_object(tl_jni_class("android/content/res/AssetManager"))); }
static void FMOD_audioDevices(tl_jcall *c) { c->ret = vl(tl_jni_new_obj_array(tl_jni_class("android/media/AudioDeviceInfo"), 0)); }

static void Audio_init(tl_jcall *c)
{
    /* init(int channels, int sampleRate, int bufferLength, int bufferCount) */
    tl_jni_set_field(c->self, "channels", "I", vi(c->args[0].i));
    tl_jni_set_field(c->self, "rate", "I", vi(c->args[1].i));
    tl_log_line("fmod: audio output opened: %d channel(s) at %d Hz, buffer %d x %d", c->args[0].i, c->args[1].i, c->args[2].i, c->args[3].i);
    c->ret = vz(1);
}

static void Audio_write(tl_jcall *c)
{
    jobj *arr = c->args[0].l;
    int n = c->args[1].i;                                    /* samples (not frames) */
    int ch = tl_jni_get_field(c->self, "channels", "I").i, rate = tl_jni_get_field(c->self, "rate", "I").i;
    if (ch <= 0) ch = 2;
    if (rate <= 0) rate = 48000;
    if (!arr || arr->kind != TL_K_PRIM_ARRAY || n <= 0) return;
    if ((uint32_t)n > arr->arr.len) n = (int)arr->arr.len;
    if (tl_cocos_audio_hook) {
        tl_cocos_audio_hook((const int16_t *)arr->arr.data, n / ch, ch, rate);     /* the hook blocks until it has room */
    } else {
        struct timespec ts = { 0, (long)((double)(n / ch) * 1e9 / rate) };
        nanosleep(&ts, NULL);
    }
}

static const struct { const char *name, *super; } k_classes[] = {
    { "org/fmod/FMOD", "java/lang/Object" }, { "org/fmod/AudioDevice", "java/lang/Object" }, { "org/fmod/MediaCodec", "java/lang/Object" },
    { "android/media/AudioDeviceInfo", "java/lang/Object" },
};

#define M(c, n, s, f) { c, n, s, f }
static const tl_jhle k_hle[] = {
    M("org/fmod/FMOD", "checkInit", "()Z", RetTrue),
    M("org/fmod/FMOD", "getOutputSampleRate", "()I", FMOD_sampleRate), M("org/fmod/FMOD", "getOutputBlockSize", "()I", FMOD_blockSize),
    M("org/fmod/FMOD", "supportsLowLatency", "()Z", RetFalse), M("org/fmod/FMOD", "supportsAAudio", "()Z", RetFalse),
    M("org/fmod/FMOD", "lowLatencyFlag", "()Z", RetFalse), M("org/fmod/FMOD", "proAudioFlag", "()Z", RetFalse),
    M("org/fmod/FMOD", "isBluetoothOn", "()Z", RetFalse), M("org/fmod/FMOD", "fileDescriptorFromUri", "(Ljava/lang/String;)I", FMOD_fd),
    M("org/fmod/FMOD", "getAssetManager", "()Landroid/content/res/AssetManager;", FMOD_assets),
    M("org/fmod/FMOD", "getAudioDevices", "(I)[Landroid/media/AudioDeviceInfo;", FMOD_audioDevices),
    M("org/fmod/AudioDevice", "<init>", "()V", Noop), M("org/fmod/AudioDevice", "init", "(IIII)Z", Audio_init),
    M("org/fmod/AudioDevice", "write", "([SI)V", Audio_write), M("org/fmod/AudioDevice", "close", "()V", Noop),
    M("org/fmod/MediaCodec", "<init>", "()V", Noop), M("org/fmod/MediaCodec", "init", "(J)Z", RetFalse),
    { NULL, NULL, NULL, NULL }
};

void tl_fmod_install(void)
{
    for (size_t i = 0; i < sizeof(k_classes) / sizeof(k_classes[0]); i++) tl_jni_declare(k_classes[i].name, k_classes[i].super);
    tl_jni_register_hle(k_hle);
}
