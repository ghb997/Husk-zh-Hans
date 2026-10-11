/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "husk-tl-res.h"

#include <stdlib.h>
#include <string.h>

/*
 * Layout, for anyone reading this against the format's own header.
 *
 * resources.arsc is a tree of chunks, each beginning type(u16) headerSize(u16)
 * size(u32). The file is:
 *
 *   TABLE (0x0002)
 *     STRING_POOL (0x0001)      every string value in the table: file paths,
 *                               string resources
 *     PACKAGE (0x0200)          one per package; an app's own is 0x7f
 *       STRING_POOL             the names of the resource TYPES (index+1 is
 *                               the type id): "attr", "drawable", "layout"...
 *       STRING_POOL             the names of the resource ENTRIES ("bird")
 *       TYPE_SPEC (0x0202)      which configurations an entry varies by
 *       TYPE (0x0201)           one per (type, configuration): an offset table
 *                               and the entries it points at
 *
 * A resource id is (package << 24) | (type << 16) | entry. The same id has one
 * entry in each configuration of its type -- one per screen density, one per
 * language -- and which one applies is a decision for the device, made here
 * with only a density.
 *
 * Every read goes through rd8/rd16/rd32, which return zero off the end of the
 * buffer rather than trapping, and every offset that is followed is compared
 * against its chunk's end first.
 */

#define RES_TABLE        0x0002
#define RES_STRING_POOL  0x0001
#define RES_PACKAGE      0x0200
#define RES_TYPE         0x0201

#define TYPE_STRING      0x03

#define MAX_PACKAGES     8
#define NO_ENTRY16       0xFFFFu
#define NO_ENTRY32       0xFFFFFFFFu

typedef struct {
    const uint8_t *d;
    size_t n;
    size_t end;          /* one past the pool chunk */
    uint32_t count;
    int utf8;
    size_t index_off;    /* the offset table */
    size_t data_off;     /* the string bytes */
    char **cache;        /* UTF-16 pools only: each string, decoded on demand */
} res_pool;

typedef struct {
    uint32_t id;         /* 0x7f for an app's own */
    res_pool types;
    res_pool keys;
} res_package;

typedef struct {
    int pkg;
    uint8_t type_id;     /* 1-based */
    uint16_t density;
    uint32_t locale;     /* language and country, packed; 0 is the default */
    size_t off;
    size_t end;
} res_chunk;

struct tl_res {
    const uint8_t *d;
    size_t n;
    res_pool values;
    res_package pkgs[MAX_PACKAGES];
    int npkgs;
    res_chunk *chunks;
    int nchunks, cap;
    size_t entries;
};

typedef struct {
    uint32_t key;
    uint8_t data_type;
    uint32_t data;
    int complex;         /* a style or an array: no single value */
} res_entry;

/* ------------------------------------------------------------- primitives */

static uint8_t rd8(const uint8_t *d, size_t n, size_t i)
{
    return i < n ? d[i] : 0;
}

static uint16_t rd16(const uint8_t *d, size_t n, size_t i)
{
    if (i + 1 >= n) return 0;
    return (uint16_t)(d[i] | (d[i + 1] << 8));
}

static uint32_t rd32(const uint8_t *d, size_t n, size_t i)
{
    if (i + 3 >= n) return 0;
    return (uint32_t)d[i] | ((uint32_t)d[i + 1] << 8) |
           ((uint32_t)d[i + 2] << 16) | ((uint32_t)d[i + 3] << 24);
}

/* ------------------------------------------------------------ string pool */

static int pool_init(res_pool *p, const uint8_t *d, size_t n, size_t chunk)
{
    memset(p, 0, sizeof(*p));
    if (chunk >= n || chunk + 28 > n || rd16(d, n, chunk) != RES_STRING_POOL) return 0;

    size_t header = rd16(d, n, chunk + 2);
    size_t size = rd32(d, n, chunk + 4);
    uint32_t count = rd32(d, n, chunk + 8);
    uint32_t flags = rd32(d, n, chunk + 16);
    size_t strings = rd32(d, n, chunk + 20);

    /* A real pool is never anywhere near a million strings; a count that large
     * is a corrupt header, and trusting it is an allocation of the same size. */
    if (header < 28 || size < header || chunk + size > n || count > 1000000) return 0;
    if (header + (size_t)count * 4 > size || strings > size) return 0;

    p->d = d;
    p->n = n;
    p->end = chunk + size;
    p->count = count;
    p->utf8 = (flags & 0x100) != 0;
    p->index_off = chunk + header;
    p->data_off = chunk + strings;
    if (!p->utf8 && count) {
        p->cache = calloc(count, sizeof(char *));
        if (!p->cache) { memset(p, 0, sizeof(*p)); return 0; }
    }
    return 1;
}

static void pool_free(res_pool *p)
{
    if (p->cache) {
        for (uint32_t i = 0; i < p->count; i++) free(p->cache[i]);
        free(p->cache);
    }
    memset(p, 0, sizeof(*p));
}

/* One code point to UTF-8; returns bytes written. */
static size_t utf8_put(char *out, uint32_t cp)
{
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* The string at `idx`, as NUL-terminated UTF-8, or NULL. Owned by the pool. */
static const char *pool_get(res_pool *p, uint32_t idx)
{
    if (!p->d || idx >= p->count) return NULL;

    size_t off = p->data_off + rd32(p->d, p->n, p->index_off + (size_t)idx * 4);
    if (off >= p->end) return NULL;

    if (p->utf8) {
        /* Two lengths, each one byte or two: the length in UTF-16 units, which
         * is of no use here, then the length in bytes. */
        size_t q = off;
        if (p->d[q++] & 0x80) q++;
        if (q >= p->end) return NULL;
        size_t bytes = p->d[q++];
        if (bytes & 0x80) {
            if (q >= p->end) return NULL;
            bytes = ((bytes & 0x7F) << 8) | p->d[q++];
        }
        /* Room for the terminator, and the terminator actually there. */
        if (q + bytes >= p->end || p->d[q + bytes] != 0) return NULL;
        return (const char *)p->d + q;
    }

    if (p->cache[idx]) return p->cache[idx];

    size_t q = off;
    if (q + 2 > p->end) return NULL;
    size_t len = rd16(p->d, p->n, q);
    q += 2;
    if (len & 0x8000) {
        if (q + 2 > p->end) return NULL;
        len = ((len & 0x7FFF) << 16) | rd16(p->d, p->n, q);
        q += 2;
    }
    if (q + len * 2 > p->end) return NULL;

    char *out = malloc(len * 4 + 1);
    if (!out) return NULL;
    size_t w = 0;
    for (size_t i = 0; i < len; i++) {
        uint32_t u = rd16(p->d, p->n, q + i * 2);
        if (u >= 0xD800 && u < 0xDC00 && i + 1 < len) {
            uint32_t lo = rd16(p->d, p->n, q + (i + 1) * 2);
            if (lo >= 0xDC00 && lo < 0xE000) {
                u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                i++;
            }
        }
        w += utf8_put(out + w, u);
    }
    out[w] = 0;
    p->cache[idx] = out;
    return out;
}

/* -------------------------------------------------------------- the table */

static int add_chunk(tl_res *r, int pkg, size_t off, size_t size)
{
    if (r->nchunks == r->cap) {
        int cap = r->cap ? r->cap * 2 : 64;
        res_chunk *grown = realloc(r->chunks, (size_t)cap * sizeof(*grown));
        if (!grown) return 0;
        r->chunks = grown;
        r->cap = cap;
    }

    /* The configuration begins 20 bytes in. Its own first word is its size,
     * which older tables make too short to reach the density at +14. */
    uint32_t cfg = rd32(r->d, r->n, off + 20);
    res_chunk *c = &r->chunks[r->nchunks++];
    c->pkg = pkg;
    c->type_id = rd8(r->d, r->n, off + 8);
    c->off = off;
    c->end = off + size;
    c->locale = cfg >= 12 ? rd32(r->d, r->n, off + 20 + 8) : 0;
    c->density = cfg >= 16 ? rd16(r->d, r->n, off + 20 + 14) : 0;

    r->entries += rd32(r->d, r->n, off + 12);
    return 1;
}

static void scan_package(tl_res *r, size_t p, size_t size)
{
    if (r->npkgs >= MAX_PACKAGES) return;

    /* 288 bytes with typeIdOffset, 284 in tables that predate it. The two pool
     * offsets sit at the same place in both. */
    size_t header = rd16(r->d, r->n, p + 2);
    if (header < 284 || header > size) return;

    int pi = r->npkgs;
    res_package *pk = &r->pkgs[pi];
    memset(pk, 0, sizeof(*pk));
    pk->id = rd32(r->d, r->n, p + 8);

    size_t ts = rd32(r->d, r->n, p + 268);
    size_t ks = rd32(r->d, r->n, p + 276);
    if (ts && ts < size) pool_init(&pk->types, r->d, r->n, p + ts);
    if (ks && ks < size) pool_init(&pk->keys, r->d, r->n, p + ks);
    r->npkgs++;

    size_t q = p + header;
    while (q + 8 <= p + size) {
        uint16_t type = rd16(r->d, r->n, q);
        size_t csz = rd32(r->d, r->n, q + 4);
        if (csz < 8 || q + csz > p + size) break;
        if (type == RES_TYPE && !add_chunk(r, pi, q, csz)) break;
        q += csz;
    }
}

tl_res *tl_res_create(const uint8_t *data, size_t len)
{
    if (!data || len < 16 || rd16(data, len, 0) != RES_TABLE) return NULL;

    tl_res *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->d = data;
    r->n = len;

    size_t p = rd16(data, len, 2);
    if (!pool_init(&r->values, data, len, p)) { free(r); return NULL; }
    p += rd32(data, len, p + 4);

    while (p + 8 <= len) {
        uint16_t type = rd16(data, len, p);
        size_t size = rd32(data, len, p + 4);
        if (size < 8 || p + size > len) break;
        if (type == RES_PACKAGE) scan_package(r, p, size);
        p += size;
    }

    if (r->nchunks == 0) { tl_res_destroy(r); return NULL; }
    return r;
}

void tl_res_destroy(tl_res *r)
{
    if (!r) return;
    pool_free(&r->values);
    for (int i = 0; i < r->npkgs; i++) {
        pool_free(&r->pkgs[i].types);
        pool_free(&r->pkgs[i].keys);
    }
    free(r->chunks);
    free(r);
}

size_t tl_res_count(const tl_res *r)
{
    return r ? r->entries : 0;
}

/* ---------------------------------------------------------------- entries */

/*
 * The entry at `index` in one configuration's chunk, if that configuration has
 * one. Most do not: a resource that varies by density has an entry in the
 * densities it was drawn for and a hole everywhere else.
 */
static int chunk_entry(const tl_res *r, const res_chunk *c, uint32_t index, res_entry *out)
{
    const uint8_t *d = r->d;
    size_t n = r->n;
    size_t p = c->off;

    size_t header = rd16(d, n, p + 2);
    uint8_t flags = rd8(d, n, p + 9);
    uint32_t count = rd32(d, n, p + 12);
    size_t estart = rd32(d, n, p + 16);
    int sparse = (flags & 0x01) != 0;
    int off16 = (flags & 0x02) != 0;

    size_t entry_off = 0;
    if (sparse) {
        /* Sorted (index, offset/4) pairs, only for entries that exist. */
        int found = 0;
        for (uint32_t i = 0; i < count; i++) {
            size_t q = p + header + (size_t)i * 4;
            if (q + 4 > c->end) return 0;
            if (rd16(d, n, q) == index) { entry_off = (size_t)rd16(d, n, q + 2) * 4; found = 1; break; }
        }
        if (!found) return 0;
    } else {
        if (index >= count) return 0;
        if (off16) {
            size_t q = p + header + (size_t)index * 2;
            if (q + 2 > c->end) return 0;
            uint16_t raw = rd16(d, n, q);
            if (raw == NO_ENTRY16) return 0;
            entry_off = (size_t)raw * 4;
        } else {
            size_t q = p + header + (size_t)index * 4;
            if (q + 4 > c->end) return 0;
            uint32_t raw = rd32(d, n, q);
            if (raw == NO_ENTRY32) return 0;
            entry_off = raw;
        }
    }

    size_t e = p + estart + entry_off;
    if (e + 8 > c->end) return 0;

    uint16_t eflags = rd16(d, n, e + 2);
    memset(out, 0, sizeof(*out));

    if (eflags & 0x0008) {
        /* Compact (Android 14 and later): the key is the first half-word, the
         * value's type is the top byte of the flags, and the data follows. */
        out->key = rd16(d, n, e);
        out->data_type = (uint8_t)((eflags >> 8) & 0xFF);
        out->data = rd32(d, n, e + 4);
        return 1;
    }

    out->key = rd32(d, n, e + 4);
    if (eflags & 0x0001) {          /* complex: a style, an array, a plural */
        out->complex = 1;
        return 1;
    }

    size_t v = e + rd16(d, n, e);
    if (v + 8 > c->end) return 0;
    out->data_type = rd8(d, n, v + 3);
    out->data = rd32(d, n, v + 4);
    return 1;
}

/* ----------------------------------------------------------------- lookup */

/* Names are compared as the build tools see them: '-', '.' and '_' are all
 * one character, because aapt turns the first two into the third. A file called
 * bluebird-midflap.png is the resource bluebird_midflap. */
static int name_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = (*a == '-' || *a == '.') ? '_' : *a;
        char y = (*b == '-' || *b == '.') ? '_' : *b;
        if (x != y) return 0;
    }
    return *a == *b;
}

uint32_t tl_res_find(const tl_res *r, const char *type, const char *name)
{
    if (!r || !name || !*name) return 0;

    /* getIdentifier takes "entry", "type/entry" or "package:type/entry", and
     * the type inside the name beats the one passed beside it. */
    char tbuf[64];
    const char *colon = strchr(name, ':');
    const char *slash = strchr(name, '/');
    if (colon && (!slash || colon < slash)) name = colon + 1;
    slash = strchr(name, '/');
    if (slash) {
        size_t tl = (size_t)(slash - name);
        if (tl == 0 || tl >= sizeof(tbuf)) return 0;
        memcpy(tbuf, name, tl);
        tbuf[tl] = 0;
        type = tbuf;
        name = slash + 1;
    }
    if (!type || !*type || !*name) return 0;

    for (int pi = 0; pi < r->npkgs; pi++) {
        res_package *pk = (res_package *)&r->pkgs[pi];

        /* Which type id has this name: its position in the type pool, plus one. */
        uint32_t type_id = 0;
        for (uint32_t t = 0; t < pk->types.count && t < 255; t++) {
            const char *tn = pool_get(&pk->types, t);
            if (tn && !strcmp(tn, type)) { type_id = t + 1; break; }
        }
        if (!type_id) continue;

        for (int ci = 0; ci < r->nchunks; ci++) {
            const res_chunk *c = &r->chunks[ci];
            if (c->pkg != pi || c->type_id != type_id) continue;

            uint32_t count = rd32(r->d, r->n, c->off + 12);
            for (uint32_t i = 0; i < count; i++) {
                /* Non-sparse chunks index by position; sparse ones carry the
                 * index in their pair table. */
                uint32_t index = i;
                if (rd8(r->d, r->n, c->off + 9) & 0x01) {
                    size_t q = c->off + rd16(r->d, r->n, c->off + 2) + (size_t)i * 4;
                    if (q + 4 > c->end) break;
                    index = rd16(r->d, r->n, q);
                }

                res_entry e;
                if (!chunk_entry(r, c, index, &e)) continue;
                const char *key = pool_get(&pk->keys, e.key);
                if (key && name_eq(key, name)) {
                    return (pk->id << 24) | ((uint32_t)type_id << 16) | index;
                }
            }
        }
    }
    return 0;
}

/*
 * Walk every configuration's entry for an id, handing each to `visit`, which
 * returns nonzero to stop. The selection rules differ between a file and a
 * string, but the walk -- find the package, the type, then every chunk of that
 * type that has an entry at this index -- is the same.
 */
typedef int (*entry_visit)(const tl_res *r, const res_chunk *c, const res_entry *e, void *ctx);

static void for_each_entry(const tl_res *r, uint32_t id, entry_visit visit, void *ctx)
{
    uint32_t pkg_id = (id >> 24) & 0xFF;
    uint32_t type_id = (id >> 16) & 0xFF;
    uint32_t index = id & 0xFFFF;

    for (int ci = 0; ci < r->nchunks; ci++) {
        const res_chunk *c = &r->chunks[ci];
        if (c->type_id != type_id || r->pkgs[c->pkg].id != pkg_id) continue;
        res_entry e;
        if (chunk_entry(r, c, index, &e) && visit(r, c, &e, ctx)) return;
    }
}

typedef struct {
    int want;                /* the density being asked for */
    int have;                /* whether anything has been picked */
    int best_density;
    const char *best;
    res_pool *values;
} file_pick;

/* Density 0 is "no qualifier", which Android treats as mdpi; 0xFFFE is "any",
 * the slot an XML drawable is filed under. Neither competes with a real size. */
static int effective_density(uint16_t d)
{
    if (d == 0) return 160;
    if (d == 0xFFFE) return 1;
    return d;
}

static int pick_file(const tl_res *r, const res_chunk *c, const res_entry *e, void *vctx)
{
    (void)r;
    file_pick *f = vctx;
    if (e->complex || e->data_type != TYPE_STRING) return 0;
    const char *path = pool_get(f->values, e->data);
    if (!path || (strncmp(path, "res/", 4) && strncmp(path, "assets/", 7))) return 0;

    int dens = effective_density(c->density);
    int better;
    if (!f->have) {
        better = 1;
    } else if (dens == f->want) {
        better = f->best_density != f->want;
    } else if (f->best_density == f->want) {
        better = 0;
    } else if (dens > f->want && f->best_density < f->want) {
        better = 1;                                  /* scale down, not up */
    } else if (dens > f->want && f->best_density > f->want) {
        better = dens < f->best_density;             /* the nearest above */
    } else if (dens < f->want && f->best_density < f->want) {
        better = dens > f->best_density;             /* the nearest below */
    } else {
        better = 0;
    }

    if (better) {
        f->have = 1;
        f->best_density = dens;
        f->best = path;
    }
    return 0;
}

const char *tl_res_file(const tl_res *r, uint32_t id, int density_dpi)
{
    if (!r || !id) return NULL;
    file_pick f = { .want = density_dpi > 0 ? density_dpi : 160,
                    .values = (res_pool *)&r->values };
    for_each_entry(r, id, pick_file, &f);
    return f.best;
}

typedef struct {
    const char *best;
    int best_is_default;
    res_pool *values;
} string_pick;

static int pick_string(const tl_res *r, const res_chunk *c, const res_entry *e, void *vctx)
{
    (void)r;
    string_pick *s = vctx;
    if (e->complex || e->data_type != TYPE_STRING) return 0;
    const char *v = pool_get(s->values, e->data);
    if (!v) return 0;
    int is_default = (c->locale == 0);
    if (!s->best || (is_default && !s->best_is_default)) {
        s->best = v;
        s->best_is_default = is_default;
    }
    return 0;
}

const char *tl_res_string(const tl_res *r, uint32_t id)
{
    if (!r || !id) return NULL;
    string_pick s = { .values = (res_pool *)&r->values };
    for_each_entry(r, id, pick_string, &s);
    return s.best;
}
