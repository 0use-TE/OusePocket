// SPDX-License-Identifier: MIT
#include "bsp/display.h"
#include "esp_lv_adapter.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "display_flush_wait.h"
#include <inttypes.h>

// Keep the published BSP's panel/touch lifecycle and buffer ownership. The
// linker wrapper only tunes its display profile; managed sources stay intact.
lv_display_t *__real_esp_lv_adapter_register_display(const esp_lv_adapter_display_config_t *config);
lv_display_t *__wrap_esp_lv_adapter_register_display(const esp_lv_adapter_display_config_t *config)
{
    if (!config || config->profile.interface != ESP_LV_ADAPTER_PANEL_IF_OTHER ||
        config->profile.hor_res != BSP_LCD_H_RES || config->profile.ver_res != BSP_LCD_V_RES ||
        config->tear_avoid_mode != ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE) {
        return __real_esp_lv_adapter_register_display(config);
    }
    esp_lv_adapter_display_config_t tuned = *config;
    // LVGL pixels are copied into the compositor before flush_ready. Its
    // drawing buffer is never owned by background SPI DMA, so one 12,800-byte
    // internal buffer is sufficient (saving 38,400 bytes over 2 x 40 lines).
    tuned.profile.buffer_height = 20;
    tuned.profile.use_psram = false;
    tuned.profile.require_double_buffer = false;
    lv_display_t *display = __real_esp_lv_adapter_register_display(&tuned);
    // Install before the BSP starts the LVGL worker. Waiting until the BSP
    // returns leaves its initial screen flush using LVGL's tight polling loop.
    if (display) {
        // Match the measured ~50Hz panel cadence. A 33ms UI clock beats
        // against ~20ms scan cycles and 40ms game frames, causing uneven pacing.
        lv_timer_set_period(lv_display_get_refr_timer(display),20);
        (void)ouse_display_flush_wake_init(display);
        lv_display_set_flush_wait_cb(display, ouse_display_flush_wait);
    }
    if (display) ESP_LOGI("OuseDisplay", "CPU staging: 1 x 20 lines, internal buffer; UI cadence 20ms");
    return display;
}

#if CONFIG_OUSE_DEVICE_DEBUG
static struct {
    int64_t render_started, wait_started;
    uint64_t render_us, wait_us, bytes;
    uint32_t frames, flushes, render_max_us;
} timing;

static void display_timing(lv_event_t *event)
{
    int64_t now = esp_timer_get_time();
    switch (lv_event_get_code(event)) {
    case LV_EVENT_RENDER_START:
        timing.render_started = now;
        break;
    case LV_EVENT_RENDER_READY: {
        uint32_t elapsed = (uint32_t)(now - timing.render_started);
        timing.render_us += elapsed;
        if (elapsed > timing.render_max_us) timing.render_max_us = elapsed;
        ++timing.frames;
        break;
    }
    case LV_EVENT_FLUSH_WAIT_START:
        timing.wait_started = now;
        break;
    case LV_EVENT_FLUSH_WAIT_FINISH:
        timing.wait_us += (uint64_t)(now - timing.wait_started);
        break;
    case LV_EVENT_FLUSH_START: {
        const lv_area_t *area = lv_event_get_param(event);
        timing.bytes += (uint64_t)lv_area_get_size(area) * 2;
        ++timing.flushes;
        break;
    }
    default:
        break;
    }
}

static void report_timing(lv_timer_t *timer)
{
    (void)timer;
    if (!timing.frames) return;
    // Render phase includes flush waits, and may finish before the last DMA.
    // These are UI-task timings/submitted bytes, not panel FPS or DMA duration.
    ESP_LOGI("OuseDisplay", "5s: renders=%" PRIu32 " phase_avg_us=%" PRIu64
             " phase_max_us=%" PRIu32 " flush_wait_us=%" PRIu64
             " flushes=%" PRIu32 " submitted_bytes=%" PRIu64,
             timing.frames, timing.render_us / timing.frames, timing.render_max_us,
             timing.wait_us, timing.flushes, timing.bytes);
    timing.render_us = timing.wait_us = timing.bytes = 0;
    timing.frames = timing.flushes = timing.render_max_us = 0;
}

void ouse_board_display_timing_init(lv_display_t *display)
{
    static const lv_event_code_t events[] = {
        LV_EVENT_RENDER_START, LV_EVENT_RENDER_READY, LV_EVENT_FLUSH_WAIT_START,
        LV_EVENT_FLUSH_WAIT_FINISH, LV_EVENT_FLUSH_START,
    };
    for (unsigned i = 0; i < sizeof(events) / sizeof(events[0]); ++i)
        lv_display_add_event_cb(display, display_timing, events[i], NULL);
    lv_timer_create(report_timing, 5000, NULL);
}
#endif

// FT6336 supports two tracked contacts. Give each contact its own LVGL indev.
lv_indev_t *__real_esp_lv_adapter_register_touch(const esp_lv_adapter_touch_config_t *config);
lv_indev_t *__wrap_esp_lv_adapter_register_touch(const esp_lv_adapter_touch_config_t *config)
{
    if(!config)return __real_esp_lv_adapter_register_touch(config);
    esp_lv_adapter_touch_config_t tuned=*config;
    tuned.multi_touch.mode=ESP_LV_ADAPTER_TOUCH_MODE_MULTI_CONTROL;
    tuned.multi_touch.pointers=2;
    return __real_esp_lv_adapter_register_touch(&tuned);
}
