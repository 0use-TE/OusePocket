// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

#define OUSE_TASK_CAPACITY 96
#define OUSE_TASK_ACTIVE_LIMIT 64
#define OUSE_TASK_OWNER_LIMIT 8
#define OUSE_TASK_OWNERS_MAX 12
typedef struct { char app_id[48]; uint32_t instance; } ouse_task_owner_t;
typedef enum { OUSE_TASK_WIFI, OUSE_TASK_AUDIO, OUSE_TASK_STORAGE, OUSE_TASK_SETTINGS, OUSE_TASK_PACKAGE,
    OUSE_TASK_NETWORK, OUSE_TASK_ALARM, OUSE_TASK_CAMERA, OUSE_TASK_NATIVE } ouse_task_kind_t;
typedef enum { OUSE_TASK_QUEUED, OUSE_TASK_RUNNING, OUSE_TASK_CANCELLING,
    OUSE_TASK_SUCCEEDED, OUSE_TASK_FAILED, OUSE_TASK_CANCELLED } ouse_task_state_t;
typedef enum { OUSE_TASK_FINISH_ON_CLOSE, OUSE_TASK_STOP_ON_CLOSE } ouse_task_policy_t;
// Small, typed completion summaries. Large/private storage values stay in the
// storage completion channel; no result retains a provider's buffer or pointer.
typedef struct {
    bool present;
    union {
        struct { bool enabled,connected; unsigned networks; int reason; char ssid[33],ip[16]; } wifi;
        struct { unsigned recorded_ms,position_ms; } audio;
        struct { size_t json_bytes; } storage;
        struct { int status; size_t body_bytes; } network;
        struct { char id[32]; } alarm;
    } value;
} ouse_task_result_t;
typedef struct {
    uint32_t id, revision;
    ouse_task_owner_t owner;
    ouse_task_kind_t kind;
    ouse_task_state_t state;
    ouse_task_policy_t policy;
    char operation[24], cancel_reason[24];
    bool cancel_requested, cancellable, detached, delivery_pending, result_held;
    esp_err_t error;
    ouse_task_result_t result;
    int64_t created_us, started_us, finished_us;
} ouse_task_snapshot_t;
typedef struct {
    unsigned records, active, pending_delivery, pending_results, owners,submitting;
    uint32_t rejected, recycled, revision;
    size_t registry_bytes;
} ouse_task_stats_t;

// instance=0 is reserved for system services. IDs never repeat within a boot.
esp_err_t ouse_task_owner_open(const char *app_id,ouse_task_owner_t *out);
void ouse_task_owner_close(const ouse_task_owner_t *owner);
esp_err_t ouse_task_create(const ouse_task_owner_t *owner,ouse_task_kind_t kind,
    const char *operation,ouse_task_policy_t policy,bool cancellable,uint32_t *id);
// Atomically claims queued work, or finishes a cancelled queued request.
bool ouse_task_start(uint32_t id,bool cancellable_while_running);
bool ouse_task_cancel_requested(uint32_t id);
esp_err_t ouse_task_cancel(const ouse_task_owner_t *owner,uint32_t id,const char *reason);
// A terminal transition happens once, only after native work has stopped.
void ouse_task_finish(uint32_t id,esp_err_t error,bool cancelled);
void ouse_task_finish_result(uint32_t id,esp_err_t error,bool cancelled,const ouse_task_result_t *result);
bool ouse_task_get(const ouse_task_owner_t *owner,uint32_t id,ouse_task_snapshot_t *out);
unsigned ouse_task_list(const ouse_task_owner_t *owner,ouse_task_snapshot_t *out,unsigned capacity,bool pending_only);
bool ouse_task_next(const ouse_task_owner_t *owner,uint32_t after_id,ouse_task_snapshot_t *out,bool pending_only);
void ouse_task_ack(const ouse_task_owner_t *owner,uint32_t id);
// Payload channel pins history until its result is consumed/discarded. Set
// before enqueue, clear on rejection/consumption. Owner close revokes the pin.
void ouse_task_hold_result(uint32_t id,bool held);
void ouse_tasks_get_stats(ouse_task_stats_t *out);
// Install once before JS launches; notification never runs under registry lock.
void ouse_tasks_set_notify(void (*notify)(void));
// System drain gate. Existing work still starts/finishes; new requests/owners
// are rejected. Only system settings bookkeeping may register during drain.
void ouse_tasks_quiesce(bool enabled);
bool ouse_tasks_quiescing(void);
// Admission for short setters which mutate state before worker registration.
bool ouse_tasks_submission_begin(void);
void ouse_tasks_submission_end(void);
void ouse_tasks_cancel_active(const char *app_id,const char *reason);
const char *ouse_task_state_name(ouse_task_state_t state);
const char *ouse_task_kind_name(ouse_task_kind_t kind);
#ifdef __cplusplus
}
#endif
