// SPDX-License-Identifier: MIT
#include "ouse_board.h"
#include "ouse_core.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "counter_app.h"

static esp_err_t save_brightness(void *context, uint8_t percent)
{
    (void)context;
    return ouse_system_request_settings(percent,(uint8_t)ouse_theme_get());
}

static esp_err_t save_theme(void *context, uint8_t theme, uint8_t brightness)
{
    (void)context; return ouse_system_request_settings(brightness,theme);
}

static esp_err_t settings_state(void *context, ouse_settings_state_t *state)
{
    (void)context;
    return ouse_system_get_settings_state(state);
}

static esp_err_t system_info(void *context, ouse_system_info_t *info)
{
    (void)context;
    return ouse_system_get_info(info);
}

void app_main(void)
{
    ESP_LOGI("OuseCore", "Starting core %s; API %d; reset reason %d",
             ouse_core_version(), OUSE_CORE_API_VERSION, esp_reset_reason());
    ouse_core_config_t config = {.initial_brightness = 45, .apps = &ouse_counter_app, .app_count = 1};
    esp_err_t settings_error = ouse_system_init(&config.initial_brightness);
    if (settings_error != ESP_OK) {
        ESP_LOGW("OuseCore", "Settings unavailable: %s; NVS is preserved", esp_err_to_name(settings_error));
    }
    ouse_settings_state_t stored;
    if (ouse_system_get_settings_state(&stored)==ESP_OK) config.initial_theme=(ouse_theme_t)stored.requested_theme;
    ESP_ERROR_CHECK(ouse_board_init(&config.port));
    config.port.save_brightness = save_brightness;
    config.port.save_theme = save_theme;
    config.port.get_settings_state = settings_state;
    config.port.get_system_info = system_info;
    ESP_ERROR_CHECK(ouse_core_start(&config));
    ESP_LOGI("OuseCore", "DESKTOP READY; core=%s; internal_free=%u; psram_free=%u",
             ouse_core_version(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}
