// SPDX-License-Identifier: MIT
// Explicit adapter for the published Waveshare BSP, not a device plugin system.
#include "ouse_board.h"
#include "driver/gpio.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_lv_adapter.h"
#include "display_flush_wait.h"
#include "sdkconfig.h"
#if CONFIG_OUSE_DEVICE_DEBUG
#include "esp_lcd_panel_ops.h"

static lv_display_t *debug_display;
static bool debug_lcd_enabled = true;
static uint32_t debug_lcd_skipped;
static bool debug_flush_wait_enabled = true;
static uint32_t debug_flush_waits;

static void debug_flush_wait(lv_display_t *display)
{
    ++debug_flush_waits;
    ouse_display_flush_wait(display);
}

// UI task only. This A/B control leaves the LCD/SPI transfer path unchanged.
// -1 queries; 1 enables yielding waits; 0 restores LVGL's default tight poll.
esp_err_t ouse_board_debug_flush_wait(int enabled, bool *current_enabled, uint32_t *waits)
{
    if (enabled < -1 || enabled > 1 || !current_enabled || !waits) return ESP_ERR_INVALID_ARG;
    if (!debug_display) return ESP_ERR_INVALID_STATE;
    if (enabled >= 0 && (enabled != 0) != debug_flush_wait_enabled) {
        lv_display_set_flush_wait_cb(debug_display, enabled ? debug_flush_wait : NULL);
        debug_flush_wait_enabled = enabled != 0;
    }
    *current_enabled = debug_flush_wait_enabled;
    *waits = debug_flush_waits;
    return ESP_OK;
}

static esp_err_t debug_skip_bitmap(lv_display_t *display, esp_lcd_panel_handle_t panel,
                                   int x_start, int y_start, int x_end, int y_end,
                                   const void *pixels, void *context)
{
    (void)display; (void)panel;
    (void)x_start; (void)y_start; (void)x_end; (void)y_end;
    (void)pixels; (void)context;
    ++debug_lcd_skipped;
    // The adapter completes the flush itself for this return value.
    return ESP_ERR_NOT_ALLOWED;
}

// Diagnostic hook, called only on the LVGL task. -1 queries without changing
// state; 0 skips LCD transfers while retaining rendering; 1 restores them.
esp_err_t ouse_board_debug_lcd_transfer(int enabled, bool *current_enabled, uint32_t *skipped)
{
    if (enabled < -1 || enabled > 1 || !current_enabled || !skipped) return ESP_ERR_INVALID_ARG;
    if (!debug_display) return ESP_ERR_INVALID_STATE;
    if (enabled >= 0 && (enabled != 0) != debug_lcd_enabled) {
        esp_err_t error;
        if (enabled) {
            error = esp_lv_adapter_set_draw_bitmap_callbacks(debug_display, NULL, NULL);
        } else {
            // A synchronous panel command drains the previous DMA and its done
            // ISR before a skipped flush can reuse the single drawing buffer.
            error = esp_lcd_panel_disp_on_off(bsp_display_get_panel_handle(), true);
            if (error != ESP_OK) return error;
            const esp_lv_adapter_draw_bitmap_callbacks_t callbacks = {
                .custom_draw_bitmap = debug_skip_bitmap,
            };
            error = esp_lv_adapter_set_draw_bitmap_callbacks(debug_display, &callbacks, NULL);
        }
        if (error != ESP_OK) return error;
        debug_lcd_enabled = enabled != 0;
        if (debug_lcd_enabled) lv_obj_invalidate(lv_display_get_screen_active(debug_display));
    }
    *current_enabled = debug_lcd_enabled;
    *skipped = debug_lcd_skipped;
    return ESP_OK;
}
#endif

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

static const ouse_hardware_info_t hardware = {
    .processor = "ESP32-P4 · Dual-core RISC-V · 400 MHz",
    .memory = "32 MB PSRAM",
    .storage = "16 MB Flash · microSD",
    .display = "3.5\" IPS · 320 × 480 · ST7796 · SPI",
    .touch = "FT6336 · I²C",
    .microphone = "Analog microphone · ES8311 · I²S",
    .speaker = "ES8311 · 8 ohm / 2 W",
    .wireless = "ESP32-C6 · Wi-Fi 6 · Bluetooth LE",
    .camera = "OV5647 · 5 MP · MIPI-CSI",
    .power = "AXP2101",
};

static bool board_key(void *context,unsigned key)
{(void)context;return key==0 ? gpio_get_level(GPIO_NUM_35)==0 : key==1 && gpio_get_level(GPIO_NUM_49)!=0;}
esp_err_t ouse_board_init(ouse_core_port_t *port)
{
    if (!port) return ESP_ERR_INVALID_ARG;
    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
    };
    // Keep the UI stack in internal RAM. The BSP owns display DMA and touch.
    // Leave headroom for LVGL, native controls and diagnostics. Application
    // JavaScript runs on its separate system-owned worker.
    cfg.lv_adapter_cfg.task_stack_size = 48 * 1024;
    cfg.lv_adapter_cfg.stack_in_psram = false;
    cfg.lv_adapter_cfg.task_min_delay_ms = 5;
    cfg.lv_adapter_cfg.task_max_delay_ms = 20;
    lv_display_t *display = bsp_display_start_with_config(&cfg);
    if (!display || !bsp_display_get_input_dev()) return ESP_FAIL;
    // The adapter has started the LVGL worker. Install the board's DMA wait
    // while holding that worker's lock, before creating the desktop/apps.
    if (!bsp_display_lock(2000)) return ESP_ERR_TIMEOUT;
#if CONFIG_OUSE_DEVICE_DEBUG
    debug_display = display;
    lv_display_set_flush_wait_cb(display, debug_flush_wait);
#else
    lv_display_set_flush_wait_cb(display, ouse_display_flush_wait);
#endif
    bsp_display_unlock();
    gpio_config_t keys={.pin_bit_mask=(1ULL<<35)|(1ULL<<49),.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_DISABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
    esp_err_t key_error=gpio_config(&keys);if(key_error!=ESP_OK)return key_error;
    *port = (ouse_core_port_t){
        .display = display,
        .touch = bsp_display_get_input_dev(),
        .lock = board_lock,
        .unlock = board_unlock,
        .set_brightness = board_brightness,
        .read_key=board_key,
        .board_name = "Waveshare P4 LCD 3.5",
        .hardware = &hardware,
    };
    extern esp_err_t ouse_board_devices_init(void);
    return ouse_board_devices_init();
}
