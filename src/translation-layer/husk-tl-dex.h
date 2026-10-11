/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HUSK_TL_DEX_H
#define HUSK_TL_DEX_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tl_dex_file tl_dex_file;
typedef struct tl_dex_class tl_dex_class;
typedef struct tl_dex_method tl_dex_method;
typedef struct tl_dex_field tl_dex_field;
typedef struct tl_dex_object tl_dex_object;
typedef struct tl_dex_context tl_dex_context;

typedef union {
    int32_t  i;
    int64_t  j;
    float    f;
    double   d;
    tl_dex_object *l;
    uint32_t raw32;
    uint64_t raw64;
} tl_dex_val;

/* Slots java.lang.Enum reserves at the front of every enum object's field
 * table: its name (a String) and its ordinal (an int). The enum's own fields
 * follow them. See dex_link_class(). */
#define TL_ENUM_SLOT_NAME    0
#define TL_ENUM_SLOT_ORDINAL 1

struct tl_dex_field {
    tl_dex_class *clazz;
    const char *name;
    const char *type;
    uint32_t access_flags;
    bool is_static;
    uint32_t slot;          /* offset in object or static table */
    /* Set for a field of a class this layer does not load -- android.graphics.
     * Rect.top, Build.VERSION.SDK_INT. `owner` is the class the access named;
     * the framework answers these itself (see tl_framework_field_get), and
     * `unresolved_logged` keeps the log to one line per field. */
    const char *owner;
    bool unresolved_logged;
};

struct tl_dex_method {
    tl_dex_class *clazz;
    const char *name;
    const char *shorty;
    const char *signature;
    uint32_t access_flags;
    uint16_t registers_size;
    uint16_t ins_size;
    uint16_t outs_size;
    uint32_t insns_size;
    const uint16_t *insns;
    /* Native function pointer if ACC_NATIVE or framework builtin */
    void *native_func;
    /* Set once a call into this method has been reported as having no body and
     * no shim, so the log names each framework gap once instead of per call. */
    bool unshimmed_logged;
    /* The class the call named, for a method that belongs to no class here --
     * a framework method. `clazz` is NULL for those, so without this a gap in
     * android.graphics.Canvas could not even be reported by name. */
    const char *owner;
};

struct tl_dex_class {
    tl_dex_context *ctx;
    tl_dex_file *dex;
    const char *descriptor;     /* e.g. "Lcom/flappybird/recreation/c;" */
    const char *super_descriptor;
    tl_dex_class *super_class;
    uint32_t access_flags;

    int num_fields;
    tl_dex_field *fields;
    int num_instance_fields;
    int num_static_fields;

    int num_methods;
    tl_dex_method *methods;

    /* Static storage */
    tl_dex_val *static_values;
    bool initialized;

    /*
     * Instance layout, including what the class inherits.
     *
     * Every class used to number its own instance fields from zero, so a
     * subclass's first field and its parent's first field were the same slot.
     * Neither is wrong on its own; together they make an int written through
     * one read back as an object through the other, which is how a perfectly
     * good Bitmap call ended up handed 0x800000003 as its receiver.
     *
     * `instance_base` is how many slots come before this class's own fields,
     * and `instance_size` the total an object of this class needs.
     */
    int instance_base;
    int instance_size;
    bool linked;

    tl_dex_class *next_hash;
    /* Native bridge handler if custom (e.g. Canvas, View, Bitmap) */
    void *native_class_data;
};

/*
 * What an object's storage actually is.
 *
 * The object holds ONE of four things in a union -- a field table, array data, a
 * string, or a native wrapper -- and nothing used to say which. Every shim that
 * wanted a native wrapper just read `native_ptr`, so being handed an array, or a
 * plain object, or a string by mistake did not fail: it read two small integers
 * (an array's length and element size, fused) as a pointer and dereferenced
 * them. Several crashes in a row had exactly that address.
 *
 * The kind is set where an object is made, and the accessors below return NULL
 * for anything of the wrong kind, so a shim given the wrong thing gets "nothing"
 * and not somebody else's memory.
 */
#define TL_KIND_MASK    0x7u
#define TL_KIND_OBJECT  0u     /* fields[], nfields slots of it */
#define TL_KIND_ARRAY   1u     /* array.length / elem_size / elements */
#define TL_KIND_STRING  2u     /* str_utf8 */
#define TL_KIND_NATIVE  3u     /* native_ptr */

struct tl_dex_object {
    tl_dex_class *clazz;
    uint32_t flags;            /* low bits: the kind */
    /* Slots in `fields`, or zero when the union below holds something else.
     * Every field access checks against this: the slot comes from the bytecode,
     * and a wrong one must read as null rather than as somebody else's memory. */
    uint32_t nfields;
    union {
        /* Object fields */
        tl_dex_val *fields;
        /* Array data */
        struct {
            uint32_t length;
            uint32_t elem_size;
            void *elements;
        } array;
        /* Java string value */
        char *str_utf8;
        /* Native object wrapper (e.g. Canvas*, Bitmap*, View*) */
        void *native_ptr;
    };
};

#define TL_DEX_CLASS_HASH_SIZE 16384

struct tl_dex_context {
    /*
     * Prints every executed instruction while nonzero. Only exists in a build
     * made with -DTL_DEX_TRACE; otherwise the interpreter does not even test it.
     *
     * Per-instruction logging used to be always on, and a single frame produced
     * nineteen thousand lines -- ten thousand times slower, and enough text to
     * stall the log view. That was the right thing to remove. It is also the
     * only way to see what a game's bytecode actually does with an input, so it
     * stays as something a diagnostic build can turn on for one call.
     */
    int trace;

    /* Told about every bitmap drawn at a position, if set. A test that plays the
     * game needs to know where its sprites are, and the draws are the one place
     * that is certain -- the game's own fields have obfuscated names. */
    void (*draw_observer)(void *user, int bitmap_w, int bitmap_h, float x, float y);
    void *draw_observer_user;

    /* 0: draw through CoreGraphics only. Used to compare the two renderers on
     * real frames, and as the way back if the blitter ever disagrees. */
    int use_cg_only;

    /* java.lang.Math.random's state. Seedable, so a run can be replayed: the
     * same seed gives the same pipes, which is what makes two renderers
     * comparable frame for frame. Seeded from the system unless told otherwise. */
    uint64_t rng;

    /* Where a frame's time goes. See tl_dex_perf. */
    struct tl_dex_perf {
        uint64_t frames;
        uint64_t ns_logic;    /* input, deferred tasks, and the app's own doFrame */
        uint64_t ns_render;   /* the app's onDraw, everything it calls included */
        uint64_t ns_draw;     /* of that, time inside the canvas draw calls */
        uint64_t draws;       /* how many of those there were */
    } perf;

    /*
     * The layer's own clock, in nanoseconds, as the host last reported it.
     *
     * System.nanoTime() and currentTimeMillis() answer from this rather than
     * from the machine's clock, for the reason Android's own Choreographer does:
     * a frame callback is handed a time, and game code freely compares it with
     * nanoTime(). Two different clocks would make every such comparison nonsense.
     * It also makes a run reproducible, which is worth a lot when the only way
     * to see what a game did is to run it again.
     */
    uint64_t clock_nanos;

    int num_dex_files;
    tl_dex_file *dex_files[16];

    int num_classes;
    int class_capacity;
    tl_dex_class **classes;
    tl_dex_class *class_hash[TL_DEX_CLASS_HASH_SIZE];

    /* Display surface for drawing */
    uint32_t *framebuffer;
    int fb_width;
    int fb_height;

    /* Active game view and activity */
    tl_dex_object *current_view;
    tl_dex_object *current_activity;

    /* Registered Choreographer frame callback */
    tl_dex_object *choreographer_cb;

    /* Work an app asked to be done later: View.postDelayed, Handler.post... */
    struct tl_dex_task *tasks;

    /*
     * Input waiting for the next frame.
     *
     * Touches arrive on the UI thread; the interpreter runs on the frame pump's
     * thread and has one shared context -- one result register, one task list,
     * one allocator -- with no locking at all. Delivering a touch where it
     * arrived ran the app's handler concurrently with a frame, a data race on
     * every tap that would surface only as rare, unreproducible corruption.
     * Android avoids this by running input and frames on one looper thread, so
     * this does the same: send_touch only queues, under a lock, and the pump
     * delivers at the top of each frame, before deferred tasks and doFrame --
     * the order Choreographer uses.
     */
    pthread_mutex_t input_lock;
    struct { int action; float x, y; } input[64];
    int input_head, input_tail;

    /* Total frames delivered */
    uint64_t frame_count;

    /* Asset lookup from APK */
    const char *apk_path;

    /* Framework private state */
    void *framework_data;
};

/* Context lifecycle */
/*
 * Deferred work.
 *
 * Android apps lean on a main-thread message queue constantly: "do this in 300
 * ms", "do this after the current event". There is no queue here and nothing
 * ran, which looks harmless until the thing deferred is the code that re-enables
 * the very input that deferred it -- Flappy Bird locks touch for a moment after
 * each flap and unlocks it with a postDelayed, so with no queue the first tap
 * worked and every tap after it was ignored.
 *
 * The queue is ordered by due time on the layer's own clock, and drained at the
 * start of every frame. Due means due: it never runs a task early, and a task
 * that posts another is not run again until a later frame.
 */
typedef struct tl_dex_task {
    uint64_t due_nanos;
    tl_dex_object *runnable;
    struct tl_dex_task *next;
} tl_dex_task;

/* Monotonic nanoseconds, for measuring. The same clock the host hands the
 * layer as frame time on the device, so a measurement and a frame stamp agree. */
uint64_t tl_dex_now_ns(void);

/*
 * A frame pacer: sixty frames a second on a deadline.
 *
 * A pump that draws a frame and then sleeps a full frame's length puts the sleep
 * on top of the work, so a frame that costs five milliseconds takes twenty-one
 * and the whole thing runs slow. This gives every frame a deadline -- the last
 * one's plus a frame -- and sleeps only for what remains of it, so work is
 * absorbed rather than added.
 *
 * A pacer that has fallen far behind does not run frames back to back to catch
 * up. After a stall that sprint is a burst of frames with no time between them,
 * and it only makes the next stall longer; it resynchronises to now instead.
 */
typedef struct tl_pacer {
    uint64_t frame_ns;
    uint64_t next;          /* the deadline of the frame in progress */
    uint32_t tb_numer, tb_denom;
} tl_pacer;

void tl_pacer_start(tl_pacer *p, uint64_t frame_ns);

/* Call after each frame's work. Sleeps out what is left of the frame's time.
 * Returns true if the frame missed its deadline. */
bool tl_pacer_wait(tl_pacer *p);

/* Replay a run: the same seed gives the same Math.random sequence. */
void tl_dex_seed(tl_dex_context *ctx, uint64_t seed);

/* The next Math.random in [0, 1). */
double tl_dex_random(tl_dex_context *ctx);

void tl_dex_post_delayed(tl_dex_context *ctx, tl_dex_object *runnable, uint64_t delay_ms);
void tl_dex_remove_callbacks(tl_dex_context *ctx, tl_dex_object *runnable);

tl_dex_context *tl_dex_context_create(const char *apk_path, uint32_t *fb, int width, int height);
void tl_dex_context_destroy(tl_dex_context *ctx);

/* Load classes*.dex from the given APK */
bool tl_dex_load_apk(tl_dex_context *ctx, const char *apk_path);

/* Find class by descriptor */
tl_dex_class *tl_dex_find_class(tl_dex_context *ctx, const char *descriptor);

/* Find method on class or superclasses */
tl_dex_method *tl_dex_find_method(tl_dex_class *clazz, const char *name, const char *shorty);

/* Find field on class or superclasses */
tl_dex_field *tl_dex_find_field(tl_dex_class *clazz, const char *name, const char *type);

/* Object allocation */
tl_dex_object *tl_dex_alloc_object(tl_dex_class *clazz);

/*
 * Give an object a native wrapper (a Rect, a Bitmap, a list).
 *
 * `native_ptr` shares storage with `fields`, so taking it over has to release
 * the field table and zero `nfields` -- otherwise the table leaks, and a
 * bytecode field read on the object indexes straight through the wrapper's
 * struct as if it were an array of values.
 */
static inline void tl_dex_set_native(tl_dex_object *obj, void *ptr)
{
    if (!obj) return;
    if (obj->nfields) {
        free(obj->fields);
        obj->nfields = 0;
    }
    obj->native_ptr = ptr;
    obj->flags = (obj->flags & ~TL_KIND_MASK) | TL_KIND_NATIVE;
}

/*
 * Make an existing object hold a string (a StringBuilder's buffer, say). The
 * same hand-over as tl_dex_set_native: the field table goes, nfields goes to
 * zero, and the kind changes so the string can be read back at all. Takes
 * ownership of `str`, and frees the buffer it replaces.
 */
static inline void tl_dex_set_string(tl_dex_object *obj, char *str)
{
    if (!obj) return;
    if (obj->nfields) {
        free(obj->fields);
        obj->nfields = 0;
    } else if ((obj->flags & TL_KIND_MASK) == TL_KIND_STRING) {
        free(obj->str_utf8);
    }
    obj->str_utf8 = str;
    obj->flags = (obj->flags & ~TL_KIND_MASK) | TL_KIND_STRING;
}

static inline uint32_t tl_dex_kind(const tl_dex_object *o)
{
    return o ? (o->flags & TL_KIND_MASK) : TL_KIND_OBJECT;
}

/* The object's native wrapper, or NULL if it does not have one. */
static inline void *tl_dex_native(const tl_dex_object *o)
{
    return (o && tl_dex_kind(o) == TL_KIND_NATIVE) ? o->native_ptr : NULL;
}

/* The object's string, or NULL if it is not a string. */
static inline const char *tl_dex_string(const tl_dex_object *o)
{
    return (o && tl_dex_kind(o) == TL_KIND_STRING) ? o->str_utf8 : NULL;
}

/* The object, if and only if it is an array with storage behind it. */
static inline tl_dex_object *tl_dex_array(tl_dex_object *o)
{
    return (o && tl_dex_kind(o) == TL_KIND_ARRAY && o->array.elements) ? o : NULL;
}
tl_dex_object *tl_dex_alloc_array(tl_dex_class *elem_class, uint32_t length, uint32_t elem_size);
tl_dex_object *tl_dex_alloc_string(tl_dex_context *ctx, const char *utf8);

/* Bytecode execution */
bool tl_dex_invoke(tl_dex_context *ctx, tl_dex_method *method, tl_dex_val *args, int nargs, tl_dex_val *ret);

/* Frame tick and input event dispatch */
void tl_dex_tick_frame(tl_dex_context *ctx, uint64_t frame_time_nanos);
void tl_dex_send_touch(tl_dex_context *ctx, int action, float x, float y);

#ifdef __cplusplus
}
#endif

#endif /* HUSK_TL_DEX_H */
