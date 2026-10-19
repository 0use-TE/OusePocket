// SPDX-License-Identifier: MIT
#include "bsp/display.h"
#include "esp_lv_adapter.h"
#include "esp_log.h"
#include "esp_timer.h"
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
    // Two 25,600-byte internal DMA buffers: +19,200 bytes over the BSP's
    // single 50-line buffer. LVGL renders one while SPI sends the other.
    tuned.profile.buffer_height = 40;
    tuned.profile.use_psram = false;
    tuned.profile.require_double_buffer = true;
    // The board's LCD connector has no TE signal. Double drawing buffers
    // overlap CPU/DMA work; they do not synchronize the panel's scanout.
    lv_display_t *display = __real_esp_lv_adapter_register_display(&tuned);
    if (display) ESP_LOGI("OuseDisplay", "SPI partial rendering: 2 x 40 lines, internal DMA buffers");
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
