/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * One blocking HTTP(S) request on the host's own networking (NSURLSession), for the Java HTTP clients a game's
 * Java side would have used: Unity's UnityWebRequest runs on HttpURLConnection, which has no implementation here.
 *
 * Redirects are followed only when asked for (Unity reports the 3xx and follows it itself, as it does on Android),
 * and the whole body is read before this returns.
 */
#ifndef HUSK_TL_HTTP_H
#define HUSK_TL_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tl_http_request {
    const char *url, *method;
    int nheaders;
    const char *const *header_names, *const *header_values;
    const uint8_t *body; size_t body_len;
    int timeout_ms;                       /* 0: no limit of its own */
    bool follow_redirects;                /* follow up to 10 redirects, as OkHttp does; otherwise the 3xx itself is the answer */
} tl_http_request;

/* What went wrong, in the terms Unity's UnityWebRequest uses. */
enum { TL_HTTP_OK = 0, TL_HTTP_SDK = 2, TL_HTTP_MALFORMED_URL = 5, TL_HTTP_UNKNOWN_HOST = 7, TL_HTTP_READ = 12, TL_HTTP_TIMEOUT = 14,
       TL_HTTP_SSL = 16, TL_HTTP_SSL_UNTRUSTED = 25 };

typedef struct tl_http_response {
    int error;                            /* TL_HTTP_OK, or one of the above */
    char message[256];
    int status;
    int nheaders;
    char **header_names, **header_values; /* the first is "Status": the status line */
    uint8_t *body; size_t body_len;
} tl_http_response;

bool tl_http_perform(const tl_http_request *req, tl_http_response *out);
void tl_http_response_free(tl_http_response *r);

#ifdef __cplusplus
}
#endif

#endif
