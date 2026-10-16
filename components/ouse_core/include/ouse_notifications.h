// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

#define OUSE_NOTIFICATION_CAPACITY 24
#define OUSE_NOTIFICATION_APP_CAPACITY 6
#define OUSE_NOTIFICATION_APP_ID_SIZE 48
#define OUSE_NOTIFICATION_ID_SIZE 48
#define OUSE_NOTIFICATION_TITLE_SIZE 128
#define OUSE_NOTIFICATION_BODY_SIZE 384
#define OUSE_NOTIFICATION_PAYLOAD_SIZE 256

typedef struct {
    const char *id, *title, *body, *payload;
    bool ongoing;
} ouse_notification_request_t;
typedef struct {
    uint32_t serial;
    char app_id[OUSE_NOTIFICATION_APP_ID_SIZE], id[OUSE_NOTIFICATION_ID_SIZE];
    char title[OUSE_NOTIFICATION_TITLE_SIZE], body[OUSE_NOTIFICATION_BODY_SIZE];
    char payload[OUSE_NOTIFICATION_PAYLOAD_SIZE];
    bool ongoing;
    uint64_t created_ms, updated_ms;
} ouse_notification_t;
typedef struct {
    uint32_t token;
    char id[OUSE_NOTIFICATION_ID_SIZE], payload[OUSE_NOTIFICATION_PAYLOAD_SIZE];
} ouse_notification_open_t;

// Initialize once at core startup. Other operations are thread-safe, copy all
// data and never retain JS values, UI objects or package-owned pointers.
esp_err_t ouse_notifications_init(void);
esp_err_t ouse_notifications_publish(const char *app_id,const ouse_notification_request_t *request);
esp_err_t ouse_notifications_cancel(const char *app_id,const char *id);
// A NULL app_id requests the system view. Returns records copied, newest first.
unsigned ouse_notifications_list(const char *app_id,ouse_notification_t *out,unsigned capacity);
unsigned ouse_notifications_count(const char *app_id);
uint32_t ouse_notifications_generation(void);
// User dismissal never removes ongoing records; application cancel may do so.
esp_err_t ouse_notifications_dismiss(uint32_t serial);
unsigned ouse_notifications_clear_all(void);
void ouse_notifications_remove_app(const char *app_id);

// UI queues an immutable open intent before opening its owner. A closed app
// can install its handler at startup without losing the event. Queues are
// bounded (8 total, 4 per app); undelivered intents expire after five minutes.
esp_err_t ouse_notifications_activate(uint32_t serial,char *app_id,unsigned app_id_size,uint32_t *token);
bool ouse_notifications_peek_open(const char *app_id,ouse_notification_open_t *out);
bool ouse_notifications_ack_open(const char *app_id,uint32_t token);
void ouse_notifications_discard_open(const char *app_id,uint32_t token);
#ifdef __cplusplus
}
#endif
