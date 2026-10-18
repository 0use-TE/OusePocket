// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif

#define OUSE_RECENTS_CAPACITY 24
typedef struct {
    const char *id;   // unique ASCII id, 1..47 bytes
    const char *name; // UTF-8, 1..47 bytes
    const char *icon; // built-in Montserrat symbol, 1..15 bytes
    // Short UI-task callbacks only. root belongs to the manager.
    esp_err_t (*open)(lv_obj_t *root, void *context);
    // Called for both successful close and failed/partial open. Must tolerate
    // partial initialization, cancel every timer/async resource; do not delete root.
    void (*close)(void *context);
    // Background keeps the instance. Pause its UI work; a script runtime may
    // independently continue declared, explicitly requested background work.
    void (*pause)(void *context);
    void (*resume)(void *context);
    // UI-only optional local navigation (e.g. dismiss an app-owned modal).
    // Return true when Back was consumed; never execute JavaScript here.
    bool (*back)(void *context);
    void *context; // lives for the firmware lifetime
    // Optional app-owned translations. A missing language uses name verbatim.
    const char *name_zh, *name_en;
    // Borrowed image; it must outlive the registered app and every UI using it.
    const lv_image_dsc_t *icon_image;
} ouse_app_definition_t;

typedef struct {
    char id[48], name[48], icon[16];
    // name is resolved for the current locale; these retain untranslated input.
    char default_name[48], name_zh[48], name_en[48];
    const lv_image_dsc_t *icon_image;
} ouse_app_info_t;

typedef struct {
    unsigned registered, live_instances;
    uint32_t opened, closed, failed_opens;
    uint32_t preview_bytes;
    char active_id[48];
} ouse_app_stats_t;

// UI task/lock only. Foreign tasks submit via ouse_core_post_ui. Opening the
// active id is a no-op; switching backgrounds the old instance. Unknown ids keep it.
esp_err_t ouse_app_open(const char *id);
esp_err_t ouse_app_close(void);
esp_err_t ouse_app_background(void);
bool ouse_app_back(void);
esp_err_t ouse_app_close_id(const char *id);
esp_err_t ouse_app_get_recent(unsigned index, ouse_app_info_t *info);
// Borrowed preview; delete any UI images using it before open/background/close.
const lv_image_dsc_t *ouse_app_get_preview(const char *id);
esp_err_t ouse_app_get_info(unsigned index, ouse_app_info_t *info);
esp_err_t ouse_app_find_info(const char *id, ouse_app_info_t *info);
esp_err_t ouse_app_get_stats(ouse_app_stats_t *stats);

#ifdef __cplusplus
}
#endif
