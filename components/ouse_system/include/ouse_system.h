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
    uint32_t internal_largest,internal_minimum,psram_largest,psram_minimum;
    uint32_t flash_bytes, cpu_mhz, reset_reason;
    uint64_t uptime_ms;
    uint64_t memory_sample_ms;
    const char *idf_version;
} ouse_system_info_t;

typedef struct {
    bool pending;
    uint8_t requested_locale, saved_locale, requested_font_percent, saved_font_percent;
    uint8_t requested_brightness, saved_brightness;
    uint8_t requested_theme, saved_theme; // 0 light, 1 dark
    bool requested_debug, saved_debug;
    esp_err_t last_error;
} ouse_settings_state_t;

// Call once at boot before other APIs. Never erases NVS on initialization errors.
// A failure leaves system queries usable; brightness falls back to the default.
esp_err_t ouse_system_init(uint8_t *brightness);
esp_err_t ouse_system_get_info(ouse_system_info_t *info);
// Native coordinator only, nominal 1 Hz. Zero timestamp means not sampled.
void ouse_system_sample_memory(void);
// Enqueues the latest setting. Flash operations run only on the settings worker.
// ESP_OK means accepted, not committed; inspect get_settings_state for completion.
esp_err_t ouse_system_request_brightness(uint8_t brightness);
esp_err_t ouse_system_request_theme(uint8_t theme);
esp_err_t ouse_system_request_ui(uint8_t locale, uint8_t font_percent);
esp_err_t ouse_system_request_debug(bool enabled);
bool ouse_system_debug_enabled(void);
esp_err_t ouse_system_request_settings(uint8_t brightness, uint8_t theme);
esp_err_t ouse_system_get_settings_state(ouse_settings_state_t *state);

#ifdef __cplusplus
}
#endif
