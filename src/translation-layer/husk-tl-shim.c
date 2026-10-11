/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The first bionic shim: the symbol surface an APK's arm64 code binds
 * against, implemented over Darwin.
 *
 * What is here is honest: __android_log_print into the attempt's log, the
 * libc passthroughs where the two ABIs agree, a software ANativeWindow the
 * guest draws RGBX into and Swift reads back, the JNI tables a
 * NativeActivity receives, and a mutex built inside bionic's 40-byte
 * pthread_mutex_t so a guest that embeds the type still works. EGL is
 * named but not shimmed: the first attempt renders through the software
 * window, and eglGetProcAddress hands guests a resolver that answers from
 * this table so a guest that hard-requires EGL symbols at relocation time
 * at least links.
 *
 * Everything the guest binds against is in one table; husk_tl_attempt_*
 * entry points live in husk-tl-load.c.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "husk-tl-internal.h"

/* ---- forward decls of the loader side, kept in one place ----------- */

void tl_log_line(const char *fmt, ...);          /* husk-tl-load.c */
void *tl_shim_find(const char *name);            /* this file, further down */
void tl_loader_frame_posted(void);               /* husk-tl-load.c */
void tl_loader_request_stop(void);               /* husk-tl-load.c */

/* The frames the guest draws, read from Swift between begin/end. */
void husk_tl_frame_begin_read(void);
void husk_tl_frame_end_read(void);
const uint8_t *husk_tl_frame_pixels(void);
int husk_tl_frame_width(void);
int husk_tl_frame_height(void);
int husk_tl_frame_stride(void);

/* ------------------------------------------- the guest-facing types -- */

/* tl_window lives in husk-tl-internal.h: the loader shares it. */

struct tl_input_queue {
    pthread_mutex_t lock;
};

struct tl_asset_manager {
    int unused;
};

/* AInputEvent type codes, from the NDK. */
#define AINPUT_EVENT_TYPE_MOTION 2
#define AMOTION_EVENT_ACTION_DOWN 0
#define AMOTION_EVENT_ACTION_UP   1
#define AMOTION_EVENT_ACTION_MOVE 2

/* ------------------------------------------------ the JNI fake VM ---- */

/*
 * A NativeActivity receives JNIEnv*, jobject and JavaVM* and is entitled
 * to call into them. Java cannot run here (ART is milestone 2), so the
 * tables answer the calls made before anything draws -- GetVersion,
 * GetEnv, AttachCurrentThread -- and the rest are stubs that log and
 * fail. The table must be the full size and order of JNI's
 * NativeInterface because the guest indexes it with compiled-in offsets;
 * the macro list keeps the order and the assert keeps the size.
 */

typedef struct tl_jni_env_ { const void *functions; } tl_jni_env;
typedef struct tl_jni_vm_  { const void *functions; } tl_jni_vm;

#define JNI_ENV_ENTRIES(X) \
    X(0,  reserved0)   X(1,  reserved1)   X(2,  reserved2)   X(3,  reserved3) \
    X(4,  GetVersion)  X(5,  DefineClass) X(6,  FindClass)   X(7,  FromReflectedMethod) \
    X(8,  FromReflectedField) X(9,  ToReflectedMethod) X(10, GetSuperclass) \
    X(11, IsAssignableFrom) X(12, ToReflectedField) X(13, Throw) X(14, ThrowNew) \
    X(15, ExceptionOccurred) X(16, ExceptionDescribe) X(17, ExceptionClear) \
    X(18, FatalError) X(19, PushLocalFrame) X(20, PopLocalFrame) \
    X(21, NewGlobalRef) X(22, DeleteGlobalRef) X(23, DeleteLocalRef) \
    X(24, IsSameObject) X(25, NewLocalRef) X(26, EnsureLocalCapacity) \
    X(27, AllocObject) X(28, NewObject) X(29, NewObjectV) X(30, NewObjectA) \
    X(31, GetObjectClass) X(32, IsInstanceOf) X(33, GetMethodID) \
    X(34, CallObjectMethod) X(35, CallObjectMethodV) X(36, CallObjectMethodA) \
    X(37, CallBooleanMethod) X(38, CallBooleanMethodV) X(39, CallBooleanMethodA) \
    X(40, CallByteMethod) X(41, CallByteMethodV) X(42, CallByteMethodA) \
    X(43, CallCharMethod) X(44, CallCharMethodV) X(45, CallCharMethodA) \
    X(46, CallShortMethod) X(47, CallShortMethodV) X(48, CallShortMethodA) \
    X(49, CallIntMethod) X(50, CallIntMethodV) X(51, CallIntMethodA) \
    X(52, CallLongMethod) X(53, CallLongMethodV) X(54, CallLongMethodA) \
    X(55, CallFloatMethod) X(56, CallFloatMethodV) X(57, CallFloatMethodA) \
    X(58, CallDoubleMethod) X(59, CallDoubleMethodV) X(60, CallDoubleMethodA) \
    X(61, CallVoidMethod) X(62, CallVoidMethodV) X(63, CallVoidMethodA) \
    X(64, CallNonvirtualObjectMethod) X(65, CallNonvirtualObjectMethodV) \
    X(66, CallNonvirtualObjectMethodA) X(67, CallNonvirtualBooleanMethod) \
    X(68, CallNonvirtualBooleanMethodV) X(69, CallNonvirtualBooleanMethodA) \
    X(70, CallNonvirtualByteMethod) X(71, CallNonvirtualByteMethodV) \
    X(72, CallNonvirtualByteMethodA) X(73, CallNonvirtualCharMethod) \
    X(74, CallNonvirtualCharMethodV) X(75, CallNonvirtualCharMethodA) \
    X(76, CallNonvirtualShortMethod) X(77, CallNonvirtualShortMethodV) \
    X(78, CallNonvirtualShortMethodA) X(79, CallNonvirtualIntMethod) \
    X(80, CallNonvirtualIntMethodV) X(81, CallNonvirtualIntMethodA) \
    X(82, CallNonvirtualLongMethod) X(83, CallNonvirtualLongMethodV) \
    X(84, CallNonvirtualLongMethodA) X(85, CallNonvirtualFloatMethod) \
    X(86, CallNonvirtualFloatMethodV) X(87, CallNonvirtualFloatMethodA) \
    X(88, CallNonvirtualDoubleMethod) X(89, CallNonvirtualDoubleMethodV) \
    X(90, CallNonvirtualDoubleMethodA) X(91, CallNonvirtualVoidMethod) \
    X(92, CallNonvirtualVoidMethodV) X(93, CallNonvirtualVoidMethodA) \
    X(94, GetFieldID) X(95, GetObjectField) X(96, GetBooleanField) \
    X(97, GetByteField) X(98, GetCharField) X(99, GetShortField) \
    X(100, GetIntField) X(101, GetLongField) X(102, GetFloatField) \
    X(103, GetDoubleField) X(104, SetObjectField) X(105, SetBooleanField) \
    X(106, SetByteField) X(107, SetCharField) X(108, SetShortField) \
    X(109, SetIntField) X(110, SetLongField) X(111, SetFloatField) \
    X(112, SetDoubleField) X(113, GetStaticMethodID) \
    X(114, CallStaticObjectMethod) X(115, CallStaticObjectMethodV) \
    X(116, CallStaticObjectMethodA) X(117, CallStaticBooleanMethod) \
    X(118, CallStaticBooleanMethodV) X(119, CallStaticBooleanMethodA) \
    X(120, CallStaticByteMethod) X(121, CallStaticByteMethodV) \
    X(122, CallStaticByteMethodA) X(123, CallStaticCharMethod) \
    X(124, CallStaticCharMethodV) X(125, CallStaticCharMethodA) \
    X(126, CallStaticShortMethod) X(127, CallStaticShortMethodV) \
    X(128, CallStaticShortMethodA) X(129, CallStaticIntMethod) \
    X(130, CallStaticIntMethodV) X(131, CallStaticIntMethodA) \
    X(132, CallStaticLongMethod) X(133, CallStaticLongMethodV) \
    X(134, CallStaticLongMethodA) X(135, CallStaticFloatMethod) \
    X(136, CallStaticFloatMethodV) X(137, CallStaticFloatMethodA) \
    X(138, CallStaticDoubleMethod) X(139, CallStaticDoubleMethodV) \
    X(140, CallStaticDoubleMethodA) X(141, CallStaticVoidMethod) \
    X(142, CallStaticVoidMethodV) X(143, CallStaticVoidMethodA) \
    X(144, GetStaticFieldID) X(145, GetStaticObjectField) \
    X(146, GetStaticBooleanField) X(147, GetStaticByteField) \
    X(148, GetStaticCharField) X(149, GetStaticShortField) \
    X(150, GetStaticIntField) X(151, GetStaticLongField) \
    X(152, GetStaticFloatField) X(153, GetStaticDoubleField) \
    X(154, SetStaticObjectField) X(155, SetStaticBooleanField) \
    X(156, SetStaticByteField) X(157, SetStaticCharField) \
    X(158, SetStaticShortField) X(159, SetStaticIntField) \
    X(160, SetStaticLongField) X(161, SetStaticFloatField) \
    X(162, SetStaticDoubleField) X(163, NewString) X(164, GetStringLength) \
    X(165, GetStringChars) X(166, ReleaseStringChars) X(167, NewStringUTF) \
    X(168, GetStringUTFLength) X(169, GetStringUTFChars) \
    X(170, ReleaseStringUTFChars) X(171, GetArrayLength) \
    X(172, NewObjectArray) X(173, GetObjectArrayElement) \
    X(174, SetObjectArrayElement) X(175, NewBooleanArray) \
    X(176, NewByteArray) X(177, NewCharArray) X(178, NewShortArray) \
    X(179, NewIntArray) X(180, NewLongArray) X(181, NewFloatArray) \
    X(182, NewDoubleArray) X(183, GetBooleanArrayElements) \
    X(184, GetByteArrayElements) X(185, GetCharArrayElements) \
    X(186, GetShortArrayElements) X(187, GetIntArrayElements) \
    X(188, GetLongArrayElements) X(189, GetFloatArrayElements) \
    X(190, GetDoubleArrayElements) X(191, ReleaseBooleanArrayElements) \
    X(192, ReleaseByteArrayElements) X(193, ReleaseCharArrayElements) \
    X(194, ReleaseShortArrayElements) X(195, ReleaseIntArrayElements) \
    X(196, ReleaseLongArrayElements) X(197, ReleaseFloatArrayElements) \
    X(198, ReleaseDoubleArrayElements) X(199, GetBooleanArrayRegion) \
    X(200, GetByteArrayRegion) X(201, GetCharArrayRegion) \
    X(202, GetShortArrayRegion) X(203, GetIntArrayRegion) \
    X(204, GetLongArrayRegion) X(205, GetFloatArrayRegion) \
    X(206, GetDoubleArrayRegion) X(207, SetBooleanArrayRegion) \
    X(208, SetByteArrayRegion) X(209, SetCharArrayRegion) \
    X(210, SetShortArrayRegion) X(211, SetIntArrayRegion) \
    X(212, SetLongArrayRegion) X(213, SetFloatArrayRegion) \
    X(214, SetDoubleArrayRegion) X(215, RegisterNatives) \
    X(216, UnregisterNatives) X(217, MonitorEnter) X(218, MonitorExit) \
    X(219, GetJavaVM) X(220, GetStringRegion) X(221, GetStringUTFRegion) \
    X(222, GetPrimitiveArrayCritical) X(223, ReleasePrimitiveArrayCritical) \
    X(224, GetStringCritical) X(225, ReleaseStringCritical) \
    X(226, NewWeakGlobalRef) X(227, DeleteWeakGlobalRef) \
    X(228, ExceptionCheck) X(229, NewDirectByteBuffer) \
    X(230, GetDirectBufferAddress) X(231, GetDirectBufferCapacity) \
    X(232, GetObjectRefType)

/* The real ones. Forward declared where they refer to state below. */
static int32_t jni_GetVersion_impl(void) { return 0x00090000; }
static tl_jni_vm g_vm;
static int32_t jni_GetJavaVM_impl(void *env, void **vm)
{
    (void)env;
    if (vm) *vm = &g_vm;
    return 0;
}
static int32_t jni_MonitorEnter_impl(void *env, void *obj) { (void)env; (void)obj; return 0; }
static int32_t jni_MonitorExit_impl(void *env, void *obj)  { (void)env; (void)obj; return 0; }
static void *jni_NewDirectByteBuffer_impl(void *env, void *addr, int64_t cap)
{
    (void)env;
    /* A plain struct with the address and the capacity, in place of the
     * object: code that only reads its address and capacity gets truth. */
    struct { void *addr; int64_t cap; } *buf = malloc(sizeof(*buf) + (size_t)(cap > 0 ? cap : 0));
    if (!buf) return NULL;
    buf->addr = addr;
    buf->cap = cap;
    return buf;
}
static void *jni_GetDirectBufferAddress_impl(void *env, void *buf)
{
    (void)env;
    struct { void *addr; int64_t cap; } *b = buf;
    return b ? b->addr : NULL;
}
static int64_t jni_GetDirectBufferCapacity_impl(void *env, void *buf)
{
    (void)env;
    struct { void *addr; int64_t cap; } *b = buf;
    return b ? b->cap : 0;
}

/* Generic stub: log the entry's name and return a failure the caller can
 * see (0/false/NULL are all "no" in JNI's conventions). */
static int jni_stub_log(const char *name)
{
    tl_log_line("jni: %s called -- Java is not running (ART is milestone 2); returning failure", name);
    return 0;
}

#define JNI_SLOT(n, name) \
    static void *jni_slot_##name(void *a0, void *a1, void *a2, void *a3) { \
        (void)a0; (void)a1; (void)a2; (void)a3; \
        jni_stub_log(#name); \
        return 0; \
    }

/* Build the 233 slots: the real ones where implemented, stubs elsewhere. */
static const void *g_env_table[233];

/* Every unfinished slot points at its own thunk, generated by macro, so
 * a guest that reaches one learns exactly which entry it hit. */
#define THUNK(n, name) \
    static void *jni_thunk_##n(void *a0, void *a1, void *a2, void *a3, void *a4, void *a5) { \
        (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; \
        jni_stub_log(#name); \
        (void)a0; \
        return 0; \
    }

JNI_ENV_ENTRIES(THUNK)

static void jni_env_table_fill(void)
{
    void **t = (void **)g_env_table;
#define FILL(n, name) t[n] = (void *)jni_thunk_##n;
    JNI_ENV_ENTRIES(FILL)
#undef FILL
    t[4]   = (void *)jni_GetVersion_impl;
    t[217] = (void *)jni_MonitorEnter_impl;
    t[218] = (void *)jni_MonitorExit_impl;
    t[219] = (void *)jni_GetJavaVM_impl;
    t[229] = (void *)jni_NewDirectByteBuffer_impl;
    t[230] = (void *)jni_GetDirectBufferAddress_impl;
    t[231] = (void *)jni_GetDirectBufferCapacity_impl;
}

_Static_assert(sizeof(g_env_table) / sizeof(g_env_table[0]) == 233,
               "the JNI table must match NativeInterface's entry count");

static tl_jni_env g_env_main = { g_env_table };

/* NativeInterface's mirror, for the VM side. */
typedef struct {
    void *reserved0, *reserved1, *reserved2;
    int32_t (*DestroyJavaVM)(void *);
    int32_t (*AttachCurrentThread)(void *, void **, void *);
    int32_t (*DetachCurrentThread)(void *);
    int32_t (*GetEnv)(void *, void **, uint32_t);
    int32_t (*AttachCurrentThreadAsDaemon)(void *, void **, void *);
} tl_vm_vtable;

static int32_t vm_DestroyJavaVM(void *vm) { (void)vm; return 0; }
static int32_t vm_AttachCurrentThread(void *vm, void **env, void *args)
{
    (void)vm; (void)args;
    if (env) *env = &g_env_main;
    return 0;
}
static int32_t vm_DetachCurrentThread(void *vm) { (void)vm; return 0; }
static int32_t vm_GetEnv(void *vm, void **penv, uint32_t version)
{
    (void)vm;
    if (penv) *penv = &g_env_main;
    return (version <= 0x00090000) ? 0 : -0x100;
}
static int32_t vm_AttachCurrentThreadAsDaemon(void *vm, void **env, void *args)
{
    return vm_AttachCurrentThread(vm, env, args);
}

static const tl_vm_vtable g_vm_vtable = {
    NULL, NULL, NULL,
    vm_DestroyJavaVM,
    vm_AttachCurrentThread,
    vm_DetachCurrentThread,
    vm_GetEnv,
    vm_AttachCurrentThreadAsDaemon,
};

static void jni_init(void)
{
    static bool once;
    if (!once) {
        jni_env_table_fill();
        g_vm.functions = &g_vm_vtable;
        once = true;
    }
}

void *tl_shim_env(void)  { jni_init(); return &g_env_main; }
void *tl_shim_vm(void)   { jni_init(); return &g_vm; }
void *tl_shim_activity_class(void) { return (void *)0x10; }   /* non-null stand-in */
int32_t JNI_GetDefaultJavaVMInitArgs(void *args) { (void)args; return -1; }
int32_t JNI_CreateJavaVM(void **vm, void **env, void *args)
{
    (void)vm; (void)env; (void)args;
    tl_log_line("jni: JNI_CreateJavaVM -- Java cannot start (ART is milestone 2)");
    return -1;
}
int32_t JNI_GetCreatedJavaVMs(void **vms, int max, int *count)
{
    jni_init();
    if (count) *count = 0;
    if (max > 0 && vms) vms[0] = NULL;
    return 0;
}

/* ------------------------------------------------- window and frames -- */

tl_window *tl_window_create(int width, int height)
{
    tl_window *w = calloc(1, sizeof(*w));
    if (!w) {
        return NULL;
    }
    w->width = width;
    w->height = height;
    w->format = 1;               /* WINDOW_FORMAT_RGBA_8888 */
    w->stridePixels = width;
    w->bits = calloc(1, (size_t)width * height * 4);
    pthread_mutex_init(&w->lock, NULL);
    atomic_init(&w->refcount, 1);
    return w;
}

static void tl_window_free(tl_window *w)
{
    pthread_mutex_destroy(&w->lock);
    free(w->bits);
    free(w);
}

void tl_window_acquire(tl_window *w) { if (w) atomic_fetch_add(&w->refcount, 1); }
void tl_window_release(tl_window *w)
{
    if (w && atomic_fetch_sub(&w->refcount, 1) == 1) {
        tl_window_free(w);
    }
}

/* The lock the loader holds while the guest draws. */
void tl_window_lock(tl_window *w)   { if (w) pthread_mutex_lock(&w->lock); }
void tl_window_unlock(tl_window *w) { if (w) pthread_mutex_unlock(&w->lock); }
uint8_t *tl_window_bits(tl_window *w) { return w ? w->bits : NULL; }

int32_t ANativeWindow_getWidth(void *window)  { return window ? ((tl_window *)window)->width : 0; }
int32_t ANativeWindow_getHeight(void *window) { return window ? ((tl_window *)window)->height : 0; }
int32_t ANativeWindow_getFormat(void *window) { return window ? ((tl_window *)window)->format : 0; }

/*
 * ANativeWindow_lock: the NDK's ANativeWindow_Buffer is
 *   { void* bits; int32_t width, height, stride, format; ... }
 * so the guest reads fields[0] as the pixel pointer and fields[3] as the
 * row stride in pixels. Both readings of the historical layout (stride
 * second) get truth: fields[1] is width, and a guest written against the
 * old order reads width as its stride -- width == stride here, so that
 * is still correct.
 */
int32_t ANativeWindow_lock(void *window, void *outBuffer, void *rect)
{
    (void)rect;
    if (!window || !outBuffer) {
        return -1;
    }
    tl_window *w = window;
    pthread_mutex_lock(&w->lock);
    void **fields = outBuffer;
    fields[0] = w->bits;
    int32_t *nums = (int32_t *)(fields + 1);
    nums[0] = w->width;
    nums[1] = w->height;
    nums[2] = w->stridePixels;
    nums[3] = w->format;
    return 0;
}

int32_t ANativeWindow_unlockAndPost(void *window)
{
    if (!window) {
        return -1;
    }
    pthread_mutex_unlock(&((tl_window *)window)->lock);
    tl_loader_frame_posted();        /* the loader counts the frame */
    return 0;
}

void ANativeWindow_acquire(void *window) { tl_window_acquire(window); }
void ANativeWindow_release(void *window) { tl_window_release(window); }

int32_t ANativeWindow_setBuffersGeometry(void *window, int32_t width,
                                         int32_t height, int32_t format)
{
    (void)window; (void)width; (void)height; (void)format;
    /* The attempt's window does not resize; claiming success keeps a
     * guest that calls it before drawing alive, and the size it asked
     * for is what it already had. */
    return 0;
}

void ANativeWindow_setFrameRate(void *window, float fps, int mode)
{
    (void)window; (void)fps; (void)mode;
}

/* EGL: named, not implemented. eglGetProcAddress answers from this table
 * so a guest that resolves every call through it (the common pattern)
 * gets pointers; the EGL calls themselves fail cleanly. */
void *eglGetProcAddress(const char *name)
{
    return tl_shim_find(name);
}

unsigned eglGetError(void) { return 0x3005; }   /* EGL_NOT_INITIALIZED */

/* --------------------------------------------------------- activity -- */

void ANativeActivity_finish(void *activity)
{
    (void)activity;
    tl_log_line("activity: ANativeActivity_finish -- stopping the attempt");
    tl_loader_request_stop();
}

void ANativeActivity_setWindowFlags(void *a, unsigned flags, unsigned mask)
{
    (void)a; (void)flags; (void)mask;
}

void ANativeActivity_showSoftInput(void *a, unsigned flags) { (void)a; (void)flags; }
void ANativeActivity_hideSoftInput(void *a, unsigned flags) { (void)a; (void)flags; }

/* ---------------------------------------------------------- input  -- */

int32_t AInputQueue_preDispatchEvent(void *q, void *e) { (void)q; (void)e; return 0; }
void AInputQueue_finishEvent(void *q, void *e, int handled) { (void)q; (void)e; (void)handled; }
void AInputQueue_attach(void *q) { (void)q; }
void AInputQueue_detach(void *q) { (void)q; }

/* The loader pushes touch events as AInputEvent-shaped memory; the guest
 * reads type, action and pointers from the offsets below. */
struct tl_input_event {
    int32_t type;          /* AINPUT_EVENT_TYPE_MOTION */
    int32_t action;        /* DOWN / UP / MOVE */
    int64_t when;
    float  x, y;
    int32_t pointer_count;
};

int32_t AInputEvent_getType(const void *event)
{
    return event ? ((const struct tl_input_event *)event)->type : 0;
}
int32_t AMotionEvent_getAction(const void *event)
{
    return event ? ((const struct tl_input_event *)event)->action : 0;
}
int32_t AMotionEvent_getPointerCount(const void *event)
{
    return event ? ((const struct tl_input_event *)event)->pointer_count : 1;
}
float AMotionEvent_getX(const void *event, size_t idx)
{
    (void)idx;
    return event ? ((const struct tl_input_event *)event)->x : 0.f;
}
float AMotionEvent_getY(const void *event, size_t idx)
{
    (void)idx;
    return event ? ((const struct tl_input_event *)event)->y : 0.f;
}
int64_t AMotionEvent_getEventTime(const void *event)
{
    return event ? ((const struct tl_input_event *)event)->when : 0;
}

/* --------------------------------------------------------- assets  -- */

void *AAssetManager_open(void *mgr, const char *name, int mode)
{
    (void)mgr; (void)mode;
    tl_log_line("asset: %s requested -- AAsset is not wired in the attempt", name);
    return NULL;
}
void AAsset_close(void *a) { (void)a; }
int AAsset_read(void *a, void *buf, size_t n) { (void)a; (void)buf; (void)n; return 0; }
int64_t AAsset_getLength(void *a) { (void)a; return 0; }
const void *AAsset_getBuffer(void *a) { (void)a; return NULL; }

/* --------------------------------------------------------- libc  ---- */

/*
 * pthread_mutex_t on bionic arm64 is 40 bytes; Darwin's is far larger.
 * Guests embed the type in their own structs, so the shim must fit IN 40
 * bytes, not hand out the host's. The attempt builds a spinlock inside
 * it: byte 0 is a lock byte, bytes 4..7 a recursion-free owner marker.
 * It cannot implement recursive or error-check mutexes; those guests
 * fail visibly rather than corrupt quietly.
 */
int pthread_mutex_init_shim(void *m, const void *attr)
{
    (void)attr;
    if (!m) return EINVAL;
    memset(m, 0, 40);
    ((uint8_t *)m)[0] = 0;       /* unlocked */
    return 0;
}

static void spin_lock_40(uint8_t *b)
{
    while (__sync_bool_compare_and_swap(b, 0, 1) == false) {
        sched_yield();
    }
}

static void spin_unlock_40(uint8_t *b) { *b = 0; }

int pthread_mutex_lock_shim(void *m)   { if (m) spin_lock_40(m); return 0; }
int pthread_mutex_trylock_shim(void *m)
{
    if (!m) return EINVAL;
    return __sync_bool_compare_and_swap((uint8_t *)m, 0, 1) ? 0 : EBUSY;
}
int pthread_mutex_unlock_shim(void *m) { if (m) spin_unlock_40(m); return 0; }
int pthread_mutex_destroy_shim(void *m) { if (m) memset(m, 0, 40); return 0; }

/* pthread_once_t is 4 bytes on bionic, 8 on Darwin (long). Same shape of
 * problem, same answer: 0 is "never run", 1 is "done", and the compare-
 * and-swap makes it safe enough for guest code. */
int pthread_once_shim(void *once, void (*fn)(void))
{
    if (!once || !fn) return EINVAL;
    int32_t *state = once;
    if (__sync_bool_compare_and_swap(state, 0, 2)) {
        fn();
        __sync_synchronize();
        *state = 1;
    } else {
        while (*state == 2) {
            sched_yield();
        }
    }
    return 0;
}

/* sysconf: guests ask for page size and the like. The truthful answer on
 * this host is 16 KiB, and that is what Android 15+ code now expects. */
long sysconf_shim(int name)
{
    switch (name) {
    case 29:  /* _SC_PAGESIZE */
    case 30:  /* _SC_PAGE_SIZE */
        return 16384;
    case 84:  /* _SC_NPROCESSORS_ONLN */
        return (long)sysconf(_SC_NPROCESSORS_ONLN);
    default:
        return sysconf(name);
    }
}

void *dlopen_shim(const char *path, int mode)
{
    (void)mode;
    tl_log_line("shim: guest asked to dlopen %s -- not provided", path ? path : "(self)");
    return NULL;
}

void *dlsym_shim(void *handle, const char *name)
{
    (void)handle;
    return tl_shim_find(name);
}

extern int *__error(void);

static unsigned long getauxval_shim(unsigned long type)
{
    if (type == 16) { /* AT_HWCAP */
        return (1ul << 0) | (1ul << 1) | (1ul << 2) | (1ul << 3) |
               (1ul << 4) | (1ul << 5) | (1ul << 7);
    }
    if (type == 6) { /* AT_PAGESZ */
        return 16384;
    }
    return 0;
}

static int *__errno_shim(void)
{
    return __error();
}

static int __system_property_get_shim(const char *name, char *value)
{
    if (!name || !value) return 0;
    if (!strcmp(name, "ro.build.version.sdk")) {
        strcpy(value, "33");
        return (int)strlen(value);
    }
    value[0] = '\0';
    return 0;
}

static size_t __strlen_chk_shim(const char *s, size_t maxlen)
{
    size_t len = strlen(s);
    if (len >= maxlen) abort();
    return len;
}

static void *__memmove_chk_shim(void *dst, const void *src, size_t len, size_t dstlen)
{
    if (len > dstlen) abort();
    return memmove(dst, src, len);
}

static int __vsnprintf_chk_shim(char *s, size_t maxlen, int flag, size_t slen, const char *format, va_list args)
{
    (void)flag; (void)slen;
    return vsnprintf(s, maxlen, format, args);
}

static void android_set_abort_message_shim(const char *msg)
{
    tl_log_line("abort: %s", msg ? msg : "");
}

static void __cxa_finalize_shim(void *d) { (void)d; }
static int __cxa_atexit_shim(void (*fn)(void *), void *arg, void *d) { (void)fn; (void)arg; (void)d; return 0; }
static int dl_iterate_phdr_shim(void *cb, void *data) { (void)cb; (void)data; return 0; }

static uint64_t g_sF_storage[24]; /* 3 fake FILE structs */
static FILE *map_stream(void *s) {
    if (s == &g_sF_storage[0]) return stdin;
    if (s == &g_sF_storage[8]) return stdout;
    if (s == &g_sF_storage[16]) return stderr;
    return (FILE *)s;
}

static int fprintf_shim(void *stream, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(map_stream(stream), fmt, ap);
    va_end(ap);
    return r;
}

static int vfprintf_shim(void *stream, const char *fmt, va_list ap) {
    return vfprintf(map_stream(stream), fmt, ap);
}

static int fputc_shim(int c, void *stream) {
    return fputc(c, map_stream(stream));
}

static int fflush_shim(void *stream) {
    return fflush(stream ? map_stream(stream) : NULL);
}

static size_t fwrite_shim(const void *ptr, size_t size, size_t nmemb, void *stream) {
    return fwrite(ptr, size, nmemb, map_stream(stream));
}

/* Surface and HardwareBuffer stubs */
static void *ANativeWindow_fromSurface_shim(void *env, void *surface) { (void)env; (void)surface; return NULL; }
static void *ASurfaceControl_create_shim(void) { return NULL; }
static void *ASurfaceControl_createFromWindow_shim(void *p, const char *n) { (void)p; (void)n; return NULL; }
static void ASurfaceControl_release_shim(void *sc) { (void)sc; }
static void *ASurfaceTransaction_create_shim(void) { return NULL; }
static void ASurfaceTransaction_delete_shim(void *t) { (void)t; }
static void ASurfaceTransaction_apply_shim(void *t) { (void)t; }
static void ASurfaceTransaction_reparent_shim(void *t, void *sc, void *p) { (void)t; (void)sc; (void)p; }
static void ASurfaceTransaction_setOnComplete_shim(void *t, void *c, void *l) { (void)t; (void)c; (void)l; }
static void ASurfaceTransaction_setOnCommit_shim(void *t, void *c, void *l) { (void)t; (void)c; (void)l; }
static void ASurfaceTransaction_setBuffer_shim(void *t, void *sc, void *b, int f) { (void)t; (void)sc; (void)b; (void)f; }
static void ASurfaceTransaction_setVisibility_shim(void *t, void *sc, int8_t v) { (void)t; (void)sc; (void)v; }
static void ASurfaceTransaction_setZOrder_shim(void *t, void *sc, int32_t z) { (void)t; (void)sc; (void)z; }
static void ASurfaceTransaction_setDamageRegion_shim(void *t, void *sc, const void *r, size_t n) { (void)t; (void)sc; (void)r; (void)n; }
static void ASurfaceTransaction_setDesiredPresentTime_shim(void *t, int64_t pt) { (void)t; (void)pt; }
static void ASurfaceTransaction_setBufferTransparency_shim(void *t, void *sc, int8_t tr) { (void)t; (void)sc; (void)tr; }
static void ASurfaceTransaction_setBufferAlpha_shim(void *t, void *sc, float a) { (void)t; (void)sc; (void)a; }
static void ASurfaceTransaction_setCrop_shim(void *t, void *sc, const void *c) { (void)t; (void)sc; (void)c; }
static void ASurfaceTransaction_setPosition_shim(void *t, void *sc, float x, float y) { (void)t; (void)sc; (void)x; (void)y; }
static void ASurfaceTransaction_setScale_shim(void *t, void *sc, float sx, float sy) { (void)t; (void)sc; (void)sx; (void)sy; }
static void ASurfaceTransaction_setBufferTransform_shim(void *t, void *sc, int32_t tr) { (void)t; (void)sc; (void)tr; }
static void ASurfaceTransaction_setBufferDataSpace_shim(void *t, void *sc, int32_t ds) { (void)t; (void)sc; (void)ds; }
static void ASurfaceTransaction_setGeometry_shim(void *t, void *sc, const void *s, const void *d, int32_t tr) { (void)t; (void)sc; (void)s; (void)d; (void)tr; }
static void ASurfaceTransactionStats_getASurfaceControls_shim(void *st, void ***sc, size_t *cnt) { (void)st; if (sc) *sc = NULL; if (cnt) *cnt = 0; }
static int ASurfaceTransactionStats_getPreviousReleaseFenceFd_shim(void *st, void *sc) { (void)st; (void)sc; return -1; }
static void ASurfaceTransactionStats_releaseASurfaceControls_shim(void *st) { (void)st; }
static void ASurfaceTransaction_setFrameRateWithChangeStrategy_shim(void *t, void *sc, float f, int8_t c, int8_t s) { (void)t; (void)sc; (void)f; (void)c; (void)s; }
static void ASurfaceTransaction_setFrameRate_shim(void *t, void *sc, float f, int8_t c) { (void)t; (void)sc; (void)f; (void)c; }
static void *AHardwareBuffer_fromHardwareBuffer_shim(void *env, void *hb) { (void)env; (void)hb; return NULL; }
static void AHardwareBuffer_describe_shim(const void *b, void *d) { (void)b; (void)d; }
static int AHardwareBuffer_lock_shim(void *b, uint64_t u, int f, const void *r, void **out) { (void)b; (void)u; (void)f; (void)r; if (out) *out = NULL; return -1; }
static void AHardwareBuffer_release_shim(void *b) { (void)b; }
static int AHardwareBuffer_unlock_shim(void *b, int *f) { (void)b; (void)f; return 0; }
static void *AHardwareBuffer_toHardwareBuffer_shim(void *env, const void *b) { (void)env; (void)b; return NULL; }
static int AHardwareBuffer_allocate_shim(const void *d, void **out) { (void)d; if (out) *out = NULL; return -1; }

void *tl_shim_find(const char *name);

typedef struct { const char *name; void *addr; } tl_export_entry;

int __android_log_write(int prio, const char *tag, const char *text)
{
    (void)prio;
    tl_log_line("%s: %s", tag ? tag : "guest", text ? text : "");
    return 1;
}

int __android_log_print(int prio, const char *tag, const char *fmt, ...)
{
    char text[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    return __android_log_write(prio, tag, text);
}

int __android_log_buf_print(int prio, int buf, const char *tag, const char *fmt, ...)
{
    (void)buf;
    char text[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    return __android_log_write(prio, tag, text);
}

void __android_log_assert(const char *cond, const char *tag, const char *fmt, ...)
{
    char text[1024];
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(text, sizeof(text), fmt, ap);
        va_end(ap);
    } else {
        snprintf(text, sizeof(text), "assertion failed: %s", cond ? cond : "unknown");
    }
    __android_log_write(6, tag, text);
    tl_loader_request_stop();
}

void tl_shim_bind_run(void *activity, void *window)
{
    (void)activity;
    (void)window;
}

/* One line each. Order does not matter; the linear scan is honest and
 * small. */
static const tl_export_entry g_exports[] = {
    /* logging */
    { "__android_log_print",        __android_log_print },
    { "__android_log_write",        __android_log_write },
    { "__android_log_buf_print",    __android_log_buf_print },
    { "__android_log_assert",       __android_log_assert },
    { "__android_log_vprint",       NULL },   /* varargs adapter below */

    /* window */
    { "ANativeWindow_getWidth",     ANativeWindow_getWidth },
    { "ANativeWindow_getHeight",    ANativeWindow_getHeight },
    { "ANativeWindow_getFormat",    ANativeWindow_getFormat },
    { "ANativeWindow_lock",         ANativeWindow_lock },
    { "ANativeWindow_unlockAndPost", ANativeWindow_unlockAndPost },
    { "ANativeWindow_acquire",      ANativeWindow_acquire },
    { "ANativeWindow_release",      ANativeWindow_release },
    { "ANativeWindow_setBuffersGeometry", ANativeWindow_setBuffersGeometry },
    { "ANativeWindow_setFrameRate", ANativeWindow_setFrameRate },

    /* activity */
    { "ANativeActivity_finish",     ANativeActivity_finish },
    { "ANativeActivity_setWindowFlags", ANativeActivity_setWindowFlags },
    { "ANativeActivity_showSoftInput", ANativeActivity_showSoftInput },
    { "ANativeActivity_hideSoftInput", ANativeActivity_hideSoftInput },

    /* input */
    { "AInputQueue_preDispatchEvent", AInputQueue_preDispatchEvent },
    { "AInputQueue_finishEvent",    AInputQueue_finishEvent },
    { "AInputQueue_attach",         AInputQueue_attach },
    { "AInputQueue_detach",         AInputQueue_detach },
    { "AInputEvent_getType",        AInputEvent_getType },
    { "AMotionEvent_getAction",     AMotionEvent_getAction },
    { "AMotionEvent_getPointerCount", AMotionEvent_getPointerCount },
    { "AMotionEvent_getX",          AMotionEvent_getX },
    { "AMotionEvent_getY",          AMotionEvent_getY },
    { "AMotionEvent_getEventTime",  AMotionEvent_getEventTime },

    /* assets */
    { "AAssetManager_open",         AAssetManager_open },
    { "AAsset_close",               AAsset_close },
    { "AAsset_read",                AAsset_read },
    { "AAsset_getLength",           AAsset_getLength },
    { "AAsset_getBuffer",           AAsset_getBuffer },

    /* EGL: resolver only, honest failure at call time */
    { "eglGetProcAddress",          eglGetProcAddress },
    { "eglGetError",                eglGetError },

    /* JNI entry points */
    { "JNI_GetDefaultJavaVMInitArgs", JNI_GetDefaultJavaVMInitArgs },
    { "JNI_CreateJavaVM",           JNI_CreateJavaVM },
    { "JNI_GetCreatedJavaVMs",      JNI_GetCreatedJavaVMs },

    /* libc: the 40-byte-type adapters and the passthroughs */
    { "pthread_mutex_init",         pthread_mutex_init_shim },
    { "pthread_mutex_lock",         pthread_mutex_lock_shim },
    { "pthread_mutex_trylock",      pthread_mutex_trylock_shim },
    { "pthread_mutex_unlock",       pthread_mutex_unlock_shim },
    { "pthread_mutex_destroy",      pthread_mutex_destroy_shim },
    { "pthread_once",               pthread_once_shim },
    { "sysconf",                    sysconf_shim },
    { "dlopen",                     dlopen_shim },
    { "dlsym",                      dlsym_shim },

    { "getauxval",                  getauxval_shim },
    { "__errno",                    __errno_shim },
    { "__system_property_get",      __system_property_get_shim },
    { "__strlen_chk",               __strlen_chk_shim },
    { "__memmove_chk",              __memmove_chk_shim },
    { "__vsnprintf_chk",            __vsnprintf_chk_shim },
    { "android_set_abort_message",  android_set_abort_message_shim },
    { "__cxa_finalize",             __cxa_finalize_shim },
    { "__cxa_atexit",               __cxa_atexit_shim },
    { "dl_iterate_phdr",            dl_iterate_phdr_shim },
    { "__sF",                       g_sF_storage },
    { "fprintf",                    fprintf_shim },
    { "vfprintf",                   vfprintf_shim },
    { "fputc",                      fputc_shim },
    { "fflush",                     fflush_shim },
    { "fwrite",                     fwrite_shim },

    { "ANativeWindow_fromSurface",  ANativeWindow_fromSurface_shim },
    { "ASurfaceControl_create",     ASurfaceControl_create_shim },
    { "ASurfaceControl_createFromWindow", ASurfaceControl_createFromWindow_shim },
    { "ASurfaceControl_release",    ASurfaceControl_release_shim },
    { "ASurfaceTransaction_create", ASurfaceTransaction_create_shim },
    { "ASurfaceTransaction_delete", ASurfaceTransaction_delete_shim },
    { "ASurfaceTransaction_apply",  ASurfaceTransaction_apply_shim },
    { "ASurfaceTransaction_reparent", ASurfaceTransaction_reparent_shim },
    { "ASurfaceTransaction_setOnComplete", ASurfaceTransaction_setOnComplete_shim },
    { "ASurfaceTransaction_setOnCommit", ASurfaceTransaction_setOnCommit_shim },
    { "ASurfaceTransaction_setBuffer", ASurfaceTransaction_setBuffer_shim },
    { "ASurfaceTransaction_setVisibility", ASurfaceTransaction_setVisibility_shim },
    { "ASurfaceTransaction_setZOrder", ASurfaceTransaction_setZOrder_shim },
    { "ASurfaceTransaction_setDamageRegion", ASurfaceTransaction_setDamageRegion_shim },
    { "ASurfaceTransaction_setDesiredPresentTime", ASurfaceTransaction_setDesiredPresentTime_shim },
    { "ASurfaceTransaction_setBufferTransparency", ASurfaceTransaction_setBufferTransparency_shim },
    { "ASurfaceTransaction_setBufferAlpha", ASurfaceTransaction_setBufferAlpha_shim },
    { "ASurfaceTransaction_setCrop", ASurfaceTransaction_setCrop_shim },
    { "ASurfaceTransaction_setPosition", ASurfaceTransaction_setPosition_shim },
    { "ASurfaceTransaction_setScale", ASurfaceTransaction_setScale_shim },
    { "ASurfaceTransaction_setBufferTransform", ASurfaceTransaction_setBufferTransform_shim },
    { "ASurfaceTransaction_setBufferDataSpace", ASurfaceTransaction_setBufferDataSpace_shim },
    { "ASurfaceTransaction_setGeometry", ASurfaceTransaction_setGeometry_shim },
    { "ASurfaceTransactionStats_getASurfaceControls", ASurfaceTransactionStats_getASurfaceControls_shim },
    { "ASurfaceTransactionStats_getPreviousReleaseFenceFd", ASurfaceTransactionStats_getPreviousReleaseFenceFd_shim },
    { "ASurfaceTransactionStats_releaseASurfaceControls", ASurfaceTransactionStats_releaseASurfaceControls_shim },
    { "ASurfaceTransaction_setFrameRateWithChangeStrategy", ASurfaceTransaction_setFrameRateWithChangeStrategy_shim },
    { "ASurfaceTransaction_setFrameRate", ASurfaceTransaction_setFrameRate_shim },
    { "AHardwareBuffer_fromHardwareBuffer", AHardwareBuffer_fromHardwareBuffer_shim },
    { "AHardwareBuffer_describe",   AHardwareBuffer_describe_shim },
    { "AHardwareBuffer_lock",       AHardwareBuffer_lock_shim },
    { "AHardwareBuffer_release",    AHardwareBuffer_release_shim },
    { "AHardwareBuffer_unlock",     AHardwareBuffer_unlock_shim },
    { "AHardwareBuffer_toHardwareBuffer", AHardwareBuffer_toHardwareBuffer_shim },
    { "AHardwareBuffer_allocate",   AHardwareBuffer_allocate_shim },
};

static int __android_log_vprint_shim(int prio, const char *tag, const char *fmt, va_list ap)
{
    char text[1024];
    vsnprintf(text, sizeof(text), fmt, ap);
    __android_log_write(prio, tag, text);
    return 1;
}

void *tl_shim_find(const char *name)
{
    if (!name) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(g_exports) / sizeof(g_exports[0]); i++) {
        if (!strcmp(g_exports[i].name, name) && g_exports[i].addr) {
            return g_exports[i].addr;
        }
    }
    if (!strcmp(name, "__android_log_vprint")) {
        return (void *)__android_log_vprint_shim;
    }
    void *host = dlsym(RTLD_DEFAULT, name);
    if (host) {
        return host;
    }
    return NULL;
}

bool tl_shim_supplies(const char *soname)
{
    /* The attempt provides liblog and libc behaviour in-process. libEGL
     * resolves far enough to relocate; calling further into EGL fails at
     * eglGetProcAddress with what the table holds. */
    return !strcmp(soname, "liblog.so") || !strcmp(soname, "libc.so")
        || !strcmp(soname, "libm.so") || !strcmp(soname, "libdl.so")
        || !strcmp(soname, "libandroid.so") || !strcmp(soname, "libEGL.so")
        || !strcmp(soname, "libGLESv2.so") || !strcmp(soname, "libGLESv1_CM.so")
        || !strcmp(soname, "libGLESv3.so");
}

/* The pieces the loader wires into the ANativeActivity it passes. */
void *tl_shim_new_input_queue(void)
{
    struct tl_input_queue *q = calloc(1, sizeof(*q));
    pthread_mutex_init(&q->lock, NULL);
    return q;
}

void *tl_shim_new_asset_manager(void)
{
    return calloc(1, sizeof(struct tl_asset_manager));
}

void tl_shim_free(void *p) { free(p); }
