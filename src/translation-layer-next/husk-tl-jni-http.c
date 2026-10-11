/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Unity's web requests, whose Java side is com.unity3d.player.UnityWebRequest: a Runnable over HttpURLConnection that the engine
 * builds (with a java.util.HashMap of request headers), sets up, runs on a thread of its own, and which reports back through
 * native callbacks -- the status, each header, the length, the body in pieces, or an error. Here the request itself is the host's
 * (husk-tl-http.m), and the Java object is this: it keeps what the engine gave it and, on run(), does what the Java would have done,
 * in the same order, so the engine sees the same calls.
 *
 * Also the java.util.HashMap that carries the headers, since the engine reads nothing back from it but this code does.
 */
#define _DARWIN_C_SOURCE
#include "husk-tl-jni.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "husk-tl-bionic.h"
#include "husk-tl-http.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <resolv.h>

static jvalue vl(void *p) { jvalue v; v.j = 0; v.l = p; return v; }
static jvalue vi(int i) { jvalue v; v.j = 0; v.i = i; return v; }
static jvalue vz(int z) { jvalue v; v.j = 0; v.z = z != 0; return v; }
static void Noop(tl_jcall *c) { (void)c; }
static const char *S(const jobj *o) { const char *s = tl_jni_string(o); return s ? s : ""; }

/* ------------------------------------------------------------------ HashMap */

typedef struct { jobj **k, **v; int n, cap; } hmap;
static hmap *HM(jobj *o) { if (!o->native) o->native = calloc(1, sizeof(hmap)); return o->native; }

static void Map_init(tl_jcall *c) { (void)HM(c->self); }
static void Map_put(tl_jcall *c)
{
    hmap *m = HM(c->self);
    const char *key = S(c->args[0].l);
    for (int i = 0; i < m->n; i++) {
        if (!strcmp(S(m->k[i]), key)) { jobj *old = m->v[i]; m->v[i] = c->args[1].l ? tl_jni_ref(c->args[1].l) : NULL; c->ret = vl(old); return; }
    }
    if (m->n == m->cap) { m->cap = m->cap ? m->cap * 2 : 8; m->k = realloc(m->k, (size_t)m->cap * sizeof(jobj *)); m->v = realloc(m->v, (size_t)m->cap * sizeof(jobj *)); }
    m->k[m->n] = c->args[0].l ? tl_jni_ref(c->args[0].l) : NULL;
    m->v[m->n] = c->args[1].l ? tl_jni_ref(c->args[1].l) : NULL;
    m->n++;
    c->ret = vl(NULL);
}
static void Map_get(tl_jcall *c)
{
    hmap *m = HM(c->self);
    const char *key = S(c->args[0].l);
    for (int i = 0; i < m->n; i++) if (!strcmp(S(m->k[i]), key)) { c->ret = vl(m->v[i] ? tl_jni_ref(m->v[i]) : NULL); return; }
    c->ret = vl(NULL);
}
static void Map_containsKey(tl_jcall *c)
{
    hmap *m = HM(c->self);
    const char *key = S(c->args[0].l);
    for (int i = 0; i < m->n; i++) if (!strcmp(S(m->k[i]), key)) { c->ret = vz(1); return; }
    c->ret = vz(0);
}
static void Map_size(tl_jcall *c) { c->ret = vi(HM(c->self)->n); }
static void Map_isEmpty(tl_jcall *c) { c->ret = vz(HM(c->self)->n == 0); }
static void Map_entrySet(tl_jcall *c) { c->ret = vl(tl_jni_new_object(tl_jni_class("java/util/HashSet"))); }

/* ------------------------------------------------------------ UnityWebRequest */

#define CLS "com/unity3d/player/UnityWebRequest"

typedef struct { int64_t ptr; char *url, *method; jobj *headers; int timeout_ms; int64_t upload_len; bool expect, chunked; } uwr;
static uwr *U(jobj *o) { if (!o->native) o->native = calloc(1, sizeof(uwr)); return o->native; }

static void UWR_init(tl_jcall *c)
{
    /* (long nativeRequest, String method, Map headers, String url, boolean certificateHandler, int timeoutMs) */
    uwr *u = U(c->self);
    u->ptr = c->args[0].j;
    u->method = strdup(S(c->args[1].l));
    u->headers = c->args[2].l ? tl_jni_ref(c->args[2].l) : NULL;
    u->url = strdup(S(c->args[3].l));
    u->timeout_ms = c->args[5].i;
}
static void UWR_setup(tl_jcall *c)
{
    uwr *u = U(c->self);
    u->upload_len = c->args[0].j; u->expect = c->args[1].z; u->chunked = c->args[2].z;
}

typedef int (*upload_fn)(void *env, void *cls, int64_t ptr, void *buf);
typedef uint8_t (*download_fn)(void *env, void *cls, int64_t ptr, void *buf, int n);
typedef void (*header_fn)(void *env, void *cls, int64_t ptr, void *name, void *value);
typedef void (*int_fn)(void *env, void *cls, int64_t ptr, int v);
typedef void (*error_fn)(void *env, void *cls, int64_t ptr, int code, void *msg);

static jobj *direct_buffer(void *mem, int64_t cap)
{
    jobj *o = tl_jni_new_object(tl_jni_class("java/nio/DirectByteBuffer"));
    jvalue a, n; a.j = 0; a.l = mem; n.j = cap;
    tl_jni_set_field(o, "address", "J", a);
    tl_jni_set_field(o, "capacity", "J", n);
    return o;
}

static void UWR_run(tl_jcall *c)
{
    uwr *u = U(c->self);
    void *env = tl_jni_env(), *cls = tl_jni_class_object(CLS);
    upload_fn upload = tl_jni_native(CLS, "uploadCallback", "(JLjava/nio/ByteBuffer;)I");
    download_fn download = tl_jni_native(CLS, "downloadCallback", "(JLjava/nio/ByteBuffer;I)Z");
    header_fn header = tl_jni_native(CLS, "headerCallback", "(JLjava/lang/String;Ljava/lang/String;)V");
    int_fn length = tl_jni_native(CLS, "contentLengthCallback", "(JI)V");
    int_fn status = tl_jni_native(CLS, "responseCodeCallback", "(JI)V");
    error_fn error = tl_jni_native(CLS, "errorCallback", "(JILjava/lang/String;)V");
    if (!upload || !download || !header || !length || !status || !error) return;

    /* the request body, if any: the engine says how much with a null buffer, then hands it over in pieces */
    const size_t CHUNK = 128 * 1024;
    uint8_t *chunk = malloc(CHUNK), *body = NULL; size_t body_len = 0;
    jobj *buf = direct_buffer(chunk, (int64_t)CHUNK);
    if (upload(env, cls, u->ptr, NULL) > 0) {
        for (;;) {
            int n = upload(env, cls, u->ptr, buf);
            if (n <= 0) break;
            body = realloc(body, body_len + (size_t)n);
            memcpy(body + body_len, chunk, (size_t)n);
            body_len += (size_t)n;
        }
    }

    /* the headers it set */
    hmap *hm = u->headers ? HM(u->headers) : NULL;
    int nh = hm ? hm->n : 0;
    const char **names = calloc((size_t)nh + 1, sizeof(char *)), **values = calloc((size_t)nh + 1, sizeof(char *));
    for (int i = 0; i < nh; i++) { names[i] = S(hm->k[i]); values[i] = S(hm->v[i]); }

    tl_http_request rq = { .url = u->url, .method = u->method, .nheaders = nh, .header_names = names, .header_values = values,
                           .body = body, .body_len = body_len, .timeout_ms = u->timeout_ms };
    tl_http_response rs;
    tl_log_line("http: %s %s (%zu bytes sent)", u->method, u->url, body_len);
    if (!tl_http_perform(&rq, &rs)) {
        tl_log_line("http: %s failed: %s", u->url, rs.message);
        error(env, cls, u->ptr, rs.error, tl_jni_new_string(rs.message));
    } else {
        long content_length = -1;
        for (int i = 0; i < rs.nheaders; i++) {
            header(env, cls, u->ptr, tl_jni_new_string(rs.header_names[i]), tl_jni_new_string(rs.header_values[i]));
            if (!strcasecmp(rs.header_names[i], "content-length")) content_length = atol(rs.header_values[i]);
        }
        length(env, cls, u->ptr, (int)(content_length >= 0 ? content_length : (long)rs.body_len));
        status(env, cls, u->ptr, rs.status);
        for (size_t off = 0; off < rs.body_len;) {
            size_t n = rs.body_len - off < CHUNK ? rs.body_len - off : CHUNK;
            memcpy(chunk, rs.body + off, n);
            if (!download(env, cls, u->ptr, buf, (int)n)) break;
            off += n;
        }
        tl_log_line("http: %s -> %d (%zu bytes)", u->url, rs.status, rs.body_len);
        tl_http_response_free(&rs);
    }
    free(names); free(values); free(body);
    /* chunk stays: the buffer object may still be referenced by the engine */
}

#define M_(c, n, s, f) { c, n, s, f }
static const tl_jhle k_hle[] = {
    M_("java/util/HashMap", "<init>", "()V", Map_init), M_("java/util/HashMap", "<init>", "(I)V", Map_init),
    M_("java/util/HashMap", "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", Map_put),
    M_("java/util/HashMap", "get", "(Ljava/lang/Object;)Ljava/lang/Object;", Map_get),
    M_("java/util/HashMap", "containsKey", "(Ljava/lang/Object;)Z", Map_containsKey),
    M_("java/util/HashMap", "size", "()I", Map_size), M_("java/util/HashMap", "isEmpty", "()Z", Map_isEmpty),
    M_("java/util/HashMap", "entrySet", "()Ljava/util/Set;", Map_entrySet),
    M_("java/util/Map", "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", Map_put),
    M_("java/util/Map", "get", "(Ljava/lang/Object;)Ljava/lang/Object;", Map_get),
    M_("java/util/Map", "containsKey", "(Ljava/lang/Object;)Z", Map_containsKey),
    M_("java/util/Map", "size", "()I", Map_size), M_("java/util/Map", "isEmpty", "()Z", Map_isEmpty),
    M_("java/util/Map", "entrySet", "()Ljava/util/Set;", Map_entrySet),
    M_(CLS, "<init>", "(JLjava/lang/String;Ljava/util/Map;Ljava/lang/String;ZI)V", UWR_init),
    M_(CLS, "setupTransferSettings", "(JZZ)V", UWR_setup),
    M_(CLS, "run", "()V", UWR_run),
    M_(CLS, "clearCookieCache", "(Ljava/lang/String;Ljava/lang/String;)V", Noop),
    { NULL, NULL, NULL, NULL }
};

/* ------------------------------------------------------------ connectivity */

static void CM_activeNetworkInfo(tl_jcall *c) { c->ret = vl(tl_jni_new_object(tl_jni_class("android/net/NetworkInfo"))); }
static void CM_activeNetwork(tl_jcall *c) { c->ret = vl(tl_jni_new_object(tl_jni_class("android/net/Network"))); }
static void CM_capabilities(tl_jcall *c) { c->ret = vl(tl_jni_new_object(tl_jni_class("android/net/NetworkCapabilities"))); }
static void True_(tl_jcall *c) { c->ret = vz(1); }
static void NI_type(tl_jcall *c) { c->ret = vi(1); }                          /* TYPE_WIFI */
static void NI_typeName(tl_jcall *c) { c->ret = vl(tl_jni_new_string("WIFI")); }
static void NI_subtype(tl_jcall *c) { c->ret = vi(0); }

/*
 * The DNS servers the phone uses, as text, for code that resolves names itself rather than through getaddrinfo: c-ares (in
 * Geode 5's curl, and others) asks Android for them through ConnectivityManager -> LinkProperties.getDnsServers(), and
 * failing that reads net.dns1/net.dns2. Without an answer it has no servers and every request fails with "couldn't resolve
 * host". The system's own resolver configuration is read once; public resolvers stand behind it.
 */
int tl_dns_servers(char out[][64], int max)
{
    int n = 0;
    struct __res_state st;
    memset(&st, 0, sizeof(st));
    if (res_ninit(&st) == 0) {
        union res_sockaddr_union addrs[8];
        int got = res_getservers(&st, addrs, 8);
        for (int i = 0; i < got && n < max; i++) {
            if (addrs[i].sin.sin_family == AF_INET) inet_ntop(AF_INET, &addrs[i].sin.sin_addr, out[n], 64);
            else if (addrs[i].sin6.sin6_family == AF_INET6) inet_ntop(AF_INET6, &addrs[i].sin6.sin6_addr, out[n], 64);
            else continue;
            if (out[n][0]) n++;
        }
        res_ndestroy(&st);
    }
    static const char *fallback[] = { "1.1.1.1", "8.8.8.8" };
    for (int i = 0; i < 2 && n < max; i++) snprintf(out[n++], 64, "%s", fallback[i]);
    return n;
}

static void CM_linkProperties(tl_jcall *c) { c->ret = vl(tl_jni_new_object(tl_jni_class("android/net/LinkProperties"))); }
typedef struct { jobj **items; uint32_t n; } alist;      /* the ArrayList the gamepad code answers size()/get() for */
static void LP_dnsServers(tl_jcall *c)
{
    char servers[6][64];
    int n = tl_dns_servers(servers, 6);
    jobj *l = tl_jni_new_object(tl_jni_class("java/util/ArrayList"));
    alist *a = calloc(1, sizeof(*a));
    a->items = calloc((size_t)n, sizeof(jobj *));
    for (int i = 0; i < n; i++) {
        jobj *ia = tl_jni_new_object(tl_jni_class("java/net/InetAddress"));
        jvalue v; v.j = 0; v.l = tl_jni_new_string(servers[i]);
        tl_jni_set_field(ia, "host", "Ljava/lang/String;", v);
        a->items[a->n++] = ia;
    }
    l->native = a;
    c->ret = vl(l);
}
static void IA_hostAddress(tl_jcall *c) { c->ret = tl_jni_get_field(c->self, "host", "Ljava/lang/String;"); }

/* The phone has its network, as far as a game asking Android about it can tell: one active connection, over Wi-Fi, that is up. */
static const tl_jhle k_net[] = {
    M_("android/net/ConnectivityManager", "getActiveNetworkInfo", "()Landroid/net/NetworkInfo;", CM_activeNetworkInfo),
    M_("android/net/ConnectivityManager", "getNetworkInfo", "(I)Landroid/net/NetworkInfo;", CM_activeNetworkInfo),
    M_("android/net/ConnectivityManager", "getActiveNetwork", "()Landroid/net/Network;", CM_activeNetwork),
    M_("android/net/ConnectivityManager", "getNetworkCapabilities", "(Landroid/net/Network;)Landroid/net/NetworkCapabilities;", CM_capabilities),
    M_("android/net/NetworkInfo", "isConnected", "()Z", True_), M_("android/net/NetworkInfo", "isConnectedOrConnecting", "()Z", True_),
    M_("android/net/NetworkInfo", "isAvailable", "()Z", True_), M_("android/net/NetworkInfo", "getType", "()I", NI_type),
    M_("android/net/NetworkInfo", "getTypeName", "()Ljava/lang/String;", NI_typeName), M_("android/net/NetworkInfo", "getSubtype", "()I", NI_subtype),
    M_("android/net/NetworkCapabilities", "hasCapability", "(I)Z", True_), M_("android/net/NetworkCapabilities", "hasTransport", "(I)Z", True_),
    M_("android/net/ConnectivityManager", "getLinkProperties", "(Landroid/net/Network;)Landroid/net/LinkProperties;", CM_linkProperties),
    M_("android/net/LinkProperties", "getDnsServers", "()Ljava/util/List;", LP_dnsServers),
    M_("java/net/InetAddress", "getHostAddress", "()Ljava/lang/String;", IA_hostAddress),
    { NULL, NULL, NULL, NULL }
};


/* ------------------------------------------------- Xbox's HttpClientRequest */

/*
 * Minecraft's web calls (Xbox services, the store) go through Microsoft's libHttpClient, whose Android side is a Java class over OkHttp.
 * The library builds a com.xbox.httpclient.HttpClientRequest, fills it in, calls doRequestAsync(call) and waits to be told, through
 * native methods on that class, how it went: OnRequestCompleted(call, response) -- after which it asks the response for its status, its headers
 * and (getResponseBodyBytes) its body, which the Java streams back through another native -- or OnRequestFailed. Here the request goes to the
 * host's network stack. It is carried out on the thread that asked, which is one of the library's workers, so the guest code that reports the
 * outcome runs where it expects to.
 */
#include "husk-tl-ld.h"

#define XREQ "com/xbox/httpclient/HttpClientRequest"
#define XRES "com/xbox/httpclient/HttpClientResponse"
#define XIN  "com/xbox/httpclient/HttpClientRequestBody$NativeInputStream"
#define XOUT "com/xbox/httpclient/HttpClientResponse$NativeOutputStream"

static void *x_native(const char *cls, const char *name, const char *sig, const char *mangled)
{
    void *fn = tl_jni_native(cls, name, sig);
    if (!fn) { tl_lib *lib = tl_ld_find_lib("libHttpClient.Android.so"); if (lib) fn = tl_ld_sym(lib, mangled); }
    if (!fn) tl_log_line("http: native %s.%s%s is not provided by libHttpClient", cls, name, sig);
    return fn;
}

typedef struct { char *url, *method, *ctype; char **hn, **hv; int nh, cap; int64_t call, clen; } xreq;
typedef struct { int64_t call; tl_http_response rs; } xres;

static xreq *XQ(jobj *o) { if (!o->native) o->native = calloc(1, sizeof(xreq)); return o->native; }

static void XReq_init(tl_jcall *c) { (void)XQ(c->self); }
static void XReq_url(tl_jcall *c) { xreq *q = XQ(c->self); free(q->url); q->url = strdup(S(c->args[0].l)); }
static void XReq_header(tl_jcall *c)
{
    xreq *q = XQ(c->self);
    if (q->nh == q->cap) { q->cap = q->cap ? q->cap * 2 : 8; q->hn = realloc(q->hn, (size_t)q->cap * sizeof(char *)); q->hv = realloc(q->hv, (size_t)q->cap * sizeof(char *)); }
    q->hn[q->nh] = strdup(S(c->args[0].l)); q->hv[q->nh] = strdup(S(c->args[1].l)); q->nh++;
}
static void XReq_methodAndBody(tl_jcall *c)
{
    /* (String method, long call, String contentType, long contentLength) */
    xreq *q = XQ(c->self);
    free(q->method); q->method = strdup(S(c->args[0].l));
    q->call = c->args[1].j;
    free(q->ctype); q->ctype = c->args[2].l ? strdup(S(c->args[2].l)) : NULL;
    q->clen = c->args[3].j;
}

static void XReq_do(tl_jcall *c)
{
    xreq *q = XQ(c->self);
    int64_t call = c->args[0].j;
    void *env = tl_jni_env(), *self = c->self;
    typedef void (*done_fn)(void *env, void *self, int64_t call, void *response);
    typedef void (*fail_fn)(void *env, void *self, int64_t call, void *cls, void *trace, void *net, uint8_t unknown_host);
    typedef int (*read_fn)(void *env, void *stream, int64_t call, int64_t offset, void *buf, int64_t buf_off, int64_t len);
    done_fn done = x_native(XREQ, "OnRequestCompleted", "(JLcom/xbox/httpclient/HttpClientResponse;)V", "Java_com_xbox_httpclient_HttpClientRequest_OnRequestCompleted");
    fail_fn fail = x_native(XREQ, "OnRequestFailed", "(JLjava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V", "Java_com_xbox_httpclient_HttpClientRequest_OnRequestFailed");
    if (!done || !fail) return;

    /* the body, which the library holds and hands over in pieces on request */
    uint8_t *body = NULL; size_t body_len = 0;
    if (q->clen > 0) {
        read_fn rd = x_native(XIN, "nativeRead", "(JJ[BJJ)I", "Java_com_xbox_httpclient_HttpClientRequestBody_00024NativeInputStream_nativeRead");
        if (rd) {
            const int64_t CH = 64 * 1024;
            jobj *buf = tl_jni_new_prim_array('B', (uint32_t)CH);
            jobj *stream = tl_jni_new_object(tl_jni_class(XIN));
            for (;;) {
                int n = rd(env, stream, call, (int64_t)body_len, buf, 0, CH);
                if (n <= 0) break;
                body = realloc(body, body_len + (size_t)n);
                memcpy(body + body_len, buf->arr.data, (size_t)n);
                body_len += (size_t)n;
            }
            tl_jni_unref(buf); tl_jni_unref(stream);
        }
    }

    tl_http_request rq = { .url = q->url, .method = q->method, .nheaders = q->nh, .header_names = (const char *const *)q->hn,
                           .header_values = (const char *const *)q->hv, .body = body, .body_len = body_len, .timeout_ms = 60000, .follow_redirects = true };
    xres *res = calloc(1, sizeof(*res));
    res->call = call;
    tl_log_line("http: %s %s (%zu bytes sent)", q->method ? q->method : "GET", q->url ? q->url : "", body_len);
    if (!tl_http_perform(&rq, &res->rs)) {
        tl_log_line("http: %s failed: %s", q->url ? q->url : "", res->rs.message);
        const char *cls = res->rs.error == TL_HTTP_UNKNOWN_HOST ? "java.net.UnknownHostException" : res->rs.error == TL_HTTP_TIMEOUT ? "java.net.SocketTimeoutException"
                        : (res->rs.error == TL_HTTP_SSL || res->rs.error == TL_HTTP_SSL_UNTRUSTED) ? "javax.net.ssl.SSLHandshakeException" : "java.io.IOException";
        jobj *a = tl_jni_new_string(cls), *b = tl_jni_new_string(res->rs.message), *n = tl_jni_new_string("Has active network: true");
        fail(env, self, call, a, b, n, res->rs.error == TL_HTTP_UNKNOWN_HOST);
        tl_jni_unref(a); tl_jni_unref(b); tl_jni_unref(n);
        tl_http_response_free(&res->rs); free(res);
    } else {
        tl_log_line("http: %s -> %d (%zu bytes)", q->url ? q->url : "", res->rs.status, res->rs.body_len);
        jobj *r = tl_jni_new_object(tl_jni_class(XRES));
        r->native = res;
        done(env, self, call, r);                 /* the library reads the response back, through the methods below */
        tl_jni_unref(r);
    }
    free(body);
}

static xres *XR(const tl_jcall *c) { return c->self ? c->self->native : NULL; }
static void XRes_code(tl_jcall *c) { const xres *r = XR(c); c->ret = vi(r ? r->rs.status : 0); }
static void XRes_numHeaders(tl_jcall *c) { const xres *r = XR(c); c->ret = vi(r && r->rs.nheaders > 1 ? r->rs.nheaders - 1 : 0); }   /* the first is the status line */
static void XRes_headerName(tl_jcall *c) { const xres *r = XR(c); int i = c->args[0].i + 1; c->ret = vl(r && i > 0 && i < r->rs.nheaders ? tl_jni_new_string(r->rs.header_names[i]) : NULL); }
static void XRes_headerValue(tl_jcall *c) { const xres *r = XR(c); int i = c->args[0].i + 1; c->ret = vl(r && i > 0 && i < r->rs.nheaders ? tl_jni_new_string(r->rs.header_values[i]) : NULL); }
static void XRes_body(tl_jcall *c)
{
    xres *r = XR(c);
    typedef void (*write_fn)(void *env, void *stream, int64_t call, void *bytes, int off, int len);
    write_fn wr = x_native(XOUT, "nativeWrite", "(J[BII)V", "Java_com_xbox_httpclient_HttpClientResponse_00024NativeOutputStream_nativeWrite");
    if (!r || !wr) return;
    void *env = tl_jni_env();
    jobj *stream = tl_jni_new_object(tl_jni_class(XOUT));
    const size_t CH = 64 * 1024;
    jobj *buf = tl_jni_new_prim_array('B', (uint32_t)CH);
    for (size_t off = 0; off < r->rs.body_len;) {
        size_t n = r->rs.body_len - off < CH ? r->rs.body_len - off : CH;
        memcpy(buf->arr.data, r->rs.body + off, n);
        wr(env, stream, r->call, buf, 0, (int)n);
        off += n;
    }
    tl_jni_unref(buf); tl_jni_unref(stream);
    free(r->rs.body); r->rs.body = NULL; r->rs.body_len = 0;
}

static const tl_jhle k_xbox[] = {
    M_(XREQ, "<init>", "(Landroid/content/Context;)V", XReq_init), M_(XREQ, "setHttpUrl", "(Ljava/lang/String;)V", XReq_url),
    M_(XREQ, "setHttpHeader", "(Ljava/lang/String;Ljava/lang/String;)V", XReq_header),
    M_(XREQ, "setHttpMethodAndBody", "(Ljava/lang/String;JLjava/lang/String;J)V", XReq_methodAndBody), M_(XREQ, "doRequestAsync", "(J)V", XReq_do),
    M_(XRES, "getResponseCode", "()I", XRes_code), M_(XRES, "getNumHeaders", "()I", XRes_numHeaders),
    M_(XRES, "getHeaderNameAtIndex", "(I)Ljava/lang/String;", XRes_headerName), M_(XRES, "getHeaderValueAtIndex", "(I)Ljava/lang/String;", XRes_headerValue),
    M_(XRES, "getResponseBodyBytes", "()V", XRes_body),
    M_("com/xbox/httpclient/NetworkObserver", "Initialize", "(Landroid/content/Context;)V", Noop), M_("com/xbox/httpclient/NetworkObserver", "Cleanup", "(Landroid/content/Context;)V", Noop),
    { NULL, NULL, NULL, NULL }
};

void tl_http_install(void)
{
    tl_jni_declare("android/net/NetworkInfo", "java/lang/Object"); tl_jni_declare("android/net/Network", "java/lang/Object");
    tl_jni_declare("android/net/LinkProperties", "java/lang/Object"); tl_jni_declare("java/net/InetAddress", "java/lang/Object");
    tl_jni_declare("android/net/NetworkCapabilities", "java/lang/Object");
    tl_jni_register_hle(k_net);
    tl_jni_declare("com/xbox/httpclient/HttpClientRequest", "java/lang/Object"); tl_jni_declare("com/xbox/httpclient/HttpClientResponse", "java/lang/Object");
    tl_jni_declare("com/xbox/httpclient/HttpClientRequestBody$NativeInputStream", "java/lang/Object"); tl_jni_declare("com/xbox/httpclient/HttpClientResponse$NativeOutputStream", "java/lang/Object");
    tl_jni_declare("com/xbox/httpclient/NetworkObserver", "java/lang/Object");
    tl_jni_register_hle(k_xbox);
    tl_jni_declare("java/util/HashMap", "java/lang/Object");
    tl_jni_declare(CLS, "java/lang/Object");
    tl_jni_register_hle(k_hle);
}
