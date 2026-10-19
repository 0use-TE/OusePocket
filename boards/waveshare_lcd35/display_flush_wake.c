// SPDX-License-Identifier: MIT
#include "esp_attr.h"
#include "display_flush_wait.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t flush_done;
static lv_display_t *wake_display;

bool ouse_display_flush_wake_init(lv_display_t *display)
{
    if (flush_done) return wake_display == display;
    flush_done = xSemaphoreCreateBinary();
    if (!flush_done) return false;
    wake_display = display;
    return true;
}

void ouse_display_wait_event(lv_display_t *display)
{
    if (display == wake_display && flush_done) {
        // A stale token is harmless: the caller rechecks flushing. A completion
        // between checking flushing and blocking leaves a token, so cannot be lost.
        xSemaphoreTake(flush_done, portMAX_DELAY);
    } else {
        vTaskDelay(1);
    }
}

void __real_lv_display_flush_ready(lv_display_t *display);
void IRAM_ATTR __wrap_lv_display_flush_ready(lv_display_t *display)
{
    __real_lv_display_flush_ready(display);
    if (display != wake_display || !flush_done) return;
    if (xPortInIsrContext()) {
        BaseType_t woke = pdFALSE;
        xSemaphoreGiveFromISR(flush_done, &woke);
        if (woke) portYIELD_FROM_ISR();
    } else {
        xSemaphoreGive(flush_done);
    }
}

