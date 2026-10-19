// SPDX-License-Identifier: MIT
#pragma once
#include "ouse_tasks.h"
#ifdef __cplusplus
extern "C" {
#endif
#define OUSE_NET_URL_MAX 512
#define OUSE_NET_BODY_MAX 8192
#define OUSE_NET_RESPONSE_MAX 32768
#define OUSE_NET_HEADERS_MAX 24
#define OUSE_NET_PENDING_MAX 8
typedef struct { char name[64], value[512]; } ouse_http_header_t;
typedef struct {
    const char *url, *method; // GET, HEAD, POST, PUT, PATCH, DELETE
    const unsigned char *body;
    size_t body_size;
    const ouse_http_header_t *headers;
    unsigned header_count, timeout_ms; // 1000..30000, includes queue residence
} ouse_http_request_t;
typedef struct {
    int status;
    size_t body_size;
    unsigned header_count;
    ouse_http_header_t headers[OUSE_NET_HEADERS_MAX];
} ouse_http_response_t;
// Native worker only. Bounded HTTP/1.1 framing; no redirect, retry, cookie jar,
// decompression, authentication negotiation, files or JS callbacks. HTTPS uses
// the fixed IDF CA bundle and hostname verification. All pointers are borrowed.
// Deadline is monotonic; DNS resolution in fixed IDF cannot be interrupted.
// stop is polled at safe boundaries. Destroy socket/TLS before returning.
typedef esp_err_t (*ouse_http_sink_t)(void *context,const unsigned char *data,size_t size);
typedef bool (*ouse_http_stop_t)(void *context);
esp_err_t ouse_http_validate(const ouse_http_request_t *request);
esp_err_t ouse_http_transfer(const ouse_http_request_t *request,int64_t deadline_us,
    size_t body_limit,ouse_http_response_t *response,ouse_http_sink_t sink,
    ouse_http_stop_t stop,void *context);
typedef struct {
    ouse_http_response_t response;
    unsigned char *body; // malloc-owned, body_size bytes (also NUL terminated)
    esp_err_t error;
    bool cancelled;
} ouse_net_result_t;
typedef void (*ouse_net_complete_t)(void *context,uint32_t id,ouse_net_result_t *result);
// Boot task, once before BPKs launch. Submission only copies bounded data.
esp_err_t ouse_net_init(void);
bool ouse_net_available(void);
// Accepted request invokes complete exactly once on native network worker.
// Callback owns result, release with ouse_net_result_free. No instance pointers
// are required by this service. STOP_ON_CLOSE, terminal only after cleanup.
esp_err_t ouse_net_submit(const ouse_task_owner_t *owner,const ouse_http_request_t *request,
    uint32_t *id,ouse_net_complete_t complete,void *context);
void ouse_net_result_free(ouse_net_result_t *result);
#ifdef __cplusplus
}
#endif
