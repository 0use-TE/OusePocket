// SPDX-License-Identifier: MIT
#include "ouse_clock.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <sys/time.h>
static portMUX_TYPE clock_lock=portMUX_INITIALIZER_UNLOCKED;
static int64_t last_sync_us;
static bool synchronized;
void ouse_clock_note_sync(void)
{
    portENTER_CRITICAL(&clock_lock);last_sync_us=esp_timer_get_time();synchronized=true;portEXIT_CRITICAL(&clock_lock);
}
void ouse_clock_get(ouse_clock_state_t *out)
{
    if(!out)return;
    struct timeval tv;gettimeofday(&tv,NULL);
    out->utc_ms=(int64_t)tv.tv_sec*1000+tv.tv_usec/1000;
    out->valid=tv.tv_sec>=1704067200LL && tv.tv_sec<4102444800LL;
    portENTER_CRITICAL(&clock_lock);out->last_sync_uptime_ms=last_sync_us/1000;out->synced_this_boot=synchronized;portEXIT_CRITICAL(&clock_lock);
}
