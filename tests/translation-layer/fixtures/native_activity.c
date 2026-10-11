/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The attempt's guest: a real NativeActivity, built for Android's ABI.
 *
 * Compiled with -target aarch64-linux-android -nostdlib, this is exactly
 * the shape of ELF the loader will meet in a real APK: Linux dynamic
 * tags, a GNU hash table, RELATIVE and JUMP_SLOT relocations against
 * bionic symbols. Every symbol it names is one the shim must supply --
 * log, window, pthread -- so the fixture exercises the whole surface
 * without an NDK.
 *
 * What it does: registers callbacks, spawns a render thread that draws a
 * moving gradient into the ANativeWindow it is handed (lock, write,
 * unlockAndPost), and logs the lifecycle. It draws until onDestroy.
 */

/* --- the ABI, restated so no host header is needed ------------------- */

typedef struct an_activity an_activity;
typedef struct an_window an_window;

typedef struct {
    void (*onStart)(an_activity *);
    void (*onResume)(an_activity *);
    void *(*onSaveInstanceState)(an_activity *, unsigned long *);
    void (*onPause)(an_activity *);
    void (*onStop)(an_activity *);
    void (*onDestroy)(an_activity *);
    void (*onWindowFocusChanged)(an_activity *, int);
    void (*onNativeWindowCreated)(an_activity *, an_window *);
    void (*onNativeWindowResized)(an_activity *, an_window *);
    void (*onInputQueueCreated)(an_activity *, void *);
    void (*onInputQueueDestroyed)(an_activity *, void *);
    void (*onContentRectChanged)(an_activity *, const int *);
    void (*onSurfaceChanged)(an_activity *, an_window *, int);
    void (*onSurfaceRedrawNeeded)(an_activity *, an_window *);
    void (*onSurfaceDestroyed)(an_activity *);
    void (*onNativeWindowDestroyed)(an_activity *, an_window *);
} an_callbacks;

struct an_activity {
    an_callbacks *callbacks;
    void *vm;
    void *env;
    void *clazz;
    const char *internalDataPath;
    const char *externalDataPath;
    int sdkVersion;
    void *instance;
    void *assetManager;
    const char *obbPath;
};

/* --- the imports, resolved by the loader against the shim ------------ */

extern int __android_log_print(int prio, const char *tag, const char *fmt, ...);
extern int32_t ANativeWindow_getWidth(an_window *);
extern int32_t ANativeWindow_getHeight(an_window *);
extern int32_t ANativeWindow_lock(an_window *, void *, void *);
extern int32_t ANativeWindow_unlockAndPost(an_window *);
extern int pthread_create(void **, const void *, void *(*)(void *), void *);
extern int pthread_detach(void *);
extern void *memcpy(void *, const void *, unsigned long);
extern void usleep(unsigned int);

#define LOG(...) __android_log_print(4, "husk-guest", __VA_ARGS__)

/* --- the guest's own state ------------------------------------------- */

static volatile int running;
static volatile an_window *win;
static unsigned long frame_no;

static void draw_frame(void)
{
    an_window *w = (an_window *)win;
    if (!w) {
        return;
    }
    struct { void *bits; int width, height, stride, format; } buf;
    if (ANativeWindow_lock(w, &buf, 0) != 0) {
        return;
    }
    int wpx = buf.stride, hpx = buf.height;
    unsigned char *px = buf.bits;
    /* A moving diagonal band over a dark field. Cheap, but it moves,
     * which is the point: two consecutive frames must differ. */
    unsigned band = (frame_no * 4) % (wpx ? wpx : 1);
    for (int y = 0; y < hpx; y++) {
        unsigned char *row = px + (unsigned long)y * buf.stride * 4;
        for (int x = 0; x < wpx; x++) {
            unsigned d = (unsigned)(x + y + band) % 256;
            row[x * 4 + 0] = (unsigned char)(d / 3 + 20);
            row[x * 4 + 1] = (unsigned char)(d / 2 + 10);
            row[x * 4 + 2] = (unsigned char)(30 + d / 4);
            row[x * 4 + 3] = 255;
        }
    }
    ANativeWindow_unlockAndPost(w);
    frame_no++;
}

static void *render_thread(void *arg)
{
    (void)arg;
    LOG("render thread up");
    while (running) {
        draw_frame();
        usleep(100000);          /* ~10 fps: slow enough to watch */
    }
    LOG("render thread done");
    return 0;
}

/* --- the lifecycle ---------------------------------------------------- */

static void onStart(an_activity *a)   { (void)a; LOG("onStart"); }
static void onResume(an_activity *a)  { (void)a; LOG("onResume"); }
static void onPause(an_activity *a)   { (void)a; LOG("onPause"); }
static void onStop(an_activity *a)    { (void)a; LOG("onStop"); }

static void onDestroy(an_activity *a)
{
    (void)a;
    LOG("onDestroy");
    running = 0;
}

static void onNativeWindowCreated(an_activity *a, an_window *w)
{
    (void)a;
    LOG("onNativeWindowCreated %dx%d", ANativeWindow_getWidth(w),
        ANativeWindow_getHeight(w));
    win = w;
}

static void onNativeWindowDestroyed(an_activity *a, an_window *w)
{
    (void)a; (void)w;
    LOG("onNativeWindowDestroyed");
    win = 0;
}

static void onSurfaceChanged(an_activity *a, an_window *w, int format)
{
    (void)a; (void)format;
    LOG("onSurfaceChanged %dx%d", ANativeWindow_getWidth(w),
        ANativeWindow_getHeight(w));
    win = w;
}

static an_callbacks cbs = {
    onStart, onResume, 0, onPause, onStop, onDestroy, 0,
    onNativeWindowCreated, 0, 0, 0, 0, onSurfaceChanged, 0, 0,
    onNativeWindowDestroyed,
};

/* --- the entry the loader looks for ----------------------------------- */

__attribute__((visibility("default")))
void ANativeActivity_onCreate(an_activity *activity, void *savedState,
                              unsigned long savedStateSize)
{
    (void)savedState; (void)savedStateSize;
    LOG("onCreate: guest is alive, sdk %d, data %s", activity->sdkVersion,
        activity->internalDataPath);
    activity->callbacks = &cbs;
    running = 1;
    void *t = 0;
    if (pthread_create(&t, 0, render_thread, 0) == 0) {
        pthread_detach(t);
    } else {
        LOG("render thread did not start; drawing inline once");
        draw_frame();
    }
}
