// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"
#include "ouse_system.h"
#include "ouse_app.h"
#include "ouse_theme.h"
#include "ouse_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OUSE_CORE_VERSION "0.28.0"
#define OUSE_CORE_API_VERSION 31

// Immutable board specifications; these describe hardware, not current usage.
typedef struct {
    const char *processor, *memory, *storage, *display, *touch;
    const char *microphone, *speaker, *wireless, *camera, *power;
} ouse_hardware_info_t;

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
    esp_err_t (*save_ui)(void *context, uint8_t locale, uint8_t font_percent);
    const char *board_name;
    const ouse_hardware_info_t *hardware;
} ouse_core_port_t;

typedef struct {
    ouse_core_port_t port;
    uint8_t initial_brightness; // 10..100: keep the screen visible during tests.
    const ouse_app_definition_t *apps; // optional additional apps; settings is built in
    ouse_theme_t initial_theme;
    uint16_t density_dpi; // logical density; 0 defaults to 160, not physical DPI
    uint8_t initial_locale, initial_font_percent; // zh_CN=0 / en=1; font is 0(default) or 100 only
    size_t app_count;
} ouse_core_config_t;

// Creates the default desktop and the built-in display settings. No runtime,
// service manager, filesystem or network is started here. Call once at boot;
// do not start concurrently. The caller must not already hold the UI lock.
esp_err_t ouse_core_start(const ouse_core_config_t *config);
bool ouse_core_is_started(void);
// UI task only. Dynamically installed apps use the reserved user.* namespace.
esp_err_t ouse_core_user_check(const char *id);
esp_err_t ouse_core_user_apply(const ouse_app_definition_t *definition);
esp_err_t ouse_core_user_remove(const char *id);
esp_err_t ouse_core_user_maintenance(const char *id,bool enabled);
void ouse_core_app_size(int32_t *width, int32_t *height);
void ouse_core_launcher_restored(void);
esp_err_t ouse_core_set_ui(unsigned locale, unsigned font_percent); // UI task only
esp_err_t ouse_core_set_brightness(uint8_t percent); // apply and asynchronously save
esp_err_t ouse_core_preview_brightness(uint8_t percent); // no Flash request
uint8_t ouse_core_brightness(void);
void ouse_core_launcher_state(unsigned *page,unsigned *pages);
esp_err_t ouse_core_sleep_display(void); // UI task; backlight off, not deep sleep
esp_err_t ouse_core_wake_display(void);
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
