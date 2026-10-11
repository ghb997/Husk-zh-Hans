/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The Java side of a cocos2d-x game (Geometry Dash), implemented in C.
 *
 * Cocos2d-x's engine is native code with a thin Java shell: Cocos2dxActivity creates a GLSurfaceView
 * and a renderer, and the native library reaches back into a handful of static helpers for what
 * Android alone can answer -- the writable path, saved preferences, text rasterised by a Java
 * Canvas -- and into the game's own activity for ads, sign-in and the like. The shell itself is
 * husk-tl-cocos.c; this is the helpers. What the game asks and no one here answers logs once as
 * UNIMPLEMENTED (husk-tl-jni.c) and returns zero.
 */
#define _DARWIN_C_SOURCE
#include "husk-tl-jni.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "husk-tl-bionic.h"
#include "husk-tl-cocos.h"

void tl_fmod_install(void);

static struct {
    char pkg[128], apk[1024], data[512], files[600], ext[700], prefs[700];
    int width, height;
} G;

static jvalue vl(void *p) { jvalue v; v.j = 0; v.l = p; return v; }
static jvalue vi(int i) { jvalue v; v.j = 0; v.i = i; return v; }
static jvalue vz(int z) { jvalue v; v.j = 0; v.z = z != 0; return v; }
static jvalue vf(float f) { jvalue v; v.j = 0; v.f = f; return v; }
static jvalue vd(double d) { jvalue v; v.j = 0; v.d = d; return v; }
#define STR(s) tl_jni_new_string(s)
static const char *S(const jobj *o) { const char *s = tl_jni_string(o); return s ? s : ""; }

static void Noop(tl_jcall *c) { (void)c; }

/* Hooks the app may install: a link the game wants opened, and the soft keyboard. */
void (*tl_cocos_open_url_hook)(const char *url);
void (*tl_cocos_keyboard_hook)(int action);
static void RetTrue(tl_jcall *c) { c->ret = vz(1); }
static void RetFalse(tl_jcall *c) { c->ret = vz(0); }

/* ------------------------------------------------------------ preferences */

/*
 * Cocos2dxHelper keeps the game's CCUserDefault values in a SharedPreferences file. They live here
 * as text, in memory, and are written back to one file on every change: the set is small.
 */
typedef struct { char *key, *val; } pref;
static pref *g_prefs;
static int g_nprefs, g_capprefs;
static pthread_mutex_t g_prefs_mu = PTHREAD_MUTEX_INITIALIZER;

static pref *pref_find(const char *key)
{
    for (int i = 0; i < g_nprefs; i++) if (!strcmp(g_prefs[i].key, key)) return &g_prefs[i];
    return NULL;
}

static void esc_write(FILE *f, const char *s)
{
    for (; *s; s++) {
        if (*s == '\\') fputs("\\\\", f); else if (*s == '\n') fputs("\\n", f); else if (*s == '\t') fputs("\\t", f);
        else if (*s == '\r') fputs("\\r", f); else fputc(*s, f);
    }
}

static void prefs_save(void)
{
    if (!G.prefs[0]) return;
    char tmp[720]; snprintf(tmp, sizeof(tmp), "%s.tmp", G.prefs);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    for (int i = 0; i < g_nprefs; i++) { esc_write(f, g_prefs[i].key); fputc('\t', f); esc_write(f, g_prefs[i].val); fputc('\n', f); }
    fclose(f);
    rename(tmp, G.prefs);
}

static char *unesc(const char *s, size_t n)
{
    char *o = malloc(n + 1); size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) { i++; o[k++] = s[i] == 'n' ? '\n' : s[i] == 't' ? '\t' : s[i] == 'r' ? '\r' : s[i]; }
        else o[k++] = s[i];
    }
    o[k] = 0;
    return o;
}

static void prefs_put(const char *key, const char *val)
{
    pthread_mutex_lock(&g_prefs_mu);
    pref *p = pref_find(key);
    if (p) { free(p->val); p->val = strdup(val); }
    else {
        if (g_nprefs == g_capprefs) { g_capprefs = g_capprefs ? g_capprefs * 2 : 64; g_prefs = realloc(g_prefs, (size_t)g_capprefs * sizeof(pref)); }
        g_prefs[g_nprefs].key = strdup(key); g_prefs[g_nprefs].val = strdup(val); g_nprefs++;
    }
    prefs_save();
    pthread_mutex_unlock(&g_prefs_mu);
}

static void prefs_load(void)
{
    FILE *f = fopen(G.prefs, "r");
    if (!f) return;
    char *line = NULL; size_t cap = 0; ssize_t n;
    while ((n = getline(&line, &cap, f)) > 0) {
        if (line[n - 1] == '\n') line[--n] = 0;
        char *tab = memchr(line, '\t', (size_t)n);
        if (!tab) continue;
        char *k = unesc(line, (size_t)(tab - line)), *v = unesc(tab + 1, (size_t)(n - (tab - line) - 1));
        if (g_nprefs == g_capprefs) { g_capprefs = g_capprefs ? g_capprefs * 2 : 64; g_prefs = realloc(g_prefs, (size_t)g_capprefs * sizeof(pref)); }
        g_prefs[g_nprefs].key = k; g_prefs[g_nprefs].val = v; g_nprefs++;
    }
    free(line); fclose(f);
}

static const char *pref_get(const char *key)
{
    pthread_mutex_lock(&g_prefs_mu);
    pref *p = pref_find(key);
    const char *v = p ? p->val : NULL;
    pthread_mutex_unlock(&g_prefs_mu);
    return v;
}

static void Helper_getBool(tl_jcall *c) { const char *v = pref_get(S(c->args[0].l)); c->ret = vz(v ? !strcmp(v, "true") : c->args[1].z); }
static void Helper_getInt(tl_jcall *c) { const char *v = pref_get(S(c->args[0].l)); c->ret = vi(v ? atoi(v) : c->args[1].i); }
static void Helper_getFloat(tl_jcall *c) { const char *v = pref_get(S(c->args[0].l)); c->ret = vf(v ? (float)atof(v) : c->args[1].f); }
static void Helper_getDouble(tl_jcall *c) { const char *v = pref_get(S(c->args[0].l)); c->ret = vd(v ? atof(v) : c->args[1].d); }
static void Helper_getString(tl_jcall *c) { const char *v = pref_get(S(c->args[0].l)); c->ret = vl(v ? STR(v) : (c->args[1].l ? tl_jni_ref(c->args[1].l) : NULL)); }
static void Helper_setBool(tl_jcall *c) { prefs_put(S(c->args[0].l), c->args[1].z ? "true" : "false"); }
static void Helper_setInt(tl_jcall *c) { char t[24]; snprintf(t, sizeof(t), "%d", c->args[1].i); prefs_put(S(c->args[0].l), t); }
static void Helper_setFloat(tl_jcall *c) { char t[40]; snprintf(t, sizeof(t), "%.9g", (double)c->args[1].f); prefs_put(S(c->args[0].l), t); }
static void Helper_setDouble(tl_jcall *c) { char t[40]; snprintf(t, sizeof(t), "%.17g", c->args[1].d); prefs_put(S(c->args[0].l), t); }
static void Helper_setString(tl_jcall *c) { prefs_put(S(c->args[0].l), S(c->args[1].l)); }

/* ---------------------------------------------------------- Cocos2dxHelper */

static void Helper_packageName(tl_jcall *c) { c->ret = vl(STR(G.pkg)); }
static void Helper_writablePath(tl_jcall *c) { c->ret = vl(STR(G.files)); }
static void Helper_language(tl_jcall *c) { c->ret = vl(STR("en")); }
static void Helper_dpi(tl_jcall *c) { c->ret = vi(420); }
static void Helper_terminate(tl_jcall *c)
{
    (void)c;
    tl_log_line("cocos: the game asked to terminate the process");
    if (tl_guest_exit_hook) tl_guest_exit_hook(0);
}
static void Helper_showEditText(tl_jcall *c)
{
    tl_log_line("cocos: the game wants a text box (title \"%s\", text \"%s\"): not available", S(c->args[0].l), S(c->args[1].l));
}
static void Renderer_setInterval(tl_jcall *c) { tl_cocos_set_interval(c->args[0].d); }
static void Helper_showDialog(tl_jcall *c) { tl_log_line("cocos: dialog \"%s\": %s", S(c->args[0].l), S(c->args[1].l)); }

/* -------------------------------------------------------------- text bitmaps */

/*
 * Cocos2dxBitmap draws a label with a Java Canvas and hands the pixels back through
 * nativeInitBitmapDC. A rasteriser for the host belongs to the app; until one is installed a
 * label simply has no texture, and the game carries on.
 */
static tl_cocos_text_fn g_text_fn;
void tl_cocos_set_text_rasteriser(tl_cocos_text_fn fn) { g_text_fn = fn; }

static void Bitmap_createText(tl_jcall *c)
{
    /* (String text, String font, int size, float r, float g, float b, int align, int w, int h,
     *  boolean shadow, float dx, float dy, float blur, boolean stroke, float sr, float sg, float sb, float sw) */
    static int once;
    if (!g_text_fn) {
        if (!once++) tl_log_line("cocos: text \"%s\" in font \"%s\" at %d: no text rasteriser installed, label left blank", S(c->args[0].l), S(c->args[1].l), c->args[2].i);
        return;
    }
    tl_cocos_text_request r = {
        .text = S(c->args[0].l), .font = S(c->args[1].l), .size = c->args[2].i,
        .r = c->args[3].f, .g = c->args[4].f, .b = c->args[5].f, .align = c->args[6].i,
        .width = c->args[7].i, .height = c->args[8].i,
        .shadow = c->args[9].z, .shadow_dx = c->args[10].f, .shadow_dy = c->args[11].f, .shadow_blur = c->args[12].f,
        .stroke = c->args[13].z, .stroke_r = c->args[14].f, .stroke_g = c->args[15].f, .stroke_b = c->args[16].f, .stroke_w = c->args[17].f,
    };
    g_text_fn(&r);
}

/* ----------------------------------------------------- the game's activity */

static void Act_userId(tl_jcall *c) { c->ret = vl(STR("husk0000000000000")); }
static void Act_refreshRate(tl_jcall *c) { c->ret = vf(60.f); }
static void Act_openURL(tl_jcall *c) { tl_log_line("cocos: openURL %s", S(c->args[0].l)); if (tl_cocos_open_url_hook) tl_cocos_open_url_hook(S(c->args[0].l)); }
static void Act_loadingFinished(tl_jcall *c) { (void)c; tl_log_line("cocos: the game finished loading"); }
static void Act_tryRate(tl_jcall *c) { (void)c; }
/* BaseRobTopActivity keeps the keyboard's wanted state in a flag; onToggleKeyboard() makes the keyboard match it. */
static volatile int g_keyboard_active;
static void Act_toggleKeyboard(tl_jcall *c)
{
    (void)c;
    tl_log_line("cocos: keyboard: sync to %s", g_keyboard_active ? "shown" : "hidden");
    if (tl_cocos_keyboard_hook) tl_cocos_keyboard_hook(g_keyboard_active ? 1 : 2);
}
static void Act_setKeyboardState(tl_jcall *c) { g_keyboard_active = c->args[0].z; tl_log_line("cocos: keyboard: state %d", c->args[0].z); }
static void GL_openIME(tl_jcall *c) { (void)c; tl_log_line("cocos: keyboard: open"); if (tl_cocos_keyboard_hook) tl_cocos_keyboard_hook(1); }
static void GL_closeIME(tl_jcall *c) { (void)c; tl_log_line("cocos: keyboard: close"); if (tl_cocos_keyboard_hook) tl_cocos_keyboard_hook(2); }

/* ------------------------------------------------------------------- tables */

static const struct { const char *name, *super; } k_classes[] = {
    { "org/cocos2dx/lib/Cocos2dxHelper", "java/lang/Object" }, { "org/cocos2dx/lib/Cocos2dxBitmap", "java/lang/Object" },
    { "org/cocos2dx/lib/Cocos2dxRenderer", "java/lang/Object" }, { "org/cocos2dx/lib/Cocos2dxETCLoader", "java/lang/Object" },
    { "org/cocos2dx/lib/Cocos2dxLocalStorage", "java/lang/Object" }, { "org/cocos2dx/lib/Cocos2dxGLSurfaceView", "java/lang/Object" },
    { "com/customRobTop/DefaultRobTopActivity", "android/app/Activity" }, { "com/customRobTop/BaseRobTopActivity", "com/customRobTop/DefaultRobTopActivity" },
    { "org/cocos2dx/lib/Cocos2dxActivity", "com/customRobTop/BaseRobTopActivity" },
    { "com/robtopx/geometryjump/GeometryJump", "org/cocos2dx/lib/Cocos2dxActivity" },
    { "com/customRobTop/JniToCpp", "java/lang/Object" }, { "com/customRobTop/SimpleCrypto", "java/lang/Object" },
};

#define HELPER "org/cocos2dx/lib/Cocos2dxHelper"
#define ROBTOP "com/customRobTop/BaseRobTopActivity"
#define M(c, n, s, f) { c, n, s, f }
static const tl_jhle k_hle[] = {
    M("org/cocos2dx/lib/Cocos2dxRenderer", "setAnimationInterval", "(D)V", Renderer_setInterval),
    M(HELPER, "getCocos2dxPackageName", "()Ljava/lang/String;", Helper_packageName),
    M(HELPER, "getCocos2dxWritablePath", "()Ljava/lang/String;", Helper_writablePath),
    M(HELPER, "getCurrentLanguage", "()Ljava/lang/String;", Helper_language),
    M(HELPER, "getDPI", "()I", Helper_dpi),
    M(HELPER, "terminateProcess", "()V", Helper_terminate),
    M(HELPER, "enableAccelerometer", "()V", Noop), M(HELPER, "disableAccelerometer", "()V", Noop),
    M(HELPER, "setAccelerometerInterval", "(F)V", Noop),
    M(HELPER, "showEditTextDialog", "(Ljava/lang/String;Ljava/lang/String;IIII)V", Helper_showEditText),
    M(HELPER, "showDialog", "(Ljava/lang/String;Ljava/lang/String;)V", Helper_showDialog),
    M(HELPER, "getBoolForKey", "(Ljava/lang/String;Z)Z", Helper_getBool),
    M(HELPER, "getIntegerForKey", "(Ljava/lang/String;I)I", Helper_getInt),
    M(HELPER, "getFloatForKey", "(Ljava/lang/String;F)F", Helper_getFloat),
    M(HELPER, "getDoubleForKey", "(Ljava/lang/String;D)D", Helper_getDouble),
    M(HELPER, "getStringForKey", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", Helper_getString),
    M(HELPER, "setBoolForKey", "(Ljava/lang/String;Z)V", Helper_setBool),
    M(HELPER, "setIntegerForKey", "(Ljava/lang/String;I)V", Helper_setInt),
    M(HELPER, "setFloatForKey", "(Ljava/lang/String;F)V", Helper_setFloat),
    M(HELPER, "setDoubleForKey", "(Ljava/lang/String;D)V", Helper_setDouble),
    M(HELPER, "setStringForKey", "(Ljava/lang/String;Ljava/lang/String;)V", Helper_setString),
    M("org/cocos2dx/lib/Cocos2dxBitmap", "createTextBitmapShadowStroke", "(Ljava/lang/String;Ljava/lang/String;IFFFIIIZFFFZFFFF)V", Bitmap_createText),
    M("org/cocos2dx/lib/Cocos2dxETCLoader", "loadTexture", "(Ljava/lang/String;)Z", RetFalse),
    M("org/cocos2dx/lib/Cocos2dxLocalStorage", "init", "(Ljava/lang/String;Ljava/lang/String;)Z", RetTrue),
    M("org/cocos2dx/lib/Cocos2dxGLSurfaceView", "openIMEKeyboard", "()V", GL_openIME), M("org/cocos2dx/lib/Cocos2dxGLSurfaceView", "closeIMEKeyboard", "()V", GL_closeIME),

    M(ROBTOP, "getUserID", "()Ljava/lang/String;", Act_userId),
    M(ROBTOP, "getDeviceRefreshRate", "()F", Act_refreshRate),
    M(ROBTOP, "isNetworkAvailable", "()Z", RetTrue),
    M(ROBTOP, "gameServicesIsSignedIn", "()Z", RetFalse),
    M(ROBTOP, "gameServicesSignIn", "()V", Noop), M(ROBTOP, "gameServicesSignOut", "()V", Noop),
    M(ROBTOP, "hasCachedInterstitial", "()Z", RetFalse), M(ROBTOP, "hasCachedRewardedVideo", "()Z", RetFalse),
    M(ROBTOP, "cacheInterstitial", "()V", Noop), M(ROBTOP, "cacheRewardedVideo", "()V", Noop),
    M(ROBTOP, "showInterstitial", "()V", Noop), M(ROBTOP, "showRewardedVideo", "()V", Noop), M(ROBTOP, "showAdDebug", "()V", Noop),
    M(ROBTOP, "enableBanner", "()V", Noop), M(ROBTOP, "enableBannerNoRefresh", "()V", Noop), M(ROBTOP, "disableBanner", "()V", Noop),
    M(ROBTOP, "queueRefreshBanner", "()V", Noop), M(ROBTOP, "pauseAds", "()V", Noop), M(ROBTOP, "resumeAds", "()V", Noop),
    M(ROBTOP, "loadingFinished", "()V", Act_loadingFinished),
    M(ROBTOP, "onToggleKeyboard", "()V", Act_toggleKeyboard), M(ROBTOP, "setKeyboardState", "(Z)V", Act_setKeyboardState), M(ROBTOP, "setBlockBackButton", "(Z)V", Noop),
    M(ROBTOP, "shouldResumeSound", "()Z", RetFalse),
    M(ROBTOP, "openURL", "(Ljava/lang/String;)V", Act_openURL), M(ROBTOP, "openAppPage", "()V", Noop),
    M(ROBTOP, "sendMail", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V", Noop),
    M(ROBTOP, "tryShowRateDialog", "(Ljava/lang/String;)V", Act_tryRate),
    M(ROBTOP, "showAchievements", "()V", Noop), M(ROBTOP, "unlockAchievement", "(Ljava/lang/String;)V", Noop),
    M(ROBTOP, "showLeaderboards", "()V", Noop), M(ROBTOP, "updateTopScoreLeaderboard", "(I)V", Noop),
    M(ROBTOP, "setupEveryplay", "()V", Noop), M(ROBTOP, "logEvent", "(Ljava/lang/String;)V", Noop),

    { NULL, NULL, NULL, NULL }
};

void tl_cocos_hle_install(const char *pkg, const char *apk, const char *data, int w, int h)
{
    snprintf(G.pkg, sizeof(G.pkg), "%s", pkg);
    snprintf(G.apk, sizeof(G.apk), "%s", apk);
    snprintf(G.data, sizeof(G.data), "%s", data);
    snprintf(G.files, sizeof(G.files), "%s/files", data);
    snprintf(G.ext, sizeof(G.ext), "%s/sdcard/Android/data/%s/files", data, pkg);
    G.width = w; G.height = h;
    char dir[640]; snprintf(dir, sizeof(dir), "%s/shared_prefs", data);
    mkdir(dir, 0755);
    snprintf(G.prefs, sizeof(G.prefs), "%s/Cocos2dxPrefsFile.txt", dir);
    prefs_load();
    for (size_t i = 0; i < sizeof(k_classes) / sizeof(k_classes[0]); i++) tl_jni_declare(k_classes[i].name, k_classes[i].super);
    tl_jni_register_hle(k_hle);
    tl_fmod_install();
}
