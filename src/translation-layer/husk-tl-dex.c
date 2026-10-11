/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <mach/mach_time.h>
#include "husk-tl-dex.h"
#include "husk-tl-framework.h"
#include "husk-tl-internal.h"
#include "husk-tl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <dlfcn.h>

/* DEX file header format */
typedef struct {
    uint8_t  magic[8];
    uint32_t checksum;
    uint8_t  signature[20];
    uint32_t file_size;
    uint32_t header_size;
    uint32_t endian_tag;
    uint32_t link_size;
    uint32_t link_off;
    uint32_t map_off;
    uint32_t string_ids_size;
    uint32_t string_ids_off;
    uint32_t type_ids_size;
    uint32_t type_ids_off;
    uint32_t proto_ids_size;
    uint32_t proto_ids_off;
    uint32_t field_ids_size;
    uint32_t field_ids_off;
    uint32_t method_ids_size;
    uint32_t method_ids_off;
    uint32_t class_defs_size;
    uint32_t class_defs_off;
    uint32_t data_size;
    uint32_t data_off;
} dex_header;

struct tl_dex_file {
    uint8_t *data;
    size_t size;
    const dex_header *hdr;
    const uint32_t *string_ids;
    const uint32_t *type_ids;
    const uint8_t  *proto_ids;
    const uint8_t  *field_ids;
    const uint8_t  *method_ids;
    const uint8_t  *class_defs;

    /* Resolution caches */
    tl_dex_field  **resolved_fields;
    tl_dex_method **resolved_methods;
    tl_dex_class  **resolved_types;
};

/* LEB128 decoders */
static uint32_t read_uleb128(const uint8_t **p)
{
    uint32_t val = 0;
    int shift = 0;
    while (1) {
        uint8_t b = *(*p)++;
        val |= (uint32_t)(b & 0x7f) << shift;
        shift += 7;
        if (!(b & 0x80)) break;
    }
    return val;
}

__attribute__((unused)) static int32_t read_sleb128(const uint8_t **p)
{
    int32_t val = 0;
    int shift = 0;
    uint8_t b;
    do {
        b = *(*p)++;
        val |= (int32_t)(b & 0x7f) << shift;
        shift += 7;
    } while (b & 0x80);
    if (shift < 32 && (b & 0x40)) {
        val |= ~0 << shift;
    }
    return val;
}

static const char *dex_get_string(const tl_dex_file *dex, uint32_t idx)
{
    if (!dex || idx >= dex->hdr->string_ids_size) return "";
    uint32_t off = dex->string_ids[idx];
    if (off >= dex->size) return "";
    const uint8_t *p = dex->data + off;
    (void)read_uleb128(&p); /* skip utf16_size */
    return (const char *)p;
}

static const char *dex_get_type(const tl_dex_file *dex, uint32_t idx)
{
    if (!dex || idx >= dex->hdr->type_ids_size) return "";
    uint32_t str_idx = dex->type_ids[idx];
    return dex_get_string(dex, str_idx);
}

static void dex_get_field_info(const tl_dex_file *dex, uint32_t idx,
                               const char **class_desc, const char **name, const char **type_desc)
{
    if (!dex || idx >= dex->hdr->field_ids_size) return;
    const uint8_t *f = dex->field_ids + idx * 8;
    uint16_t c_idx = (uint16_t)f[0] | ((uint16_t)f[1] << 8);
    uint16_t t_idx = (uint16_t)f[2] | ((uint16_t)f[3] << 8);
    uint32_t n_idx = (uint32_t)f[4] | ((uint32_t)f[5] << 8) | ((uint32_t)f[6] << 16) | ((uint32_t)f[7] << 24);
    if (class_desc) *class_desc = dex_get_type(dex, c_idx);
    if (type_desc)  *type_desc  = dex_get_type(dex, t_idx);
    if (name)       *name       = dex_get_string(dex, n_idx);
}

static void dex_get_method_info(const tl_dex_file *dex, uint32_t idx,
                                const char **class_desc, const char **name, const char **shorty)
{
    if (!dex || idx >= dex->hdr->method_ids_size) return;
    const uint8_t *m = dex->method_ids + idx * 8;
    uint16_t c_idx = (uint16_t)m[0] | ((uint16_t)m[1] << 8);
    uint16_t p_idx = (uint16_t)m[2] | ((uint16_t)m[3] << 8);
    uint32_t n_idx = (uint32_t)m[4] | ((uint32_t)m[5] << 8) | ((uint32_t)m[6] << 16) | ((uint32_t)m[7] << 24);
    if (class_desc) *class_desc = dex_get_type(dex, c_idx);
    if (name)       *name       = dex_get_string(dex, n_idx);
    if (shorty) {
        const uint8_t *pr = dex->proto_ids + p_idx * 12;
        uint32_t s_idx = (uint32_t)pr[0] | ((uint32_t)pr[1] << 8) | ((uint32_t)pr[2] << 16) | ((uint32_t)pr[3] << 24);
        *shorty = dex_get_string(dex, s_idx);
    }
}

/* ------------------------------------------------------------- Context */

tl_dex_context *tl_dex_context_create(const char *apk_path, uint32_t *fb, int width, int height)
{
    tl_dex_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    pthread_mutex_init(&ctx->input_lock, NULL);
    tl_dex_seed(ctx, ((uint64_t)arc4random() << 32) | arc4random());
    ctx->apk_path = apk_path ? strdup(apk_path) : NULL;
    ctx->framebuffer = fb;
    ctx->fb_width = width;
    ctx->fb_height = height;
    ctx->class_capacity = 512;
    ctx->classes = calloc(ctx->class_capacity, sizeof(tl_dex_class *));
    tl_framework_init(ctx);
    return ctx;
}

void tl_dex_context_destroy(tl_dex_context *ctx)
{
    if (!ctx) return;
    pthread_mutex_destroy(&ctx->input_lock);
    while (ctx->tasks) {
        tl_dex_task *t = ctx->tasks;
        ctx->tasks = t->next;
        free(t);
    }
    tl_framework_cleanup(ctx);
    for (int i = 0; i < ctx->num_dex_files; i++) {
        if (ctx->dex_files[i]) {
            free(ctx->dex_files[i]->resolved_fields);
            free(ctx->dex_files[i]->resolved_methods);
            free(ctx->dex_files[i]->resolved_types);
            free(ctx->dex_files[i]->data);
            free(ctx->dex_files[i]);
        }
    }
    for (int i = 0; i < ctx->num_classes; i++) {
        tl_dex_class *c = ctx->classes[i];
        if (c) {
            free(c->fields);
            free(c->methods);
            free(c->static_values);
            free(c);
        }
    }
    free(ctx->classes);
    free((void *)ctx->apk_path);
    free(ctx);
}

static inline uint32_t class_hash_str(const char *s)
{
    uint32_t h = 5381;
    while (*s) h = ((h << 5) + h) + (uint8_t)(*s++);
    return h;
}

static void context_add_class(tl_dex_context *ctx, tl_dex_class *clazz)
{
    if (ctx->num_classes >= ctx->class_capacity) {
        ctx->class_capacity *= 2;
        ctx->classes = realloc(ctx->classes, ctx->class_capacity * sizeof(tl_dex_class *));
    }
    ctx->classes[ctx->num_classes++] = clazz;

    if (clazz->descriptor) {
        uint32_t h = class_hash_str(clazz->descriptor) % TL_DEX_CLASS_HASH_SIZE;
        clazz->next_hash = ctx->class_hash[h];
        ctx->class_hash[h] = clazz;
    }
}

tl_dex_class *tl_dex_find_class(tl_dex_context *ctx, const char *descriptor)
{
    if (!ctx || !descriptor) return NULL;
    uint32_t h = class_hash_str(descriptor) % TL_DEX_CLASS_HASH_SIZE;
    for (tl_dex_class *c = ctx->class_hash[h]; c; c = c->next_hash) {
        if (!strcmp(c->descriptor, descriptor)) {
            return c;
        }
    }
    return NULL;
}

tl_dex_method *tl_dex_find_method(tl_dex_class *clazz, const char *name, const char *shorty)
{
    if (!clazz || !name) return NULL;
    for (int i = 0; i < clazz->num_methods; i++) {
        if (!strcmp(clazz->methods[i].name, name)) {
            if (!shorty || !strcmp(clazz->methods[i].shorty, shorty)) {
                return &clazz->methods[i];
            }
        }
    }
    if (clazz->super_class) {
        return tl_dex_find_method(clazz->super_class, name, shorty);
    }
    return NULL;
}

tl_dex_field *tl_dex_find_field(tl_dex_class *clazz, const char *name, const char *type)
{
    if (!clazz || !name) return NULL;
    for (int i = 0; i < clazz->num_fields; i++) {
        if (!strcmp(clazz->fields[i].name, name)) {
            if (!type || !strcmp(clazz->fields[i].type, type)) {
                return &clazz->fields[i];
            }
        }
    }
    if (clazz->super_class) {
        return tl_dex_find_field(clazz->super_class, name, type);
    }
    return NULL;
}

/* ---------------------------------------------------------------- Layout */

/*
 * Instance slots a framework superclass reserves ahead of the app's own.
 *
 * Only java.lang.Enum has any: `name` and `ordinal` are real fields that every
 * enum constant carries, and its constructor writes them. The framework
 * classes this layer shims keep their state in a native wrapper instead (see
 * tl_dex_set_native), so they reserve nothing.
 */
static int framework_instance_slots(const char *descriptor)
{
    if (descriptor && !strcmp(descriptor, "Ljava/lang/Enum;")) return 2;
    return 0;
}

/*
 * Number a class's instance fields after everything it inherits.
 *
 * Recursive, because a subclass cannot be numbered until its parent has been,
 * and memoised through `linked`. That flag is set on entry rather than exit so
 * that a hierarchy that loops back on itself -- which a malformed DEX can
 * describe -- ends the recursion instead of overflowing the stack.
 */
static void dex_link_class(tl_dex_class *c)
{
    if (!c || c->linked) return;
    c->linked = true;

    int base = 0;
    if (c->super_class) {
        dex_link_class(c->super_class);
        base = c->super_class->instance_size;
    } else {
        base = framework_instance_slots(c->super_descriptor);
    }

    c->instance_base = base;
    for (int f = 0; f < c->num_instance_fields; f++) {
        c->fields[c->num_static_fields + f].slot = (uint32_t)(base + f);
    }
    c->instance_size = base + c->num_instance_fields;
}

static void dex_log(const char *fmt, ...);

/* A field read or written that neither the app nor the framework shims could
 * answer. Said once per field: each line is a piece of the framework still to
 * write, and a loop would otherwise say it every pass. */
static void dex_note_unresolved_field(tl_dex_field *f, const char *what)
{
    if (!f || f->unresolved_logged) return;
    f->unresolved_logged = true;
    dex_log("tl: unresolved field %s %s->%s %s", what, f->owner ? f->owner : "?",
            f->name, f->type);
}

/* ------------------------------------------------------------- Resolvers */

static tl_dex_field *dex_resolve_field(tl_dex_context *ctx, tl_dex_file *dex, uint32_t idx)
{
    if (!dex || idx >= dex->hdr->field_ids_size) return NULL;
    if (dex->resolved_fields[idx]) return dex->resolved_fields[idx];

    const char *class_desc = NULL, *fname = NULL, *ftype = NULL;
    dex_get_field_info(dex, idx, &class_desc, &fname, &ftype);
    tl_dex_class *c = tl_dex_find_class(ctx, class_desc);
    tl_dex_field *f = c ? tl_dex_find_field(c, fname, ftype) : NULL;
    if (!f) {
        f = calloc(1, sizeof(*f));
        f->clazz = c;
        f->name = strdup(fname ? fname : "");
        f->type = strdup(ftype ? ftype : "");
        /* Not a field this layer knows. An out-of-range slot makes every
         * access to it read as null and write nowhere, which is what the
         * bounds checks above are for; a slot taken from num_fields++ pointed
         * past the end of both the static table and the object. */
        f->slot = UINT32_MAX;
        f->owner = class_desc ? strdup(class_desc) : NULL;
    }
    dex->resolved_fields[idx] = f;
    return f;
}

/*
 * Find the shim for a method, looking up the class's framework ancestry too.
 *
 * A call names the class it was compiled against: `d.ordinal()` for the game's
 * own enum, `c.getContext()` for its own View subclass. Neither method is
 * defined there. They belong to java.lang.Enum and android.view.View, which is
 * where the shims are registered -- so looking only under the name in the call
 * could never find them, and every inherited framework method silently did
 * nothing.
 *
 * The walk goes up through each class's declared superclass, loaded or not:
 * the app's own classes have no shim of their own and cost nothing to try, and
 * the first framework class above them is the one that matters.
 */
static tl_dex_native_func dex_lookup_shim(const char *class_desc, tl_dex_class *c,
                                          const char *mname, const char *shorty)
{
    tl_dex_native_func fn = tl_framework_lookup(class_desc, mname, shorty);
    if (fn) return fn;

    /* Arrays answer clone() themselves; the class in the call is "[L...;". */
    if (class_desc && class_desc[0] == '[' && !strcmp(mname, "clone")) {
        return tl_framework_lookup("[", "clone", shorty);
    }

    int depth = 0;   /* bounded: a malformed hierarchy can loop */
    for (tl_dex_class *k = c; k && depth < 64; k = k->super_class, depth++) {
        if (k->super_descriptor) {
            fn = tl_framework_lookup(k->super_descriptor, mname, shorty);
            if (fn) return fn;
        }
    }
    return NULL;
}

static tl_dex_method *dex_resolve_method(tl_dex_context *ctx, tl_dex_file *dex, uint32_t idx)
{
    if (!dex || idx >= dex->hdr->method_ids_size) return NULL;
    if (dex->resolved_methods[idx]) return dex->resolved_methods[idx];

    const char *class_desc = NULL, *mname = NULL, *shorty = NULL;
    dex_get_method_info(dex, idx, &class_desc, &mname, &shorty);

    tl_dex_class *c = tl_dex_find_class(ctx, class_desc);
    tl_dex_method *m = c ? tl_dex_find_method(c, mname, shorty) : NULL;

    if (!m) {
        tl_dex_native_func nfunc = dex_lookup_shim(class_desc, c, mname, shorty);
        m = calloc(1, sizeof(*m));
        m->clazz = c;
        m->owner = class_desc ? strdup(class_desc) : NULL;
        m->name = strdup(mname ? mname : "");
        m->shorty = strdup(shorty ? shorty : "V");
        m->native_func = (void *)nfunc;
    }
    dex->resolved_methods[idx] = m;
    return m;
}

/* ----------------------------------------------------------- DEX Loading */

static tl_dex_file *parse_dex_buffer(tl_dex_context *ctx, uint8_t *data, size_t size)
{
    if (size < sizeof(dex_header)) return NULL;
    if (memcmp(data, "dex\n", 4) != 0) return NULL;

    const dex_header *hdr = (const dex_header *)data;
    tl_dex_file *dex = calloc(1, sizeof(*dex));
    if (!dex) return NULL;
    dex->data = data;
    dex->size = size;
    dex->hdr = hdr;
    dex->string_ids = (const uint32_t *)(data + hdr->string_ids_off);
    dex->type_ids   = (const uint32_t *)(data + hdr->type_ids_off);
    dex->proto_ids  = data + hdr->proto_ids_off;
    dex->field_ids  = data + hdr->field_ids_off;
    dex->method_ids = data + hdr->method_ids_off;
    dex->class_defs = data + hdr->class_defs_off;

    dex->resolved_fields  = calloc(hdr->field_ids_size, sizeof(void *));
    dex->resolved_methods = calloc(hdr->method_ids_size, sizeof(void *));
    dex->resolved_types   = calloc(hdr->type_ids_size, sizeof(void *));

    for (uint32_t i = 0; i < hdr->class_defs_size; i++) {
        const uint8_t *cd = dex->class_defs + i * 32;
        uint32_t class_idx = (uint32_t)cd[0] | ((uint32_t)cd[1] << 8) | ((uint32_t)cd[2] << 16) | ((uint32_t)cd[3] << 24);
        uint32_t access_flags = (uint32_t)cd[4] | ((uint32_t)cd[5] << 8) | ((uint32_t)cd[6] << 16) | ((uint32_t)cd[7] << 24);
        uint32_t superclass_idx = (uint32_t)cd[8] | ((uint32_t)cd[9] << 8) | ((uint32_t)cd[10] << 16) | ((uint32_t)cd[11] << 24);
        uint32_t class_data_off = (uint32_t)cd[24] | ((uint32_t)cd[25] << 8) | ((uint32_t)cd[26] << 16) | ((uint32_t)cd[27] << 24);

        tl_dex_class *c = calloc(1, sizeof(*c));
        c->ctx = ctx;
        c->dex = dex;
        c->descriptor = dex_get_type(dex, class_idx);
        c->super_descriptor = superclass_idx != 0xFFFFFFFFu ? dex_get_type(dex, superclass_idx) : NULL;
        c->access_flags = access_flags;

        if (class_data_off != 0 && class_data_off < size) {
            const uint8_t *p = dex->data + class_data_off;
            uint32_t static_fields_count   = read_uleb128(&p);
            uint32_t instance_fields_count = read_uleb128(&p);
            uint32_t direct_methods_count   = read_uleb128(&p);
            uint32_t virtual_methods_count  = read_uleb128(&p);

            c->num_fields = static_fields_count + instance_fields_count;
            c->num_static_fields = static_fields_count;
            c->num_instance_fields = instance_fields_count;
            if (c->num_fields > 0) {
                c->fields = calloc(c->num_fields, sizeof(tl_dex_field));
            }
            if (static_fields_count > 0) {
                c->static_values = calloc(static_fields_count, sizeof(tl_dex_val));
            }

            uint32_t fid = 0;
            for (uint32_t f = 0; f < static_fields_count; f++) {
                fid += read_uleb128(&p);
                uint32_t flags = read_uleb128(&p);
                const char *fname = NULL, *ftype = NULL;
                dex_get_field_info(dex, fid, NULL, &fname, &ftype);
                c->fields[f] = (tl_dex_field){
                    .clazz = c, .name = fname, .type = ftype, .access_flags = flags, .is_static = true, .slot = f
                };
                if (fid < hdr->field_ids_size) dex->resolved_fields[fid] = &c->fields[f];
            }
            fid = 0;
            for (uint32_t f = 0; f < instance_fields_count; f++) {
                fid += read_uleb128(&p);
                uint32_t flags = read_uleb128(&p);
                const char *fname = NULL, *ftype = NULL;
                dex_get_field_info(dex, fid, NULL, &fname, &ftype);
                c->fields[static_fields_count + f] = (tl_dex_field){
                    .clazz = c, .name = fname, .type = ftype, .access_flags = flags, .is_static = false, .slot = f
                };
                if (fid < hdr->field_ids_size) dex->resolved_fields[fid] = &c->fields[static_fields_count + f];
            }

            c->num_methods = direct_methods_count + virtual_methods_count;
            if (c->num_methods > 0) {
                c->methods = calloc(c->num_methods, sizeof(tl_dex_method));
            }
            uint32_t mid = 0;
            for (uint32_t m = 0; m < direct_methods_count; m++) {
                mid += read_uleb128(&p);
                uint32_t flags = read_uleb128(&p);
                uint32_t code_off = read_uleb128(&p);
                const char *mname = NULL, *shorty = NULL;
                dex_get_method_info(dex, mid, NULL, &mname, &shorty);

                tl_dex_method *meth = &c->methods[m];
                meth->clazz = c;
                meth->name = mname;
                meth->shorty = shorty;
                meth->access_flags = flags;
                if (code_off != 0 && code_off + 16 <= size) {
                    const uint8_t *cp = dex->data + code_off;
                    meth->registers_size = (uint16_t)cp[0] | ((uint16_t)cp[1] << 8);
                    meth->ins_size       = (uint16_t)cp[2] | ((uint16_t)cp[3] << 8);
                    meth->outs_size      = (uint16_t)cp[4] | ((uint16_t)cp[5] << 8);
                    meth->insns_size     = (uint32_t)cp[12] | ((uint32_t)cp[13] << 8) | ((uint32_t)cp[14] << 16) | ((uint32_t)cp[15] << 24);
                    meth->insns          = (const uint16_t *)(cp + 16);
                }
                if (mid < hdr->method_ids_size) dex->resolved_methods[mid] = meth;
            }
            mid = 0;
            for (uint32_t m = 0; m < virtual_methods_count; m++) {
                mid += read_uleb128(&p);
                uint32_t flags = read_uleb128(&p);
                uint32_t code_off = read_uleb128(&p);
                const char *mname = NULL, *shorty = NULL;
                dex_get_method_info(dex, mid, NULL, &mname, &shorty);

                tl_dex_method *meth = &c->methods[direct_methods_count + m];
                meth->clazz = c;
                meth->name = mname;
                meth->shorty = shorty;
                meth->access_flags = flags;
                if (code_off != 0 && code_off + 16 <= size) {
                    const uint8_t *cp = dex->data + code_off;
                    meth->registers_size = (uint16_t)cp[0] | ((uint16_t)cp[1] << 8);
                    meth->ins_size       = (uint16_t)cp[2] | ((uint16_t)cp[3] << 8);
                    meth->outs_size      = (uint16_t)cp[4] | ((uint16_t)cp[5] << 8);
                    meth->insns_size     = (uint32_t)cp[12] | ((uint32_t)cp[13] << 8) | ((uint32_t)cp[14] << 16) | ((uint32_t)cp[15] << 24);
                    meth->insns          = (const uint16_t *)(cp + 16);
                }
                if (mid < hdr->method_ids_size) dex->resolved_methods[mid] = meth;
            }
        }
        context_add_class(ctx, c);
    }
    return dex;
}

static void dex_log(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    void (*log_fn)(const char *, ...) = (void (*)(const char *, ...))dlsym(RTLD_DEFAULT, "tl_log_line");
    if (log_fn) {
        log_fn("%s", buf);
    } else {
        printf("%s\n", buf);
        fflush(stdout);
    }
}

bool tl_dex_load_apk(tl_dex_context *ctx, const char *apk_path)
{
    tl_zip z;
    char zerr[128] = {0};
    if (!tl_zip_open(&z, apk_path, zerr, sizeof(zerr))) {
        dex_log("dex: could not open %s: %s", apk_path, zerr);
        return false;
    }

    int loaded = 0;
    for (size_t i = 0; i < z.count && ctx->num_dex_files < 16; i++) {
        const char *name = z.entries[i].name;
        if (!strcmp(name, "classes.dex") ||
            (strncmp(name, "classes", 7) == 0 && strstr(name, ".dex"))) {
            const uint8_t *data_ptr = NULL;
            size_t len = 0;
            bool owned = false;
            char err[128] = {0};
            if (tl_zip_data(&z, &z.entries[i], 64 * 1024 * 1024, &data_ptr, &len, &owned, err, sizeof(err))) {
                uint8_t *buf = malloc(len);
                if (buf) {
                    memcpy(buf, data_ptr, len);
                    tl_dex_file *df = parse_dex_buffer(ctx, buf, len);
                    if (df) {
                        ctx->dex_files[ctx->num_dex_files++] = df;
                        loaded++;
                    } else {
                        free(buf);
                    }
                }
                if (owned) free((void *)data_ptr);
            }
        }
    }
    tl_zip_close(&z);

    /* Link superclasses */
    for (int i = 0; i < ctx->num_classes; i++) {
        tl_dex_class *c = ctx->classes[i];
        if (c->super_descriptor) {
            c->super_class = tl_dex_find_class(ctx, c->super_descriptor);
        }
    }

    /* Then lay out instance fields, which needs every superclass in place. */
    for (int i = 0; i < ctx->num_classes; i++) {
        dex_link_class(ctx->classes[i]);
    }

    dex_log("dex: loaded %d DEX file(s), %d class definitions from %s",
            loaded, ctx->num_classes, apk_path);
    return loaded > 0;
}

/* ----------------------------------------------------- Object Allocation */

tl_dex_object *tl_dex_alloc_object(tl_dex_class *clazz)
{
    tl_dex_object *obj = calloc(1, sizeof(*obj));
    obj->clazz = clazz;
    /* The whole inherited layout, not just this class's own fields. */
    int nfields = clazz ? clazz->instance_size : 16;
    if (nfields < 16) nfields = 16;
    obj->fields = calloc(nfields, sizeof(tl_dex_val));
    obj->nfields = (uint32_t)nfields;
    return obj;
}

tl_dex_object *tl_dex_alloc_array(tl_dex_class *elem_class, uint32_t length, uint32_t elem_size)
{
    tl_dex_object *obj = calloc(1, sizeof(*obj));
    obj->clazz = elem_class;
    obj->flags = TL_KIND_ARRAY;
    obj->array.length = length;
    obj->array.elem_size = elem_size ? elem_size : 4;
    obj->array.elements = calloc(length ? length : 1, obj->array.elem_size);
    return obj;
}

tl_dex_object *tl_dex_alloc_string(tl_dex_context *ctx, const char *utf8)
{
    tl_dex_class *s_class = tl_dex_find_class(ctx, "Ljava/lang/String;");
    tl_dex_object *obj = calloc(1, sizeof(*obj));
    obj->clazz = s_class;
    obj->flags = TL_KIND_STRING;
    obj->str_utf8 = utf8 ? strdup(utf8) : strdup("");
    return obj;
}

/* ----------------------------------------------------- Value arithmetic */

/*
 * Dalvik's arithmetic is Java's, and Java's differs from C's in exactly the
 * places a game trips over: integer overflow wraps (in C it is undefined, and
 * the optimiser is allowed to assume it never happens), shifts take their count
 * modulo the operand width, and INT_MIN / -1 is INT_MIN rather than a trap.
 * Everything goes through these helpers so the rules live in one place.
 *
 * Division by zero returns 0. Java throws ArithmeticException there; this
 * interpreter has no exceptions yet, and zero is the least surprising thing to
 * carry on with.
 */
static inline int32_t i_add(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
static inline int32_t i_sub(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }
static inline int32_t i_mul(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
static inline int32_t i_div(int32_t a, int32_t b)
{ if (b == 0) return 0; if (a == INT32_MIN && b == -1) return INT32_MIN; return a / b; }
static inline int32_t i_rem(int32_t a, int32_t b)
{ if (b == 0 || b == -1) return 0; return a % b; }
static inline int32_t i_and(int32_t a, int32_t b) { return a & b; }
static inline int32_t i_or (int32_t a, int32_t b) { return a | b; }
static inline int32_t i_xor(int32_t a, int32_t b) { return a ^ b; }
static inline int32_t i_shl(int32_t a, int32_t b) { return (int32_t)((uint32_t)a << (b & 31)); }
static inline int32_t i_shr(int32_t a, int32_t b) { return a >> (b & 31); }
static inline int32_t i_ushr(int32_t a, int32_t b) { return (int32_t)((uint32_t)a >> (b & 31)); }
static inline int32_t i_rsub(int32_t a, int32_t b) { return i_sub(b, a); }

static inline int64_t l_add(int64_t a, int64_t b) { return (int64_t)((uint64_t)a + (uint64_t)b); }
static inline int64_t l_sub(int64_t a, int64_t b) { return (int64_t)((uint64_t)a - (uint64_t)b); }
static inline int64_t l_mul(int64_t a, int64_t b) { return (int64_t)((uint64_t)a * (uint64_t)b); }
static inline int64_t l_div(int64_t a, int64_t b)
{ if (b == 0) return 0; if (a == INT64_MIN && b == -1) return INT64_MIN; return a / b; }
static inline int64_t l_rem(int64_t a, int64_t b)
{ if (b == 0 || b == -1) return 0; return a % b; }
static inline int64_t l_and(int64_t a, int64_t b) { return a & b; }
static inline int64_t l_or (int64_t a, int64_t b) { return a | b; }
static inline int64_t l_xor(int64_t a, int64_t b) { return a ^ b; }
/* The shift count of a long shift is an INT register, not a wide one. */
static inline int64_t l_shl(int64_t a, int32_t b) { return (int64_t)((uint64_t)a << (b & 63)); }
static inline int64_t l_shr(int64_t a, int32_t b) { return a >> (b & 63); }
static inline int64_t l_ushr(int64_t a, int32_t b) { return (int64_t)((uint64_t)a >> (b & 63)); }

static inline float  f_add(float a, float b) { return a + b; }
static inline float  f_sub(float a, float b) { return a - b; }
static inline float  f_mul(float a, float b) { return a * b; }
static inline float  f_div(float a, float b) { return a / b; }
static inline float  f_rem(float a, float b) { return fmodf(a, b); }
static inline double d_add(double a, double b) { return a + b; }
static inline double d_sub(double a, double b) { return a - b; }
static inline double d_mul(double a, double b) { return a * b; }
static inline double d_div(double a, double b) { return a / b; }
static inline double d_rem(double a, double b) { return fmod(a, b); }

/*
 * Register writes replace the whole 64-bit slot.
 *
 * Writing only `.i` leaves the slot's upper half as whatever was there before,
 * and a register is read back through other union members all the time: an int
 * that was once part of a pointer comes back as part of one. That is how a
 * perfectly good Bitmap call came to be made on the address 0x800000003. A write
 * that sets all of the slot cannot leave anything behind.
 */
static inline void put_i(tl_dex_val *r, int32_t x)  { r->raw64 = (uint32_t)x; }
static inline void put_j(tl_dex_val *r, int64_t x)  { r->j = x; }
static inline void put_f(tl_dex_val *r, float x)    { uint32_t u; memcpy(&u, &x, 4); r->raw64 = u; }
static inline void put_d(tl_dex_val *r, double x)   { r->d = x; }

/* Bytes per element of an array, from the array's own descriptor ("[I",
 * "[[Lfoo;", "[Ljava/lang/String;"): the type decides it, not the allocator. */
static uint32_t dex_array_elem_size(const char *array_desc)
{
    if (!array_desc || array_desc[0] != '[') return 8;
    switch (array_desc[1]) {
        case 'Z': case 'B': return 1;
        case 'S': case 'C': return 2;
        case 'I': case 'F': return 4;
        default:            return 8;      /* J, D, and every reference */
    }
}

/* Say once, not every time: a loop that goes out of range does so every pass. */
#define TL_LOG_FEW(counter, limit, ...) \
    do { static int counter; if (counter++ < (limit)) dex_log(__VA_ARGS__); } while (0)

/* ----------------------------------------------------- Dalvik Interpreter */

/*
 * Run a class's <clinit> the first time anything touches it.
 *
 * Dalvik defers a class's static initialiser until the class is first used, and
 * nothing here was running them at all -- every static field in every class
 * stayed at its zero value for the whole session.
 *
 * In Flappy Bird that reads as a rendering bug. The game's state enum lives in
 * a static field, so it was null; the physics tick takes null as the default
 * branch, which is the death case; the death case sets the flash overlay to
 * full alpha and paints the screen white. One correct title frame, then white,
 * which is exactly what was reported.
 *
 * Lazy rather than eager, deliberately. Initialising all 9,182 classes in the
 * APK at startup runs hundreds of Play Services and AndroidX initialisers that
 * the app never asked for, several of which depend on each other or on native
 * hooks that do not exist here.
 */
static void dex_ensure_class_initialized(tl_dex_context *ctx, tl_dex_class *clazz)
{
    if (!clazz || clazz->initialized) {
        return;
    }

    /*
     * Marked before running, not after. A static initialiser almost always
     * writes its own class's fields, and that write comes straight back here;
     * without the flag already set, the initialiser starts itself again and
     * does not stop. The JVM's own rule has the same shape: a class being
     * initialised on this thread already counts as initialised.
     */
    clazz->initialized = true;

    /* A superclass is initialised before the class extending it. Enums depend
     * on the order: the constants are written by the subclass's initialiser
     * through fields the superclass declares. */
    if (clazz->super_class) {
        dex_ensure_class_initialized(ctx, clazz->super_class);
    }

    /*
     * This class's own <clinit> only. tl_dex_find_method walks up the super
     * chain when a class has no method of that name, which for an initialiser
     * would quietly run the parent's a second time.
     */
    for (int i = 0; i < clazz->num_methods; i++) {
        if (!strcmp(clazz->methods[i].name, "<clinit>")) {
            tl_dex_invoke(ctx, &clazz->methods[i], NULL, 0, NULL);
            return;
        }
    }
}

bool tl_dex_invoke(tl_dex_context *ctx, tl_dex_method *method, tl_dex_val *args, int nargs, tl_dex_val *ret)
{
    if (!method) {
        if (ret) ret->raw64 = 0;
        return false;
    }

    /* Native / Framework dispatch */
    if (method->native_func) {
        tl_dex_native_func fn = (tl_dex_native_func)method->native_func;
        tl_dex_object *this_obj = (nargs > 0) ? args[0].l : NULL;
        return fn(ctx, this_obj, args, nargs, ret);
    }

    if (!method->insns || method->insns_size == 0) {
        /*
         * No bytecode and no shim: a framework method this layer does not
         * implement yet.
         *
         * Say so, once. The per-opcode logging that used to live here was the
         * 10,000x slowdown, but the thing it accidentally also did -- naming
         * every call that went nowhere -- is the most useful output this layer
         * has, because each line is a piece of the framework still to write.
         * Once per method keeps that and costs nothing.
         *
         * And hand back zero. Returning with `ret` untouched left the caller's
         * previous result in place, so the move-result after an unimplemented
         * call quietly picked up whatever an unrelated call had returned last
         * -- an int read back as an object pointer, which is a crash in a
         * shim far from the call that caused it.
         */
        if (!method->unshimmed_logged) {
            method->unshimmed_logged = true;
            const char *owner = method->clazz ? method->clazz->descriptor
                                              : (method->owner ? method->owner : "?");
            dex_log("tl: unshimmed %s->%s%s%s", owner,
                    method->name, method->shorty ? " " : "",
                    method->shorty ? method->shorty : "");
        }
        if (ret) ret->raw64 = 0;
        return true;
    }

    tl_dex_file *dex = method->clazz->dex;
    uint16_t reg_count = method->registers_size;
    if (reg_count < method->ins_size) reg_count = method->ins_size;
    tl_dex_val *v = calloc(reg_count + 8, sizeof(tl_dex_val));

    /* Map incoming parameters to the upper registers [reg_count - ins_size .. reg_count - 1] */
    uint16_t in_start = reg_count - method->ins_size;
    for (int i = 0; i < nargs && (in_start + i) < reg_count; i++) {
        v[in_start + i] = args[i];
    }

    tl_dex_val last_result = {0};
    const uint16_t *insns = method->insns;
    uint32_t pc = 0;
    bool success = true;
#ifdef TL_DEX_TRACE
    int trace_dest = -1;          /* the register the previous instruction wrote */
    int trace_prev_op = -1;       /* ... and what that instruction was */
#endif

    while (pc < method->insns_size) {
        uint16_t inst = insns[pc];
        uint8_t opcode = inst & 0xff;
        uint8_t op_b = (inst >> 8) & 0xff;
#ifdef TL_DEX_TRACE
        if (ctx->trace && trace_dest >= 0) {
            /* The previous instruction's result, as every type it might be: the
             * instruction says which register it wrote, never what is in it. */
            float tf; memcpy(&tf, &v[trace_dest].raw64, 4);
            if (trace_prev_op == 0x1a || trace_prev_op == 0x1b) {
                /* A string constant: show the string, which is the whole point of it. */
                const char *str = tl_dex_string(v[trace_dest].l);
                fprintf(stderr, "          -> v%d = \"%s\"\n", trace_dest, str ? str : "(null)");
            } else {
                fprintf(stderr, "          -> v%d = 0x%llx  (int %d, float %g, double %g)\n", trace_dest,
                        (unsigned long long)v[trace_dest].raw64, v[trace_dest].i, (double)tf, v[trace_dest].d);
            }
        }
        trace_dest = -1;
        trace_prev_op = opcode;
        if (ctx->trace) {
            /* Which register this instruction writes, for the line above next time. */
            if ((opcode >= 0x90 && opcode <= 0xaf) || (opcode >= 0xd8 && opcode <= 0xe2) ||
                (opcode >= 0x44 && opcode <= 0x4a) || opcode == 0x13 || opcode == 0x14 ||
                opcode == 0x15 || opcode == 0x16 || opcode == 0x17 || opcode == 0x18 ||
                opcode == 0x19 || opcode == 0x0a || opcode == 0x0b || opcode == 0x0c ||
                opcode == 0x1a || opcode == 0x1b) {
                trace_dest = op_b;
            } else if ((opcode >= 0xb0 && opcode <= 0xcf) || (opcode >= 0x7b && opcode <= 0x8f) ||
                       (opcode >= 0xd0 && opcode <= 0xd7) || (opcode >= 0x52 && opcode <= 0x58) ||
                       opcode == 0x12 || opcode == 0x01 || opcode == 0x04 || opcode == 0x07) {
                trace_dest = op_b & 0x0f;
            } else if (opcode >= 0x60 && opcode <= 0x66) {
                trace_dest = op_b;
            }
        }
        if (ctx->trace) {
            /* Name what the instruction touches, not just its opcode: a field
             * or method index is meaningless to read, and which field was read
             * is nearly always the whole question. */
            char what[200] = "";
            if (opcode >= 0x52 && opcode <= 0x6d) {
                tl_dex_field *tf = dex_resolve_field(ctx, dex, insns[pc + 1]);
                if (tf) snprintf(what, sizeof(what), " field %s %s slot=%u",
                                 tf->name, tf->type, tf->slot);
            } else if ((opcode >= 0x6e && opcode <= 0x72) || (opcode >= 0x74 && opcode <= 0x78)) {
                tl_dex_method *tm = dex_resolve_method(ctx, dex, insns[pc + 1]);
                if (tm) snprintf(what, sizeof(what), " call %s->%s%s",
                                 tm->clazz ? tm->clazz->descriptor : (tm->owner ? tm->owner : "?"),
                                 tm->name, tm->native_func ? " [shim]" :
                                 (tm->insns ? "" : " [UNIMPLEMENTED]"));
            }
            /* Branches and compares: show what they compared, as raw bits and as
             * the float those bits would be, because the instruction alone says
             * only which way it went. */
            if ((opcode >= 0x32 && opcode <= 0x37) || (opcode >= 0x38 && opcode <= 0x3d) ||
                (opcode >= 0x2d && opcode <= 0x31)) {
                uint32_t ra, rb;
                if (opcode >= 0x38 && opcode <= 0x3d)      { ra = op_b; rb = op_b; }
                else if (opcode >= 0x32 && opcode <= 0x37) { ra = op_b & 0x0f; rb = (op_b >> 4) & 0x0f; }
                else { ra = insns[pc + 1] & 0xff; rb = (insns[pc + 1] >> 8) & 0xff; }
                float fa, fb; memcpy(&fa, &v[ra].raw64, 4); memcpy(&fb, &v[rb].raw64, 4);
                snprintf(what, sizeof(what), " cmp v%u=0x%llx(%g) v%u=0x%llx(%g)",
                         ra, (unsigned long long)v[ra].raw64, (double)fa,
                         rb, (unsigned long long)v[rb].raw64, (double)fb);
            }
            fprintf(stderr, "    %s.%s pc=%u op=0x%02x b=0x%02x%s\n",
                    method->clazz ? method->clazz->descriptor : "?", method->name,
                    pc, opcode, op_b, what);
        }
#endif

        switch (opcode) {
            case 0x00: /* nop */
                pc += 1;
                break;

            case 0x01: /* move vA, vB */
            case 0x07: /* move-object vA, vB */
                v[op_b & 0x0f] = v[(op_b >> 4) & 0x0f];
                pc += 1;
                break;

            case 0x02: /* move/from16 vAA, vBBBB */
            case 0x08: /* move-object/from16 vAA, vBBBB */
                v[op_b] = v[insns[pc + 1]];
                pc += 2;
                break;

            case 0x03: /* move/16 vAAAA, vBBBB */
            case 0x09: /* move-object/16 vAAAA, vBBBB */
                v[insns[pc + 1]] = v[insns[pc + 2]];
                pc += 3;
                break;

            case 0x04: /* move-wide vA, vB */
                v[op_b & 0x0f] = v[(op_b >> 4) & 0x0f];
                v[(op_b & 0x0f) + 1] = v[((op_b >> 4) & 0x0f) + 1];
                pc += 1;
                break;

            case 0x06: /* move-wide/16 vAAAA, vBBBB */
                v[insns[pc + 1]] = v[insns[pc + 2]];
                v[insns[pc + 1] + 1] = v[insns[pc + 2] + 1];
                pc += 3;
                break;

            case 0x05: /* move-wide/from16 vAA, vBBBB */
                v[op_b] = v[insns[pc + 1]];
                v[op_b + 1] = v[insns[pc + 1] + 1];
                pc += 2;
                break;

            case 0x0a: /* move-result vAA */
            case 0x0b: /* move-result-wide vAA */
            case 0x0c: /* move-result-object vAA */
                v[op_b] = last_result;
                pc += 1;
                break;

            case 0x0e: /* return-void */
                goto done;

            case 0x0f: /* return vAA */
            case 0x11: /* return-object vAA */
                if (ret) *ret = v[op_b];
                goto done;

            case 0x10: /* return-wide vAA */
                if (ret) {
                    ret->j = v[op_b].j;
                }
                goto done;

            case 0x12: { /* const/4 vA, #+B */
                uint8_t a = op_b & 0x0f;
                int8_t b = (int8_t)(op_b & 0xf0) >> 4;
                put_i(&v[a], b);
                pc += 1;
                break;
            }

            case 0x13: /* const/16 vAA, #+BBBB */
                put_i(&v[op_b], (int16_t)insns[pc + 1]);
                pc += 2;
                break;

            case 0x14: { /* const vAA, #+BBBBBBBB */
                uint32_t val = (uint32_t)insns[pc + 1] | ((uint32_t)insns[pc + 2] << 16);
                v[op_b].raw64 = (uint32_t)(val);
                pc += 3;
                break;
            }

            case 0x15: /* const-high16 vAA, #+BBBB0000 */
                v[op_b].raw64 = (uint32_t)((uint32_t)insns[pc + 1] << 16);
                pc += 2;
                break;

            case 0x16: /* const-wide/16 vAA, #+BBBB */
                v[op_b].j = (int16_t)insns[pc + 1];
                pc += 2;
                break;

            case 0x17: { /* const-wide/32 vAA, #+BBBBBBBB */
                int32_t val = (int32_t)((uint32_t)insns[pc + 1] | ((uint32_t)insns[pc + 2] << 16));
                v[op_b].j = val;
                pc += 3;
                break;
            }

            case 0x18: { /* const-wide vAA, #+BBBBBBBBBBBBBBBB */
                uint64_t w0 = insns[pc + 1];
                uint64_t w1 = insns[pc + 2];
                uint64_t w2 = insns[pc + 3];
                uint64_t w3 = insns[pc + 4];
                v[op_b].raw64 = w0 | (w1 << 16) | (w2 << 32) | (w3 << 48);
                pc += 5;
                break;
            }

            case 0x19: /* const-wide/high16 vAA, #+BBBB000000000000 */
                v[op_b].raw64 = (uint64_t)insns[pc + 1] << 48;
                pc += 2;
                break;

            case 0x1a: { /* const-string vAA, string@BBBB */
                const char *s = dex_get_string(dex, insns[pc + 1]);
                v[op_b].l = tl_dex_alloc_string(ctx, s);
                pc += 2;
                break;
            }

            case 0x1b: { /* const-string/jumbo vAA, string@BBBBBBBB */
                /* The 32-bit index form, which a dex with more than 65,535 strings
                 * needs for every string past the first 65,535. Large apps have
                 * far more than that. */
                uint32_t sidx = (uint32_t)insns[pc + 1] | ((uint32_t)insns[pc + 2] << 16);
                v[op_b].l = tl_dex_alloc_string(ctx, dex_get_string(dex, sidx));
                pc += 3;
                break;
            }

            case 0x1c: { /* const-class vAA, type@BBBB */
                const char *tdesc = dex_get_type(dex, insns[pc + 1]);
                tl_dex_class *cl = tl_dex_find_class(ctx, tdesc);
                v[op_b].l = (tl_dex_object *)cl;
                pc += 2;
                break;
            }

            case 0x1d: /* monitor-enter */
            case 0x1e: /* monitor-exit */
            case 0x1f: /* check-cast */
                pc += (opcode == 0x1f) ? 2 : 1;
                break;

            case 0x20: { /* instance-of vA, vB, type@CCCC */
                uint8_t a = op_b & 0x0f;
                uint8_t b = (op_b >> 4) & 0x0f;
                put_i(&v[a], (v[b].l != NULL) ? 1 : 0);
                pc += 2;
                break;
            }

            case 0x21: { /* array-length vA, vB */
                uint8_t a = op_b & 0x0f;
                uint8_t b = (op_b >> 4) & 0x0f;
                { tl_dex_object *arr = tl_dex_array(v[b].l);
                  put_i(&v[a], arr ? (int32_t)arr->array.length : 0); }
                pc += 1;
                break;
            }

            case 0x22: { /* new-instance vAA, type@BBBB */
                const char *tdesc = dex_get_type(dex, insns[pc + 1]);
                tl_dex_class *cl = tl_dex_find_class(ctx, tdesc);
                dex_ensure_class_initialized(ctx, cl);
                v[op_b].l = tl_dex_alloc_object(cl);
                pc += 2;
                break;
            }

            case 0x23: { /* new-array vA, vB, type@CCCC */
                uint8_t a = op_b & 0x0f;
                uint8_t b = (op_b >> 4) & 0x0f;
                const char *tdesc = dex_get_type(dex, insns[pc + 1]);
                tl_dex_class *cl = tl_dex_find_class(ctx, tdesc);
                /* A negative length is a NegativeArraySizeException in Java; with
                 * no exceptions yet it is an empty array rather than a huge one. */
                uint32_t len = (v[b].i > 0) ? (uint32_t)v[b].i : 0;
                v[a].raw64 = 0;
                v[a].l = tl_dex_alloc_array(cl, len, dex_array_elem_size(tdesc));
                pc += 2;
                break;
            }

            case 0x26: { /* fill-array-data vAA, +BBBBBBBB */
                /* The table: ident 0x0300, element width, then a 32-bit count
                 * (low half first) and the bytes. The count's high half is
                 * shifted by SIXTEEN; this used to shift it by eight. Copied
                 * only when the table's width is the array's own element size --
                 * a mismatch means the array was allocated as something else,
                 * and copying anyway scrambles it. */
                int32_t rel = (int32_t)((uint32_t)insns[pc + 1] | ((uint32_t)insns[pc + 2] << 16));
                int64_t at = (int64_t)pc + rel;
                tl_dex_object *arr = tl_dex_array(v[op_b].l);
                if (arr && at >= 0 && at + 4 <= (int64_t)method->insns_size &&
                    insns[at] == 0x0300) {
                    uint32_t width = insns[at + 1];
                    uint32_t count = (uint32_t)insns[at + 2] | ((uint32_t)insns[at + 3] << 16);
                    uint64_t bytes = (uint64_t)count * width;
                    if (width == arr->array.elem_size && count <= arr->array.length &&
                        (at + 4) * 2 + (int64_t)bytes <= (int64_t)method->insns_size * 2) {
                        memcpy(arr->array.elements, insns + at + 4, (size_t)bytes);
                    } else {
                        TL_LOG_FEW(fill_bad, 12, "tl: fill-array-data refused in %s.%s pc=%u (width %u vs elem %u, count %u vs len %u)",
                                   method->clazz ? method->clazz->descriptor : "?", method->name, pc,
                                   width, arr->array.elem_size, count, arr->array.length);
                    }
                }
                pc += 3;
                break;
            }

            case 0x24: case 0x25: { /* filled-new-array, filled-new-array/range */
                /* Builds the array and leaves it for the move-result-object that
                 * follows, as an invoke does. With no implementation the result
                 * register kept whatever it held. */
                const char *tdesc = dex_get_type(dex, insns[pc + 1]);
                uint32_t count, first = 0;
                uint8_t reg[5] = {0};
                if (opcode == 0x24) {
                    uint16_t arg_regs = insns[pc + 2];
                    count = (op_b >> 4) & 0x0f;
                    if (count > 5) count = 5;
                    reg[0] = arg_regs & 0x0f;         reg[1] = (arg_regs >> 4) & 0x0f;
                    reg[2] = (arg_regs >> 8) & 0x0f;  reg[3] = (arg_regs >> 12) & 0x0f;
                    reg[4] = op_b & 0x0f;
                } else {
                    count = op_b;
                    first = insns[pc + 2];
                }
                uint32_t es = dex_array_elem_size(tdesc);
                tl_dex_object *arr = tl_dex_alloc_array(tl_dex_find_class(ctx, tdesc), count, es);
                if (arr && arr->array.elements) {
                    for (uint32_t i = 0; i < count; i++) {
                        const tl_dex_val src = v[opcode == 0x24 ? reg[i] : first + i];
                        uint8_t *at = (uint8_t *)arr->array.elements + (size_t)i * es;
                        if (es == 1)      { at[0] = (uint8_t)src.i; }
                        else if (es == 2) { uint16_t u = (uint16_t)src.i; memcpy(at, &u, 2); }
                        else if (es == 4) { uint32_t u = src.raw32; memcpy(at, &u, 4); }
                        else              { memcpy(at, &src.raw64, 8); }
                    }
                }
                last_result.raw64 = 0;
                last_result.l = arr;
                pc += 3;
                break;
            }

            case 0x28: /* goto +AA */
                pc += (int8_t)op_b;
                break;

            case 0x29: /* goto/16 +AAAA */
                pc += (int16_t)insns[pc + 1];
                break;

            case 0x2a: /* goto/32 +AAAAAAAA */
                pc += (int32_t)((uint32_t)insns[pc + 1] | ((uint32_t)insns[pc + 2] << 16));
                break;

            /*
             * packed-switch and sparse-switch.
             *
             * Neither of these existed, and the default case stepped over them
             * one code unit at a time -- so a switch did not fall through to its
             * default, it fell into its own payload table and ran the offsets as
             * if they were instructions. Every `switch` in every app, the game's
             * whole state machine included.
             *
             * The instruction holds a signed offset, from itself, to a table the
             * compiler placed after the method body. A packed table is one
             * first key and a run of targets; a sparse one is sorted keys and
             * then targets. Targets are offsets from the switch instruction,
             * and no match means falling through to the next instruction.
             */
            case 0x2b: { /* packed-switch vAA, +BBBBBBBB */
                int32_t rel = (int32_t)((uint32_t)insns[pc + 1] | ((uint32_t)insns[pc + 2] << 16));
                int64_t at = (int64_t)pc + rel;
                int32_t step = 3;
                if (at >= 0 && at + 4 <= (int64_t)method->insns_size && insns[at] == 0x0100) {
                    uint32_t size = insns[at + 1];
                    int32_t first = (int32_t)((uint32_t)insns[at + 2] | ((uint32_t)insns[at + 3] << 16));
                    int64_t k = (int64_t)v[op_b].i - first;
                    if (k >= 0 && k < (int64_t)size &&
                        at + 4 + (k + 1) * 2 <= (int64_t)method->insns_size) {
                        const uint16_t *t = insns + at + 4 + k * 2;
                        step = (int32_t)((uint32_t)t[0] | ((uint32_t)t[1] << 16));
                    }
                }
                pc += (uint32_t)step;
                break;
            }

            case 0x2c: { /* sparse-switch vAA, +BBBBBBBB */
                int32_t rel = (int32_t)((uint32_t)insns[pc + 1] | ((uint32_t)insns[pc + 2] << 16));
                int64_t at = (int64_t)pc + rel;
                int32_t step = 3;
                if (at >= 0 && at + 2 <= (int64_t)method->insns_size && insns[at] == 0x0200) {
                    uint32_t size = insns[at + 1];
                    if (at + 2 + (int64_t)size * 4 <= (int64_t)method->insns_size) {
                        const uint16_t *keys = insns + at + 2;
                        const uint16_t *tgts = keys + size * 2;
                        int32_t key = v[op_b].i;
                        for (uint32_t i = 0; i < size; i++) {
                            int32_t kv = (int32_t)((uint32_t)keys[i * 2] | ((uint32_t)keys[i * 2 + 1] << 16));
                            if (kv == key) {
                                step = (int32_t)((uint32_t)tgts[i * 2] | ((uint32_t)tgts[i * 2 + 1] << 16));
                                break;
                            }
                        }
                    }
                }
                pc += (uint32_t)step;
                break;
            }

            case 0x2d: /* cmpl-float vAA, vBB, vCC */
            case 0x2e: { /* cmpg-float vAA, vBB, vCC */
                uint16_t regs = insns[pc + 1];
                uint8_t b = regs & 0xff;
                uint8_t c = (regs >> 8) & 0xff;
                float fb = v[b].f;
                float fc = v[c].f;
                if (isnan(fb) || isnan(fc)) put_i(&v[op_b], (opcode == 0x2d) ? -1 : 1);
                else if (fb > fc) put_i(&v[op_b], 1);
                else if (fb < fc) put_i(&v[op_b], -1);
                else put_i(&v[op_b], 0);
                pc += 2;
                break;
            }

            case 0x2f: /* cmpl-double vAA, vBB, vCC */
            case 0x30: { /* cmpg-double vAA, vBB, vCC */
                uint16_t regs = insns[pc + 1];
                uint8_t b = regs & 0xff;
                uint8_t c = (regs >> 8) & 0xff;
                double db = v[b].d;
                double dc = v[c].d;
                if (isnan(db) || isnan(dc)) put_i(&v[op_b], (opcode == 0x2f) ? -1 : 1);
                else if (db > dc) put_i(&v[op_b], 1);
                else if (db < dc) put_i(&v[op_b], -1);
                else put_i(&v[op_b], 0);
                pc += 2;
                break;
            }

            case 0x31: { /* cmp-long vAA, vBB, vCC */
                uint16_t regs = insns[pc + 1];
                uint8_t b = regs & 0xff;
                uint8_t c = (regs >> 8) & 0xff;
                int64_t jb = v[b].j;
                int64_t jc = v[c].j;
                if (jb > jc) put_i(&v[op_b], 1);
                else if (jb < jc) put_i(&v[op_b], -1);
                else put_i(&v[op_b], 0);
                pc += 2;
                break;
            }

            case 0x32: /* if-eq vA, vB, +CCCC */
            case 0x33: /* if-ne vA, vB, +CCCC */
            case 0x34: /* if-lt vA, vB, +CCCC */
            case 0x35: /* if-ge vA, vB, +CCCC */
            case 0x36: /* if-gt vA, vB, +CCCC */
            case 0x37: { /* if-le vA, vB, +CCCC */
                uint8_t a = op_b & 0x0f;
                uint8_t b = (op_b >> 4) & 0x0f;
                int16_t target = (int16_t)insns[pc + 1];
                bool take = false;
                if (opcode == 0x32) take = (v[a].i == v[b].i);
                else if (opcode == 0x33) take = (v[a].i != v[b].i);
                else if (opcode == 0x34) take = (v[a].i < v[b].i);
                else if (opcode == 0x35) take = (v[a].i >= v[b].i);
                else if (opcode == 0x36) take = (v[a].i > v[b].i);
                else if (opcode == 0x37) take = (v[a].i <= v[b].i);
                if (take) pc += target;
                else pc += 2;
                break;
            }

            case 0x38: /* if-eqz vAA, +BBBB */
            case 0x39: /* if-nez vAA, +BBBB */
            case 0x3a: /* if-ltz vAA, +BBBB */
            case 0x3b: /* if-gez vAA, +BBBB */
            case 0x3c: /* if-gtz vAA, +BBBB */
            case 0x3d: { /* if-lez vAA, +BBBB */
                int16_t target = (int16_t)insns[pc + 1];
                bool take = false;
                if (opcode == 0x38) take = (v[op_b].i == 0);
                else if (opcode == 0x39) take = (v[op_b].i != 0);
                else if (opcode == 0x3a) take = (v[op_b].i < 0);
                else if (opcode == 0x3b) take = (v[op_b].i >= 0);
                else if (opcode == 0x3c) take = (v[op_b].i > 0);
                else if (opcode == 0x3d) take = (v[op_b].i <= 0);
                if (take) pc += target;
                else pc += 2;
                break;
            }

            /*
             * Array reads and writes.
             *
             * Each opcode names the width it moves -- a boolean or byte is one
             * byte, a char or short two, an int or float four, a long, double or
             * reference eight -- and an array knows its own element size, so the
             * two are checked against each other before anything is touched.
             *
             * A read that cannot happen leaves ZERO in the destination. It used
             * to leave the destination alone, and compilers routinely load an
             * element into the very register that held the array: when the index
             * was out of range, the "element" was the array, and getWidth() was
             * then called on it.
             */
            case 0x44: case 0x45: case 0x46: case 0x47: case 0x48: case 0x49: case 0x4a: {
                static const uint8_t stride[7] = { 4, 8, 8, 1, 1, 2, 2 };
                uint16_t regs = insns[pc + 1];
                tl_dex_object *arr = tl_dex_array(v[regs & 0xff].l);
                int32_t idx = v[(regs >> 8) & 0xff].i;
                uint32_t es = stride[opcode - 0x44];
                tl_dex_val *dst = &v[op_b];
                if (arr && arr->array.elements && idx >= 0 && (uint32_t)idx < arr->array.length &&
                    arr->array.elem_size == es) {
                    const uint8_t *at = (const uint8_t *)arr->array.elements + (size_t)idx * es;
                    switch (opcode) {
                        case 0x44: { uint32_t u; memcpy(&u, at, 4); dst->raw64 = u; break; }
                        case 0x45: case 0x46: memcpy(&dst->raw64, at, 8); break;
                        case 0x47: dst->raw64 = at[0]; break;
                        case 0x48: dst->raw64 = (uint32_t)(int32_t)(int8_t)at[0]; break;
                        case 0x49: { uint16_t u; memcpy(&u, at, 2); dst->raw64 = u; break; }
                        default:   { int16_t sv; memcpy(&sv, at, 2); dst->raw64 = (uint32_t)(int32_t)sv; break; }
                    }
                } else {
                    dst->raw64 = 0;
                    TL_LOG_FEW(aget_bad, 24, "tl: array read failed in %s.%s pc=%u (array=%s idx=%d len=%d elem=%u want=%u)",
                               method->clazz ? method->clazz->descriptor : "?", method->name, pc,
                               arr ? "ok" : "null", idx, arr ? (int)arr->array.length : -1,
                               arr ? arr->array.elem_size : 0, es);
                }
                pc += 2;
                break;
            }

            case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f: case 0x50: case 0x51: {
                static const uint8_t stride[7] = { 4, 8, 8, 1, 1, 2, 2 };
                uint16_t regs = insns[pc + 1];
                tl_dex_object *arr = tl_dex_array(v[regs & 0xff].l);
                int32_t idx = v[(regs >> 8) & 0xff].i;
                uint32_t es = stride[opcode - 0x4b];
                const tl_dex_val val = v[op_b];
                if (arr && arr->array.elements && idx >= 0 && (uint32_t)idx < arr->array.length &&
                    arr->array.elem_size == es) {
                    uint8_t *at = (uint8_t *)arr->array.elements + (size_t)idx * es;
                    switch (opcode) {
                        case 0x4b: { uint32_t u = val.raw32; memcpy(at, &u, 4); break; }
                        case 0x4c: case 0x4d: memcpy(at, &val.raw64, 8); break;
                        case 0x4e: case 0x4f: at[0] = (uint8_t)val.i; break;
                        default:   { uint16_t u = (uint16_t)val.i; memcpy(at, &u, 2); break; }
                    }
                } else {
                    TL_LOG_FEW(aput_bad, 24, "tl: array write failed in %s.%s pc=%u (array=%s idx=%d len=%d elem=%u want=%u)",
                               method->clazz ? method->clazz->descriptor : "?", method->name, pc,
                               arr ? "ok" : "null", idx, arr ? (int)arr->array.length : -1,
                               arr ? arr->array.elem_size : 0, es);
                }
                pc += 2;
                break;
            }

            case 0x52: /* iget */
            case 0x53: /* iget-wide */
            case 0x54: /* iget-object */
            case 0x55: /* iget-boolean */
            case 0x56: /* iget-byte */
            case 0x57: /* iget-char */
            case 0x58: { /* iget-short */
                uint8_t a = op_b & 0x0f;
                uint8_t b = (op_b >> 4) & 0x0f;
                tl_dex_field *f = dex_resolve_field(ctx, dex, insns[pc + 1]);
                tl_dex_object *obj = v[b].l;
                if (obj && f && f->slot == UINT32_MAX) {
                    /* Not a field of an app class: a framework object's own. */
                    tl_dex_val out; out.raw64 = 0;
                    if (!tl_framework_field_get(ctx, obj, f, &out)) dex_note_unresolved_field(f, "read");
                    v[a] = out;
                } else if (obj && obj->nfields && f && f->slot < obj->nfields) {
                    v[a] = obj->fields[f->slot];
                } else {
                    v[a].raw64 = 0;
                }
                pc += 2;
                break;
            }

            case 0x59: /* iput */
            case 0x5a: /* iput-wide */
            case 0x5b: /* iput-object */
            case 0x5c: /* iput-boolean */
            case 0x5d: /* iput-byte */
            case 0x5e: /* iput-char */
            case 0x5f: { /* iput-short */
                uint8_t a = op_b & 0x0f;
                uint8_t b = (op_b >> 4) & 0x0f;
                tl_dex_field *f = dex_resolve_field(ctx, dex, insns[pc + 1]);
                tl_dex_object *obj = v[b].l;
                if (obj && f && f->slot == UINT32_MAX) {
                    if (!tl_framework_field_set(ctx, obj, f, v[a])) dex_note_unresolved_field(f, "write");
                } else if (obj && obj->nfields && f && f->slot < obj->nfields) {
                    obj->fields[f->slot] = v[a];
#ifdef TL_DEX_TRACE
                    if (ctx->trace)
                        fprintf(stderr, "        iput %s.%s slot=%u <- 0x%llx\n",
                                f->clazz ? f->clazz->descriptor : "?", f->name, f->slot,
                                (unsigned long long)v[a].raw64);
#endif
                }
                pc += 2;
                break;
            }

            case 0x60: /* sget */
            case 0x61: /* sget-wide */
            case 0x62: /* sget-object */
            case 0x63: /* sget-boolean */
            case 0x64: /* sget-byte */
            case 0x65: /* sget-char */
            case 0x66: { /* sget-short */
                tl_dex_field *f = dex_resolve_field(ctx, dex, insns[pc + 1]);
                if (f) dex_ensure_class_initialized(ctx, f->clazz);
                if (f && f->slot == UINT32_MAX) {
                    tl_dex_val out; out.raw64 = 0;
                    if (!tl_framework_static_get(ctx, f, &out)) dex_note_unresolved_field(f, "static read");
                    v[op_b] = out;
                } else if (f && f->clazz && f->clazz->static_values &&
                    f->slot < (uint32_t)f->clazz->num_static_fields) {
                    v[op_b] = f->clazz->static_values[f->slot];
                } else {
                    v[op_b].raw64 = 0;
                }
                pc += 2;
                break;
            }

            case 0x67: /* sput */
            case 0x68: /* sput-wide */
            case 0x69: /* sput-object */
            case 0x6a: /* sput-boolean */
            case 0x6b: /* sput-byte */
            case 0x6c: /* sput-char */
            case 0x6d: { /* sput-short */
                tl_dex_field *f = dex_resolve_field(ctx, dex, insns[pc + 1]);
                if (f) dex_ensure_class_initialized(ctx, f->clazz);
                if (f && f->clazz && f->clazz->static_values &&
                    f->slot < (uint32_t)f->clazz->num_static_fields) {
                    f->clazz->static_values[f->slot] = v[op_b];
                }
                pc += 2;
                break;
            }

            case 0x6e: /* invoke-virtual */
            case 0x6f: /* invoke-super */
            case 0x70: /* invoke-direct */
            case 0x71: /* invoke-static */
            case 0x72: { /* invoke-interface */
                uint8_t count = (op_b >> 4) & 0x0f;
                uint16_t m_idx = insns[pc + 1];
                uint16_t arg_regs = insns[pc + 2];
                tl_dex_method *target = dex_resolve_method(ctx, dex, m_idx);
                /* Calling a static method is a use of its class; calling an
                 * instance method is not -- whatever produced the instance
                 * initialised it already. */
                if (opcode == 0x71 && target) {
                    dex_ensure_class_initialized(ctx, target->clazz);
                }

                uint8_t reg_list[5];
                reg_list[0] = arg_regs & 0x0f;
                reg_list[1] = (arg_regs >> 4) & 0x0f;
                reg_list[2] = (arg_regs >> 8) & 0x0f;
                reg_list[3] = (arg_regs >> 12) & 0x0f;
                reg_list[4] = op_b & 0x0f;

                tl_dex_val call_args[5] = {0};
                for (int i = 0; i < count && i < 5; i++) {
                    call_args[i] = v[reg_list[i]];
                }

                /* Cleared first, always. move-result reads whatever this call
                 * leaves behind, and any callee that returns early without
                 * writing it -- a shim given a null argument, say -- would
                 * otherwise hand the caller the PREVIOUS call's result. That is
                 * how an int from one call became the "Bitmap" of the next. */
#ifdef TL_DEX_TRACE
                if (ctx->trace) {
                    fprintf(stderr, "        args:");
                    for (int i = 0; i < count && i < 5; i++)
                        fprintf(stderr, " v%u=0x%llx", reg_list[i], (unsigned long long)call_args[i].raw64);
                    fprintf(stderr, "\n");
                }
#endif
                last_result.raw64 = 0;
                tl_dex_invoke(ctx, target, call_args, count, &last_result);
                pc += 3;
                break;
            }

            case 0x74: /* invoke-virtual/range */
            case 0x75: /* invoke-super/range */
            case 0x76: /* invoke-direct/range */
            case 0x77: /* invoke-static/range */
            case 0x78: { /* invoke-interface/range */
                uint8_t count = op_b;
                uint16_t m_idx = insns[pc + 1];
                uint16_t first_reg = insns[pc + 2];
                tl_dex_method *target = dex_resolve_method(ctx, dex, m_idx);
                if (opcode == 0x77 && target) {
                    dex_ensure_class_initialized(ctx, target->clazz);
                }

                tl_dex_val *call_args = calloc(count ? count : 1, sizeof(tl_dex_val));
                for (int i = 0; i < count; i++) {
                    call_args[i] = v[first_reg + i];
                }
                last_result.raw64 = 0;   /* see the non-range form above */
                tl_dex_invoke(ctx, target, call_args, count, &last_result);
                free(call_args);
                pc += 3;
                break;
            }

            /* Unary arithmetic & Conversions */
            case 0x7b: /* neg-int */    put_i(&v[op_b & 0x0f], -v[(op_b >> 4) & 0x0f].i); pc += 1; break;
            case 0x7c: /* not-int */    put_i(&v[op_b & 0x0f], ~v[(op_b >> 4) & 0x0f].i); pc += 1; break;
            case 0x7d: /* neg-long */   v[op_b & 0x0f].j = -v[(op_b >> 4) & 0x0f].j; pc += 1; break;
            case 0x7e: /* not-long */   v[op_b & 0x0f].j = ~v[(op_b >> 4) & 0x0f].j; pc += 1; break;
            case 0x7f: /* neg-float */  put_f(&v[op_b & 0x0f], -v[(op_b >> 4) & 0x0f].f); pc += 1; break;
            case 0x80: /* neg-double */ v[op_b & 0x0f].d = -v[(op_b >> 4) & 0x0f].d; pc += 1; break;
            case 0x81: /* int-to-long */   v[op_b & 0x0f].j = v[(op_b >> 4) & 0x0f].i; pc += 1; break;
            case 0x82: /* int-to-float */  put_f(&v[op_b & 0x0f], (float)v[(op_b >> 4) & 0x0f].i); pc += 1; break;
            case 0x83: /* int-to-double */ v[op_b & 0x0f].d = (double)v[(op_b >> 4) & 0x0f].i; pc += 1; break;
            case 0x84: /* long-to-int */   put_i(&v[op_b & 0x0f], (int32_t)v[(op_b >> 4) & 0x0f].j); pc += 1; break;
            case 0x85: /* long-to-float */ put_f(&v[op_b & 0x0f], (float)v[(op_b >> 4) & 0x0f].j); pc += 1; break;
            case 0x86: /* long-to-double */v[op_b & 0x0f].d = (double)v[(op_b >> 4) & 0x0f].j; pc += 1; break;
            case 0x87: /* float-to-int */  put_i(&v[op_b & 0x0f], (int32_t)v[(op_b >> 4) & 0x0f].f); pc += 1; break;
            case 0x88: /* float-to-long */ v[op_b & 0x0f].j = (int64_t)v[(op_b >> 4) & 0x0f].f; pc += 1; break;
            case 0x89: /* float-to-double */v[op_b & 0x0f].d = (double)v[(op_b >> 4) & 0x0f].f; pc += 1; break;
            case 0x8a: /* double-to-int */ put_i(&v[op_b & 0x0f], (int32_t)v[(op_b >> 4) & 0x0f].d); pc += 1; break;
            case 0x8b: /* double-to-long */v[op_b & 0x0f].j = (int64_t)v[(op_b >> 4) & 0x0f].d; pc += 1; break;
            case 0x8c: /* double-to-float */put_f(&v[op_b & 0x0f], (float)v[(op_b >> 4) & 0x0f].d); pc += 1; break;
            case 0x8d: /* int-to-byte */  put_i(&v[op_b & 0x0f], (int8_t)v[(op_b >> 4) & 0x0f].i);   pc += 1; break;
            case 0x8e: /* int-to-char */  put_i(&v[op_b & 0x0f], (uint16_t)v[(op_b >> 4) & 0x0f].i); pc += 1; break;
            case 0x8f: /* int-to-short */ put_i(&v[op_b & 0x0f], (int16_t)v[(op_b >> 4) & 0x0f].i);  pc += 1; break;

            /* ---- Binary arithmetic: generated, one case per opcode ---- */
            case 0x90: { uint16_t r_ = insns[pc + 1]; put_i(&v[op_b], i_add(v[r_ & 0xff].i, v[(r_ >> 8) & 0xff].i)); pc += 2; break; }
            case 0x91: { uint16_t r_ = insns[pc + 1]; put_i(&v[op_b], i_sub(v[r_ & 0xff].i, v[(r_ >> 8) & 0xff].i)); pc += 2; break; }
            case 0x92: { uint16_t r_ = insns[pc + 1]; put_i(&v[op_b], i_mul(v[r_ & 0xff].i, v[(r_ >> 8) & 0xff].i)); pc += 2; break; }
            case 0x93: { uint16_t r_ = insns[pc + 1]; put_i(&v[op_b], i_div(v[r_ & 0xff].i, v[(r_ >> 8) & 0xff].i)); pc += 2; break; }
            case 0x94: { uint16_t r_ = insns[pc + 1]; put_i(&v[op_b], i_rem(v[r_ & 0xff].i, v[(r_ >> 8) & 0xff].i)); pc += 2; break; }
            case 0x95: { uint16_t r_ = insns[pc + 1]; put_i(&v[op_b], i_and(v[r_ & 0xff].i, v[(r_ >> 8) & 0xff].i)); pc += 2; break; }
            case 0x96: { uint16_t r_ = insns[pc + 1]; put_i(&v[op_b], i_or(v[r_ & 0xff].i, v[(r_ >> 8) & 0xff].i)); pc += 2; break; }
            case 0x97: { uint16_t r_ = insns[pc + 1]; put_i(&v[op_b], i_xor(v[r_ & 0xff].i, v[(r_ >> 8) & 0xff].i)); pc += 2; break; }
            case 0x98: { uint16_t r_ = insns[pc + 1]; put_i(&v[op_b], i_shl(v[r_ & 0xff].i, v[(r_ >> 8) & 0xff].i)); pc += 2; break; }
            case 0x99: { uint16_t r_ = insns[pc + 1]; put_i(&v[op_b], i_shr(v[r_ & 0xff].i, v[(r_ >> 8) & 0xff].i)); pc += 2; break; }
            case 0x9a: { uint16_t r_ = insns[pc + 1]; put_i(&v[op_b], i_ushr(v[r_ & 0xff].i, v[(r_ >> 8) & 0xff].i)); pc += 2; break; }
            case 0x9b: { uint16_t r_ = insns[pc + 1]; put_j(&v[op_b], l_add(v[r_ & 0xff].j, v[(r_ >> 8) & 0xff].j)); pc += 2; break; }
            case 0x9c: { uint16_t r_ = insns[pc + 1]; put_j(&v[op_b], l_sub(v[r_ & 0xff].j, v[(r_ >> 8) & 0xff].j)); pc += 2; break; }
            case 0x9d: { uint16_t r_ = insns[pc + 1]; put_j(&v[op_b], l_mul(v[r_ & 0xff].j, v[(r_ >> 8) & 0xff].j)); pc += 2; break; }
            case 0x9e: { uint16_t r_ = insns[pc + 1]; put_j(&v[op_b], l_div(v[r_ & 0xff].j, v[(r_ >> 8) & 0xff].j)); pc += 2; break; }
            case 0x9f: { uint16_t r_ = insns[pc + 1]; put_j(&v[op_b], l_rem(v[r_ & 0xff].j, v[(r_ >> 8) & 0xff].j)); pc += 2; break; }
            case 0xa0: { uint16_t r_ = insns[pc + 1]; put_j(&v[op_b], l_and(v[r_ & 0xff].j, v[(r_ >> 8) & 0xff].j)); pc += 2; break; }
            case 0xa1: { uint16_t r_ = insns[pc + 1]; put_j(&v[op_b], l_or(v[r_ & 0xff].j, v[(r_ >> 8) & 0xff].j)); pc += 2; break; }
            case 0xa2: { uint16_t r_ = insns[pc + 1]; put_j(&v[op_b], l_xor(v[r_ & 0xff].j, v[(r_ >> 8) & 0xff].j)); pc += 2; break; }
            case 0xa3: { uint16_t r_ = insns[pc + 1]; put_j(&v[op_b], l_shl(v[r_ & 0xff].j, v[(r_ >> 8) & 0xff].i)); pc += 2; break; }
            case 0xa4: { uint16_t r_ = insns[pc + 1]; put_j(&v[op_b], l_shr(v[r_ & 0xff].j, v[(r_ >> 8) & 0xff].i)); pc += 2; break; }
            case 0xa5: { uint16_t r_ = insns[pc + 1]; put_j(&v[op_b], l_ushr(v[r_ & 0xff].j, v[(r_ >> 8) & 0xff].i)); pc += 2; break; }
            case 0xa6: { uint16_t r_ = insns[pc + 1]; put_f(&v[op_b], f_add(v[r_ & 0xff].f, v[(r_ >> 8) & 0xff].f)); pc += 2; break; }
            case 0xa7: { uint16_t r_ = insns[pc + 1]; put_f(&v[op_b], f_sub(v[r_ & 0xff].f, v[(r_ >> 8) & 0xff].f)); pc += 2; break; }
            case 0xa8: { uint16_t r_ = insns[pc + 1]; put_f(&v[op_b], f_mul(v[r_ & 0xff].f, v[(r_ >> 8) & 0xff].f)); pc += 2; break; }
            case 0xa9: { uint16_t r_ = insns[pc + 1]; put_f(&v[op_b], f_div(v[r_ & 0xff].f, v[(r_ >> 8) & 0xff].f)); pc += 2; break; }
            case 0xaa: { uint16_t r_ = insns[pc + 1]; put_f(&v[op_b], f_rem(v[r_ & 0xff].f, v[(r_ >> 8) & 0xff].f)); pc += 2; break; }
            case 0xab: { uint16_t r_ = insns[pc + 1]; put_d(&v[op_b], d_add(v[r_ & 0xff].d, v[(r_ >> 8) & 0xff].d)); pc += 2; break; }
            case 0xac: { uint16_t r_ = insns[pc + 1]; put_d(&v[op_b], d_sub(v[r_ & 0xff].d, v[(r_ >> 8) & 0xff].d)); pc += 2; break; }
            case 0xad: { uint16_t r_ = insns[pc + 1]; put_d(&v[op_b], d_mul(v[r_ & 0xff].d, v[(r_ >> 8) & 0xff].d)); pc += 2; break; }
            case 0xae: { uint16_t r_ = insns[pc + 1]; put_d(&v[op_b], d_div(v[r_ & 0xff].d, v[(r_ >> 8) & 0xff].d)); pc += 2; break; }
            case 0xaf: { uint16_t r_ = insns[pc + 1]; put_d(&v[op_b], d_rem(v[r_ & 0xff].d, v[(r_ >> 8) & 0xff].d)); pc += 2; break; }

            /* ---- /2addr forms ---- */
            case 0xb0: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_i(d_, i_add(d_->i, v[(op_b >> 4) & 0x0f].i)); pc += 1; break; }
            case 0xb1: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_i(d_, i_sub(d_->i, v[(op_b >> 4) & 0x0f].i)); pc += 1; break; }
            case 0xb2: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_i(d_, i_mul(d_->i, v[(op_b >> 4) & 0x0f].i)); pc += 1; break; }
            case 0xb3: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_i(d_, i_div(d_->i, v[(op_b >> 4) & 0x0f].i)); pc += 1; break; }
            case 0xb4: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_i(d_, i_rem(d_->i, v[(op_b >> 4) & 0x0f].i)); pc += 1; break; }
            case 0xb5: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_i(d_, i_and(d_->i, v[(op_b >> 4) & 0x0f].i)); pc += 1; break; }
            case 0xb6: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_i(d_, i_or(d_->i, v[(op_b >> 4) & 0x0f].i)); pc += 1; break; }
            case 0xb7: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_i(d_, i_xor(d_->i, v[(op_b >> 4) & 0x0f].i)); pc += 1; break; }
            case 0xb8: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_i(d_, i_shl(d_->i, v[(op_b >> 4) & 0x0f].i)); pc += 1; break; }
            case 0xb9: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_i(d_, i_shr(d_->i, v[(op_b >> 4) & 0x0f].i)); pc += 1; break; }
            case 0xba: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_i(d_, i_ushr(d_->i, v[(op_b >> 4) & 0x0f].i)); pc += 1; break; }
            case 0xbb: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_j(d_, l_add(d_->j, v[(op_b >> 4) & 0x0f].j)); pc += 1; break; }
            case 0xbc: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_j(d_, l_sub(d_->j, v[(op_b >> 4) & 0x0f].j)); pc += 1; break; }
            case 0xbd: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_j(d_, l_mul(d_->j, v[(op_b >> 4) & 0x0f].j)); pc += 1; break; }
            case 0xbe: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_j(d_, l_div(d_->j, v[(op_b >> 4) & 0x0f].j)); pc += 1; break; }
            case 0xbf: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_j(d_, l_rem(d_->j, v[(op_b >> 4) & 0x0f].j)); pc += 1; break; }
            case 0xc0: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_j(d_, l_and(d_->j, v[(op_b >> 4) & 0x0f].j)); pc += 1; break; }
            case 0xc1: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_j(d_, l_or(d_->j, v[(op_b >> 4) & 0x0f].j)); pc += 1; break; }
            case 0xc2: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_j(d_, l_xor(d_->j, v[(op_b >> 4) & 0x0f].j)); pc += 1; break; }
            case 0xc3: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_j(d_, l_shl(d_->j, v[(op_b >> 4) & 0x0f].i)); pc += 1; break; }
            case 0xc4: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_j(d_, l_shr(d_->j, v[(op_b >> 4) & 0x0f].i)); pc += 1; break; }
            case 0xc5: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_j(d_, l_ushr(d_->j, v[(op_b >> 4) & 0x0f].i)); pc += 1; break; }
            case 0xc6: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_f(d_, f_add(d_->f, v[(op_b >> 4) & 0x0f].f)); pc += 1; break; }
            case 0xc7: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_f(d_, f_sub(d_->f, v[(op_b >> 4) & 0x0f].f)); pc += 1; break; }
            case 0xc8: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_f(d_, f_mul(d_->f, v[(op_b >> 4) & 0x0f].f)); pc += 1; break; }
            case 0xc9: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_f(d_, f_div(d_->f, v[(op_b >> 4) & 0x0f].f)); pc += 1; break; }
            case 0xca: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_f(d_, f_rem(d_->f, v[(op_b >> 4) & 0x0f].f)); pc += 1; break; }
            case 0xcb: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_d(d_, d_add(d_->d, v[(op_b >> 4) & 0x0f].d)); pc += 1; break; }
            case 0xcc: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_d(d_, d_sub(d_->d, v[(op_b >> 4) & 0x0f].d)); pc += 1; break; }
            case 0xcd: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_d(d_, d_mul(d_->d, v[(op_b >> 4) & 0x0f].d)); pc += 1; break; }
            case 0xce: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_d(d_, d_div(d_->d, v[(op_b >> 4) & 0x0f].d)); pc += 1; break; }
            case 0xcf: { tl_dex_val *d_ = &v[op_b & 0x0f]; put_d(d_, d_rem(d_->d, v[(op_b >> 4) & 0x0f].d)); pc += 1; break; }

            /* ---- literal forms ---- */
            case 0xd0: put_i(&v[op_b & 0x0f], i_add(v[(op_b >> 4) & 0x0f].i, (int16_t)insns[pc + 1])); pc += 2; break;
            case 0xd1: put_i(&v[op_b & 0x0f], i_rsub(v[(op_b >> 4) & 0x0f].i, (int16_t)insns[pc + 1])); pc += 2; break;
            case 0xd2: put_i(&v[op_b & 0x0f], i_mul(v[(op_b >> 4) & 0x0f].i, (int16_t)insns[pc + 1])); pc += 2; break;
            case 0xd3: put_i(&v[op_b & 0x0f], i_div(v[(op_b >> 4) & 0x0f].i, (int16_t)insns[pc + 1])); pc += 2; break;
            case 0xd4: put_i(&v[op_b & 0x0f], i_rem(v[(op_b >> 4) & 0x0f].i, (int16_t)insns[pc + 1])); pc += 2; break;
            case 0xd5: put_i(&v[op_b & 0x0f], i_and(v[(op_b >> 4) & 0x0f].i, (int16_t)insns[pc + 1])); pc += 2; break;
            case 0xd6: put_i(&v[op_b & 0x0f], i_or(v[(op_b >> 4) & 0x0f].i, (int16_t)insns[pc + 1])); pc += 2; break;
            case 0xd7: put_i(&v[op_b & 0x0f], i_xor(v[(op_b >> 4) & 0x0f].i, (int16_t)insns[pc + 1])); pc += 2; break;
            case 0xd8: put_i(&v[op_b], i_add(v[insns[pc + 1] & 0xff].i, (int8_t)(insns[pc + 1] >> 8))); pc += 2; break;
            case 0xd9: put_i(&v[op_b], i_rsub(v[insns[pc + 1] & 0xff].i, (int8_t)(insns[pc + 1] >> 8))); pc += 2; break;
            case 0xda: put_i(&v[op_b], i_mul(v[insns[pc + 1] & 0xff].i, (int8_t)(insns[pc + 1] >> 8))); pc += 2; break;
            case 0xdb: put_i(&v[op_b], i_div(v[insns[pc + 1] & 0xff].i, (int8_t)(insns[pc + 1] >> 8))); pc += 2; break;
            case 0xdc: put_i(&v[op_b], i_rem(v[insns[pc + 1] & 0xff].i, (int8_t)(insns[pc + 1] >> 8))); pc += 2; break;
            case 0xdd: put_i(&v[op_b], i_and(v[insns[pc + 1] & 0xff].i, (int8_t)(insns[pc + 1] >> 8))); pc += 2; break;
            case 0xde: put_i(&v[op_b], i_or(v[insns[pc + 1] & 0xff].i, (int8_t)(insns[pc + 1] >> 8))); pc += 2; break;
            case 0xdf: put_i(&v[op_b], i_xor(v[insns[pc + 1] & 0xff].i, (int8_t)(insns[pc + 1] >> 8))); pc += 2; break;
            case 0xe0: put_i(&v[op_b], i_shl(v[insns[pc + 1] & 0xff].i, (int8_t)(insns[pc + 1] >> 8))); pc += 2; break;
            case 0xe1: put_i(&v[op_b], i_shr(v[insns[pc + 1] & 0xff].i, (int8_t)(insns[pc + 1] >> 8))); pc += 2; break;
            case 0xe2: put_i(&v[op_b], i_ushr(v[insns[pc + 1] & 0xff].i, (int8_t)(insns[pc + 1] >> 8))); pc += 2; break;

            default: {
                /*
                 * An opcode this interpreter does not implement.
                 *
                 * This used to step one code unit and carry on, which is only
                 * right for a one-unit instruction. Most of the missing ones were
                 * longer, so the operands were then decoded as the next
                 * instructions and the method ran off into nonsense without a
                 * word. Stop instead, and say which opcode and where: the call
                 * ends, and the log names exactly what to implement next.
                 */
                static uint32_t reported[8];       /* one bit per opcode value */
                uint32_t bit = 1u << (opcode & 31), word = (opcode >> 5) & 7;
                if (!(reported[word] & bit)) {
                    reported[word] |= bit;
                    dex_log("tl: UNIMPLEMENTED opcode 0x%02x in %s.%s at pc=%u -- ending the call",
                            opcode, method->clazz ? method->clazz->descriptor : "?",
                            method->name, pc);
                }
                success = false;
                goto done;
            }
        }
    }

done:
    free(v);
    return success;
}

/* ------------------------------------------------ Frame & Touch Dispatch */

void tl_pacer_start(tl_pacer *p, uint64_t frame_ns)
{
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    p->frame_ns = frame_ns;
    p->tb_numer = tb.numer;
    p->tb_denom = tb.denom;
    p->next = tl_dex_now_ns() + frame_ns;
}

bool tl_pacer_wait(tl_pacer *p)
{
    uint64_t after = tl_dex_now_ns();
    uint64_t deadline = p->next;
    p->next += p->frame_ns;

    if (after < deadline) {
        /* The deadline in mach ticks: ns * denom / numer. */
        mach_wait_until((uint64_t)((__uint128_t)deadline * p->tb_denom / p->tb_numer));
        return false;
    }
    /* Late. Far enough behind and the schedule is abandoned rather than chased. */
    if (after > deadline + 4 * p->frame_ns) p->next = after + p->frame_ns;
    return true;
}

void tl_dex_seed(tl_dex_context *ctx, uint64_t seed)
{
    if (!ctx) return;
    /* xorshift cannot start at zero. */
    ctx->rng = seed ? seed : 0x9E3779B97F4A7C15ull;
}

/* xorshift64*: small, fast, and well distributed for a game's purposes. */
double tl_dex_random(tl_dex_context *ctx)
{
    uint64_t x = ctx ? ctx->rng : 0x9E3779B97F4A7C15ull;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    if (ctx) ctx->rng = x;
    return (double)((x * 0x2545F4914F6CDD1Dull) >> 11) / 9007199254740992.0;   /* 2^53 */
}

uint64_t tl_dex_now_ns(void)
{
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

void tl_dex_post_delayed(tl_dex_context *ctx, tl_dex_object *runnable, uint64_t delay_ms)
{
    if (!ctx || !runnable) return;
    tl_dex_task *t = calloc(1, sizeof(*t));
    if (!t) return;
    t->runnable = runnable;
    t->due_nanos = ctx->clock_nanos + delay_ms * 1000000ull;

    /* Sorted by due time, and after anything already due at the same moment:
     * two posts for the same instant run in the order they were made. */
    tl_dex_task **at = &ctx->tasks;
    while (*at && (*at)->due_nanos <= t->due_nanos) at = &(*at)->next;
    t->next = *at;
    *at = t;
}

void tl_dex_remove_callbacks(tl_dex_context *ctx, tl_dex_object *runnable)
{
    if (!ctx) return;
    tl_dex_task **at = &ctx->tasks;
    while (*at) {
        if (!runnable || (*at)->runnable == runnable) {
            tl_dex_task *dead = *at;
            *at = dead->next;
            free(dead);
        } else {
            at = &(*at)->next;
        }
    }
}

/* Run everything that has come due, once. Detached from the queue first, so a
 * task that posts more work (as these usually do) adds to a queue nobody is in
 * the middle of walking, and that work waits for a later frame. */
static void dex_drain_input(tl_dex_context *ctx);

static void dex_run_due_tasks(tl_dex_context *ctx)
{
    tl_dex_task *due = NULL, **tail = &due;
    tl_dex_task **at = &ctx->tasks;
    while (*at && (*at)->due_nanos <= ctx->clock_nanos) {
        tl_dex_task *t = *at;
        *at = t->next;
        t->next = NULL;
        *tail = t;
        tail = &t->next;
    }
    while (due) {
        tl_dex_task *t = due;
        due = t->next;
        if (t->runnable && t->runnable->clazz) {
            tl_dex_method *run = tl_dex_find_method(t->runnable->clazz, "run", "V");
            if (run) {
                tl_dex_val a[1];
                a[0].raw64 = 0;
                a[0].l = t->runnable;
                tl_dex_invoke(ctx, run, a, 1, NULL);
            }
        }
        free(t);
    }
}

void tl_dex_tick_frame(tl_dex_context *ctx, uint64_t frame_time_nanos)
{
    if (!ctx) return;

    /* Never backwards: code that measures an interval would see it as negative. */
    if (frame_time_nanos > ctx->clock_nanos) ctx->clock_nanos = frame_time_nanos;

    uint64_t t_start = tl_dex_now_ns();

    /* Input first, then deferred work, then the frame callback: the order
     * Android's Choreographer runs them in. */
    dex_drain_input(ctx);
    dex_run_due_tasks(ctx);

    /* 1. Tick Choreographer callback */
    if (ctx->choreographer_cb) {
        tl_dex_method *doFrame = tl_dex_find_method(ctx->choreographer_cb->clazz, "doFrame", "VJ");
        if (doFrame) {
            tl_dex_val args[2];
            args[0].l = ctx->choreographer_cb;
            args[1].j = (int64_t)frame_time_nanos;
            tl_dex_invoke(ctx, doFrame, args, 2, NULL);
        }
    }

    uint64_t t_logic = tl_dex_now_ns();

    /* 2. Render view */
    tl_framework_render_view(ctx);
    ctx->frame_count++;

    ctx->perf.frames++;
    ctx->perf.ns_logic += t_logic - t_start;
    ctx->perf.ns_render += tl_dex_now_ns() - t_logic;
}

/* Deliver one touch to the app. Only ever called from the pump thread. */
static void dex_deliver_touch(tl_dex_context *ctx, int action, float x, float y)
{
    if (!ctx->current_view) return;

    tl_dex_class *c_me = tl_dex_find_class(ctx, "Landroid/view/MotionEvent;");
    tl_dex_object *ev = tl_dex_alloc_object(c_me);
    if (ev && ev->fields) {
        ev->fields[0].raw64 = 0; ev->fields[0].f = x;
        ev->fields[1].raw64 = 0; ev->fields[1].f = y;
        ev->fields[2].raw64 = 0; ev->fields[2].i = action;
    }

    tl_dex_method *onTouch = tl_dex_find_method(ctx->current_view->clazz, "onTouchEvent", "ZL");
    if (onTouch) {
        tl_dex_val args[2];
        args[0].raw64 = 0; args[0].l = ctx->current_view;
        args[1].raw64 = 0; args[1].l = ev;
        tl_dex_invoke(ctx, onTouch, args, 2, NULL);
    }
}

/*
 * Queue a touch. Safe to call from any thread; the app sees it at the start of
 * the next frame. A full queue drops the event rather than make the UI thread
 * wait on the interpreter -- sixty-four slots is some thirty frames of a touch
 * moving at the screen's full rate, which nothing healthy comes near.
 */
void tl_dex_send_touch(tl_dex_context *ctx, int action, float x, float y)
{
    if (!ctx) return;
    pthread_mutex_lock(&ctx->input_lock);
    int next = (ctx->input_tail + 1) % 64;
    if (next != ctx->input_head) {
        ctx->input[ctx->input_tail].action = action;
        ctx->input[ctx->input_tail].x = x;
        ctx->input[ctx->input_tail].y = y;
        ctx->input_tail = next;
    }
    pthread_mutex_unlock(&ctx->input_lock);
}

/* Hand every queued touch to the app, in order. The lock is held only to take
 * an event off the queue, never while the app's handler runs: a handler that
 * is slow must not stall the UI thread's next send_touch. */
static void dex_drain_input(tl_dex_context *ctx)
{
    for (;;) {
        int action; float x, y;
        pthread_mutex_lock(&ctx->input_lock);
        if (ctx->input_head == ctx->input_tail) {
            pthread_mutex_unlock(&ctx->input_lock);
            return;
        }
        action = ctx->input[ctx->input_head].action;
        x = ctx->input[ctx->input_head].x;
        y = ctx->input[ctx->input_head].y;
        ctx->input_head = (ctx->input_head + 1) % 64;
        pthread_mutex_unlock(&ctx->input_lock);
        dex_deliver_touch(ctx, action, x, y);
    }
}
