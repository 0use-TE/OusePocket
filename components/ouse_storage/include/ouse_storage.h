// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "ouse_tasks.h"
#ifdef __cplusplus
extern "C" {
#endif

#define OUSE_STORAGE_BASE "/bpkfs"
#define OUSE_STORAGE_PACKAGE_DIRECTORY OUSE_STORAGE_BASE "/ouse-apps"
#define OUSE_STORAGE_KEY_MAX 31
#define OUSE_STORAGE_VALUE_MAX 8192
#define OUSE_STORAGE_KEYS_MAX 32
#define OUSE_STORAGE_QUOTA_BYTES (64 * 1024)
#define OUSE_STORAGE_QUEUE_CAPACITY 16

typedef enum {
    OUSE_STORAGE_GET, OUSE_STORAGE_SET, OUSE_STORAGE_REMOVE,
    OUSE_STORAGE_KEYS, OUSE_STORAGE_CLEAR, OUSE_STORAGE_INFO
} ouse_storage_operation_t;

typedef struct {
    bool ready;
    uint32_t revision;
    size_t total_bytes, used_bytes, free_bytes, package_bytes, private_bytes;
    unsigned package_count, private_apps;
    esp_err_t last_error;
} ouse_storage_stats_t;

// Runs on the native storage task; never call JS or LVGL here. On success,
// result is a malloc-owned JSON value transferred to the completion callback.
// On failure it may be NULL. The callback always owns/free()s a non-NULL result.
typedef void (*ouse_storage_complete_t)(void *context,uint32_t token,esp_err_t error,char *result);

// Call from the installer task after desktop startup, never from the UI/JS task.
// Starts the native task, mounts LittleFS there and waits for the mount result.
// Only an entirely erased partition is automatically provisioned.
esp_err_t ouse_storage_start(void);
// Nonblocking and thread-safe. Accepted requests own copies of app_id/key/value.
// SET receives a valid serialized JSON value from the caller (1..8192 bytes).
// Every accepted request invokes complete once, including errors. No callbacks
// are made for rejected submissions. No filesystem I/O occurs in submit.
// The service registers ownership and writes task_id before enqueueing; callback
// token is this global task ID. A queue rejection may expose a failed task ID.
esp_err_t ouse_storage_submit(ouse_storage_operation_t operation,const ouse_task_owner_t *owner,
    const char *key,const char *value,uint32_t *task_id,ouse_storage_complete_t complete,void *context);
// Installer-only FIFO barrier: after the registry has closed/removed the app,
// drain its previously accepted writes then delete only that app's private data.
// Never call on the UI/JS task; completion has no deadline/lifetime ambiguity.
esp_err_t ouse_storage_purge(const char *app_id);
// Queue a native stats refresh after package changes. Queries only copy cache.
esp_err_t ouse_storage_refresh(void);
esp_err_t ouse_storage_get_stats(ouse_storage_stats_t *out);
#ifdef __cplusplus
}
#endif
