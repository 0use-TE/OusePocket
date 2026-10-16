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
#define OUSE_RUNTIME_DATA_BYTES_MAX (128 * 1024)
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
} ouse_script_package_t;
typedef struct { const ouse_script_package_t *package; void *instance; } ouse_script_app_t;
typedef struct {
    unsigned instances, contexts, widgets, timers, errors;
    size_t js_heap_bytes;
    unsigned background, runnable, queued_events, dropped_events, worker_stack_min;
    unsigned storage_jobs, storage_pending;
} ouse_runtime_stats_t;
// Prepare at boot, then pass the definition to ouse_core_start. State/package
// must outlive registration. Public entry points are UI task/lock only; each
// opened instance owns copied sources/assets and asynchronous worker cleanup.
esp_err_t ouse_script_prepare(ouse_script_app_t *state, const ouse_script_package_t *package,
                              ouse_app_definition_t *definition);
// UI task only: bounded wait for isolated worker schema/compile validation.
// The worker never waits for UI and never executes application JavaScript here.
esp_err_t ouse_script_validate(const ouse_script_package_t *package);
void ouse_runtime_get_stats(ouse_runtime_stats_t *stats);
#ifdef __cplusplus
}
#endif
