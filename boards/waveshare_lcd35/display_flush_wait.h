// SPDX-License-Identifier: MIT
#pragma once

#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// LVGL 9.5.0 exposes no public query for the pending-flush flag. Keep this
// board adapter pinned to its tested layout when changing the LVGL dependency.
#if LVGL_VERSION_MAJOR != 9 || LVGL_VERSION_MINOR != 5 || LVGL_VERSION_PATCH != 0
#error "Ouse display flush wait requires the pinned LVGL 9.5.0 display layout"
#endif
#include "src/display/lv_display_private.h"

static inline void ouse_display_flush_wait(lv_display_t *display)
{
    // The LCD completion ISR owns readiness. LVGL also clears this flag when
    // its wait callback returns, so returning early could overwrite active DMA.
    while (display->flushing) vTaskDelay(1);
}
