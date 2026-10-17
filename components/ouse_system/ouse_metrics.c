// SPDX-License-Identifier: MIT
#include "ouse_metrics.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stddef.h>
#if defined(ESP_PLATFORM) && (CONFIG_FREERTOS_SMP || !CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER || !CONFIG_FREERTOS_RUN_TIME_COUNTER_TYPE_U64)
#error Ouse idle-runtime diagnostics require the pinned non-SMP IDF kernel with ESP timer U64 statistics
#endif
static portMUX_TYPE metrics_lock=portMUX_INITIALIZER_UNLOCKED;
static ouse_cpu_metrics_t metrics;
static bool initialized;
bool ouse_cpu_metrics_window(uint64_t previous_idle,uint64_t current_idle,
    uint64_t previous_time,uint64_t now,ouse_cpu_core_t *out)
{
    if(!out)return false;
    *out=(ouse_cpu_core_t){.available=true,.sampled_us=now};
    if(now<=previous_time || current_idle<previous_idle)return false;
    out->window_us=now-previous_time;out->idle_us=current_idle-previous_idle;
    // Reject inconsistent/too-short data rather than publishing a negative or
    // fabricated clamped load. A delayed sample reports its real longer window.
    if(out->window_us<10000 || out->idle_us>out->window_us)return false;
    out->busy_permille=(unsigned)(1000.0*(double)(out->window_us-out->idle_us)/(double)out->window_us+0.5);
    out->valid=true;return true;
}
static void sample_core(void *argument)
{
    unsigned core=(unsigned)(uintptr_t)argument;
    uint64_t previous_idle=ulTaskGetIdleRunTimeCounterForCore(core),previous_time=esp_timer_get_time();
    TickType_t wake=xTaskGetTickCount();
    for(;;){
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(OUSE_CPU_SAMPLE_PERIOD_MS));
        // This task is pinned to core. Therefore this core's idle task cannot
        // still be running while its accumulated counter is being read.
        uint64_t idle=ulTaskGetIdleRunTimeCounterForCore(core),now=esp_timer_get_time();
        ouse_cpu_core_t value;ouse_cpu_metrics_window(previous_idle,idle,previous_time,now,&value);
        portENTER_CRITICAL(&metrics_lock);metrics.cores[core]=value;portEXIT_CRITICAL(&metrics_lock);
        previous_idle=idle;previous_time=now;
    }
}
esp_err_t ouse_cpu_metrics_start(void)
{
    if(initialized)return ESP_ERR_INVALID_STATE;
    initialized=true;
    unsigned count=configNUMBER_OF_CORES;
    if(!count || count>OUSE_CPU_CORES_MAX)return ESP_ERR_NOT_SUPPORTED;
    portENTER_CRITICAL(&metrics_lock);metrics.core_count=count;portEXIT_CRITICAL(&metrics_lock);
    esp_err_t error=ESP_OK;
    for(unsigned core=0;core<count;++core){
        // Partial startup remains explicit: no unsafe deletion of a task on
        // another core and no hidden retry/leak of additional samplers.
        bool created=xTaskCreatePinnedToCore(sample_core,core ? "ouse_cpu1" : "ouse_cpu0",
            OUSE_CPU_SAMPLER_STACK_BYTES,(void *)(uintptr_t)core,1,NULL,core)==pdPASS;
        if(created){portENTER_CRITICAL(&metrics_lock);metrics.cores[core].available=true;portEXIT_CRITICAL(&metrics_lock);}
        else error=ESP_ERR_NO_MEM;
    }
    return error;
}
void ouse_cpu_metrics_get(ouse_cpu_metrics_t *out)
{
    if(!out)return;
    portENTER_CRITICAL(&metrics_lock);*out=metrics;portEXIT_CRITICAL(&metrics_lock);
}
