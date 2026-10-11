/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The system's trusted root certificates, as Java hands them to native code.
 *
 * Unity's TLS (mbedtls inside libunity) does not carry a CA list: on Android it asks
 * Java for the platform's -- TrustManagerFactory.getInstance(...).init(null), then
 * getTrustManagers()[0].getAcceptedIssuers(), then getEncoded() on each certificate --
 * and trusts what comes back. Without an answer every HTTPS request fails with
 * "NOT_TRUSTED", and the game cannot even register its player.
 *
 * iOS gives an app no way to list its root store, so the roots come from a PEM bundle
 * (Mozilla's, shipped with the app); the host harness defaults to the Mac's /etc/ssl/cert.pem.
 */
#define _DARWIN_C_SOURCE
#include "husk-tl-jni.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tl_log_line(const char *fmt, ...);

static char g_bundle[1024] = "/etc/ssl/cert.pem";
void tl_hle_set_ca_bundle(const char *path) { if (path) snprintf(g_bundle, sizeof(g_bundle), "%s", path); }

typedef struct { uint8_t *der; size_t len; } der_cert;
static der_cert *g_certs;
static size_t g_ncerts;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static int b64val(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static void load_bundle(void)
{
    FILE *f = fopen(g_bundle, "rb");
    if (!f) { tl_log_line("tls: no CA bundle at %s: HTTPS will fail to verify", g_bundle); return; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return; }
    fclose(f);
    buf[n] = 0;
    size_t cap = 0;
    const char *begin = "-----BEGIN CERTIFICATE-----", *end = "-----END CERTIFICATE-----";
    for (char *p = buf; (p = strstr(p, begin)); ) {
        p += strlen(begin);
        char *e = strstr(p, end);
        if (!e) break;
        uint8_t *der = malloc((size_t)(e - p) * 3 / 4 + 4);
        size_t dl = 0; uint32_t acc = 0; int bits = 0;
        for (char *q = p; q < e; q++) {
            int v = b64val((unsigned char)*q);
            if (v < 0) continue;
            acc = (acc << 6) | (uint32_t)v; bits += 6;
            if (bits >= 8) { bits -= 8; der[dl++] = (uint8_t)(acc >> bits); }
        }
        if (g_ncerts == cap) { cap = cap ? cap * 2 : 256; g_certs = realloc(g_certs, cap * sizeof(*g_certs)); }
        g_certs[g_ncerts].der = der; g_certs[g_ncerts].len = dl; g_ncerts++;
        p = e + strlen(end);
    }
    free(buf);
    tl_log_line("tls: %zu root certificates from %s", g_ncerts, g_bundle);
}

static jvalue vl(void *p) { jvalue v; v.j = 0; v.l = p; return v; }

static void TMF_getDefaultAlgorithm(tl_jcall *c) { c->ret = vl(tl_jni_new_string("PKIX")); }
static void TMF_getInstance(tl_jcall *c) { c->ret = vl(tl_jni_new_object(tl_jni_class("javax/net/ssl/TrustManagerFactory"))); }
static void TMF_init(tl_jcall *c) { (void)c; }
static void TMF_getTrustManagers(tl_jcall *c)
{
    jobj *arr = tl_jni_new_obj_array(tl_jni_class("javax/net/ssl/TrustManager"), 1);
    arr->oarr.v[0] = tl_jni_new_object(tl_jni_class("com/android/org/conscrypt/TrustManagerImpl"));
    c->ret = vl(arr);
}
static void TM_getAcceptedIssuers(tl_jcall *c)
{
    pthread_once(&g_once, load_bundle);
    jobj *arr = tl_jni_new_obj_array(tl_jni_class("java/security/cert/X509Certificate"), (uint32_t)g_ncerts);
    for (size_t i = 0; i < g_ncerts; i++) {
        jobj *cert = tl_jni_new_object(tl_jni_class("com/android/org/conscrypt/OpenSSLX509Certificate"));
        cert->native = &g_certs[i];
        arr->oarr.v[i] = cert;
    }
    c->ret = vl(arr);
}
static void Cert_getEncoded(tl_jcall *c)
{
    const der_cert *d = c->self ? c->self->native : NULL;
    if (!d) return;
    jobj *a = tl_jni_new_prim_array('B', (uint32_t)d->len);
    memcpy(a->arr.data, d->der, d->len);
    c->ret = vl(a);
}
static void Noop(tl_jcall *c) { (void)c; }

#define M(c, n, s, f) { c, n, s, f }
static const tl_jhle k_tls_hle[] = {
    M("javax/net/ssl/TrustManagerFactory", "getDefaultAlgorithm", "()Ljava/lang/String;", TMF_getDefaultAlgorithm),
    M("javax/net/ssl/TrustManagerFactory", "getInstance", "(Ljava/lang/String;)Ljavax/net/ssl/TrustManagerFactory;", TMF_getInstance),
    M("javax/net/ssl/TrustManagerFactory", "init", "(Ljava/security/KeyStore;)V", TMF_init),
    M("javax/net/ssl/TrustManagerFactory", "getTrustManagers", "()[Ljavax/net/ssl/TrustManager;", TMF_getTrustManagers),
    M("javax/net/ssl/X509TrustManager", "getAcceptedIssuers", "()[Ljava/security/cert/X509Certificate;", TM_getAcceptedIssuers),
    M("java/security/cert/Certificate", "getEncoded", "()[B", Cert_getEncoded),
    M("java/security/cert/X509Certificate", "getEncoded", "()[B", Cert_getEncoded),
    M("java/security/KeyStore", "getInstance", "(Ljava/lang/String;)Ljava/security/KeyStore;", Noop),
    { NULL, NULL, NULL, NULL }
};

void tl_tls_install(void)
{
    tl_jni_declare("javax/net/ssl/TrustManagerFactory", "java/lang/Object");
    tl_jni_declare("javax/net/ssl/TrustManager", "java/lang/Object");
    tl_jni_declare("javax/net/ssl/X509TrustManager", "javax/net/ssl/TrustManager");
    tl_jni_declare("com/android/org/conscrypt/TrustManagerImpl", "javax/net/ssl/X509TrustManager");
    tl_jni_declare("java/security/KeyStore", "java/lang/Object");
    tl_jni_declare("java/security/cert/Certificate", "java/lang/Object");
    tl_jni_declare("java/security/cert/X509Certificate", "java/security/cert/Certificate");
    tl_jni_declare("com/android/org/conscrypt/OpenSSLX509Certificate", "java/security/cert/X509Certificate");
    tl_jni_register_hle(k_tls_hle);
}
