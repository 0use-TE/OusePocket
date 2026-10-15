// SPDX-License-Identifier: MIT
// Explicit adapter for the published Waveshare BSP, not a device plugin system.
#include "ouse_board.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_lv_adapter.h"

static bool board_lock(void *context, uint32_t timeout_ms)
{
    (void)context;
    return bsp_display_lock(timeout_ms);
}

static void board_unlock(void *context)
{
    (void)context;
    bsp_display_unlock();
}

static esp_err_t board_brightness(void *context, uint8_t percent)
{
    (void)context;
    return bsp_display_brightness_set(percent);
}

esp_err_t ouse_board_init(ouse_core_port_t *port)
{
    if (!port) return ESP_ERR_INVALID_ARG;
    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
    };
    // Keep the UI stack in internal RAM. The BSP owns display DMA and touch.
    cfg.lv_adapter_cfg.task_stack_size = 8 * 1024;
    cfg.lv_adapter_cfg.stack_in_psram = false;
    cfg.lv_adapter_cfg.task_min_delay_ms = 5;
    cfg.lv_adapter_cfg.task_max_delay_ms = 20;
    lv_display_t *display = bsp_display_start_with_config(&cfg);
    if (!display || !bsp_display_get_input_dev()) return ESP_FAIL;
    *port = (ouse_core_port_t){
        .display = display,
        .touch = bsp_display_get_input_dev(),
        .lock = board_lock,
        .unlock = board_unlock,
        .set_brightness = board_brightness,
        .board_name = "Waveshare P4 LCD 3.5",
    };
    return ESP_OK;
}
