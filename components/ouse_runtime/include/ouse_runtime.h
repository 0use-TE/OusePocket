// SPDX-License-Identifier: MIT
#pragma once
#include "ouse_app.h"
#ifdef __cplusplus
extern "C" {
#endif
#define OUSE_RUNTIME_API_VERSION 1
#define OUSE_RUNTIME_MEMORY_LIMIT (512 * 1024)
typedef struct {
    const char *id;
    const lv_image_dsc_t *image;
} ouse_script_image_t;

#define OUSE_RUNTIME_ASSETS_MAX 16
#define OUSE_RUNTIME_IMAGE_BYTES_MAX (128 * 1024)
#define OUSE_RUNTIME_DATA_BYTES_MAX (512 * 1024)
typedef enum { OUSE_SCRIPT_ASSET_TEXT, OUSE_SCRIPT_ASSET_BINARY } ouse_script_asset_type_t;
typedef struct {
    const char *id;
    ouse_script_asset_type_t type;
    // TEXT must be valid UTF-8 without NUL; the package parser validates it.
    // Instances copy this data. Empty text/binary resources are permitted.
    const unsigned char *data;
    size_t size;
} ouse_script_asset_t;
typedef struct {
    const char *id, *name, *icon;
    unsigned api;
    const char *page, *script;
    const ouse_script_image_t *images;
    unsigned image_count;
    const ouse_script_asset_t *assets;
    unsigned asset_count;
    const char *name_zh, *name_en;
    const lv_image_dsc_t *icon_image;
    bool background_allowed; // manifest capabilities.background
    bool notifications_allowed; // manifest capabilities.notifications
    bool network_allowed; // manifest capabilities.network
    bool media_allowed, alarms_allowed, files_allowed, sdcard_allowed;
} ouse_script_package_t;
typedef struct { const ouse_script_package_t *package; void *instance; } ouse_script_app_t;
typedef struct {
    unsigned instances, contexts, widgets, timers, errors;
    size_t js_heap_bytes;
    unsigned background, runnable, queued_events, dropped_events, worker_stack_min;
    unsigned storage_jobs, storage_pending;
} ouse_runtime_stats_t;
typedef struct {
    char app_id[48], background_reason[64];
    uint32_t instance_id;
    bool visible,runnable,background_allowed,background_active,faulted;
    unsigned timers,queued_events;
    size_t js_heap_bytes,native_known_bytes;
    int64_t memory_sample_ms,background_since_ms,last_scheduled_us,last_elapsed_us;
    unsigned budget_checkpoints;
    bool wall_guard_hit;
} ouse_runtime_app_stats_t;
#define OUSE_RUNTIME_DIAGNOSTIC_APPS 12
typedef struct {
    int64_t sampled_ms;
    unsigned app_count;
    ouse_runtime_app_stats_t apps[OUSE_RUNTIME_DIAGNOSTIC_APPS];
} ouse_runtime_diagnostics_t;
// Prepare at boot, then pass the definition to ouse_core_start. State/package
// must outlive registration. Public entry points are UI task/lock only; each
// opened instance owns copied sources/assets and asynchronous worker cleanup.
esp_err_t ouse_script_prepare(ouse_script_app_t *state, const ouse_script_package_t *package,
                              ouse_app_definition_t *definition);
typedef struct ouse_script_validation ouse_script_validation_t;
// UI task/lock only: copy resources and enqueue validation, never wait for JS.
esp_err_t ouse_script_validate_begin(const ouse_script_package_t *package,ouse_script_validation_t **out);
// Native installer task only: consumes the handle on return, waits at most 2s.
// Timeout leaves independent source copies with the worker until it finishes.
esp_err_t ouse_script_validate_wait(ouse_script_validation_t *validation);
void ouse_runtime_get_stats(ouse_runtime_stats_t *stats);
// Cached worker-owned statistics; never walk a JS heap on the caller/UI task.
void ouse_runtime_get_diagnostics(ouse_runtime_diagnostics_t *out);
// Native asynchronous lease end. Unique instance ID prevents reopening races;
// does not close the instance or stop independent native activities.
esp_err_t ouse_runtime_end_background(uint32_t instance_id);
#ifdef __cplusplus
}
#endif
