// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"
#include "ouse_system.h"
#include "ouse_app.h"
#include "ouse_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OUSE_CORE_VERSION "0.3.0"
#define OUSE_CORE_API_VERSION 3

// The board owns LVGL initialization, display/touch and its UI lock. Callbacks
// must remain valid while the core is running; brightness is a short operation.
typedef struct {
    lv_display_t *display;
    lv_indev_t *touch;
    void *context;
    bool (*lock)(void *context, uint32_t timeout_ms);
    void (*unlock)(void *context);
    esp_err_t (*set_brightness)(void *context, uint8_t percent);
    // Optional system hooks: save only enqueues; status/info only read memory.
    esp_err_t (*save_brightness)(void *context, uint8_t percent);
    esp_err_t (*save_theme)(void *context, uint8_t theme, uint8_t brightness);
    esp_err_t (*get_settings_state)(void *context, ouse_settings_state_t *state);
    esp_err_t (*get_system_info)(void *context, ouse_system_info_t *info);
    const char *board_name;
} ouse_core_port_t;

typedef struct {
    ouse_core_port_t port;
    uint8_t initial_brightness; // 10..100: keep the screen visible during tests.
    const ouse_app_definition_t *apps; // optional additional apps; settings is built in
    ouse_theme_t initial_theme;
    size_t app_count; // at most OUSE_APP_CAPACITY - 1; registration is frozen at startup
} ouse_core_config_t;

// Creates the default desktop and the built-in display settings. No runtime,
// service manager, filesystem or network is started here. Call once at boot;
// do not start concurrently. The caller must not already hold the UI lock.
esp_err_t ouse_core_start(const ouse_core_config_t *config);
bool ouse_core_is_started(void);
const char *ouse_core_version(void);
esp_err_t ouse_core_set_theme(ouse_theme_t theme); // UI task/lock only; save asynchronously

// From non-UI tasks only: enqueue a short callback for the LVGL task, at most
// eight outstanding calls. Never pass stack context that expires before dispatch.
// All external LVGL access must go through this API; UI events/timers may call
// LVGL directly. Callbacks must not block, write Flash or call post_ui again.
typedef void (*ouse_ui_callback_t)(void *context);
esp_err_t ouse_core_post_ui(ouse_ui_callback_t callback, void *context);

#ifdef __cplusplus
}
#endif
