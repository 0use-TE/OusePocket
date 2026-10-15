// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t internal_free, internal_total;
    uint32_t psram_free, psram_total;
    uint32_t flash_bytes, cpu_mhz, reset_reason;
    uint64_t uptime_ms;
    const char *idf_version;
} ouse_system_info_t;

typedef struct {
    bool pending;
    uint8_t requested_brightness, saved_brightness;
    uint8_t requested_theme, saved_theme; // 0 light, 1 dark
    esp_err_t last_error;
} ouse_settings_state_t;

// Call once at boot before other APIs. Never erases NVS on initialization errors.
// A failure leaves system queries usable; brightness falls back to the default.
esp_err_t ouse_system_init(uint8_t *brightness);
esp_err_t ouse_system_get_info(ouse_system_info_t *info);
// Enqueues the latest setting. Flash operations run only on the settings worker.
// ESP_OK means accepted, not committed; inspect get_settings_state for completion.
esp_err_t ouse_system_request_brightness(uint8_t brightness);
esp_err_t ouse_system_request_theme(uint8_t theme);
esp_err_t ouse_system_request_settings(uint8_t brightness, uint8_t theme);
esp_err_t ouse_system_get_settings_state(ouse_settings_state_t *state);

#ifdef __cplusplus
}
#endif
