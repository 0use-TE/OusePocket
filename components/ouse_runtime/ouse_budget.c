// SPDX-License-Identifier: MIT
#include "ouse_budget.h"
#include "esp_timer.h"
#if defined(ESP_PLATFORM) || defined(OUSE_BUDGET_TEST_RTOS)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if !configGENERATE_RUN_TIME_STATS
#error Ouse execution protection requires FreeRTOS runtime statistics
#endif
#if defined(ESP_PLATFORM) && (!CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER || !CONFIG_FREERTOS_RUN_TIME_COUNTER_TYPE_U64)
#error Ouse scheduled microsecond budgets require the fixed ESP timer / U64 runtime statistics configuration
#endif
static uint64_t counter(void)
{
    TaskStatus_t state;
    // The running segment is not included by vTaskGetInfo in IDF 6.1's kernel.
    // An actual blocked tick guarantees a switch-out even without ready peers.
    vTaskDelay(1);
    vTaskGetInfo(NULL,&state,pdFALSE,eInvalid);
    return state.ulRunTimeCounter;
}
const char *ouse_budget_clock_name(void) {return "freertos-task-scheduled-us";}
#else
// Host portability only. Never present this fallback as CPU/scheduled time.
static uint64_t counter(void) {return (uint64_t)esp_timer_get_time();}
const char *ouse_budget_clock_name(void) {return "host-elapsed-us";}
#endif
void ouse_budget_begin(ouse_budget_t *budget,int64_t limit_us,bool startup)
{
    *budget=(ouse_budget_t){.baseline=counter(),.limit_us=limit_us,.wall_limit_us=startup ? 2000000 : 1000000};
    budget->observed_us=budget->started_us=budget->last_sample_us=esp_timer_get_time();
}
bool ouse_budget_exceeded(ouse_budget_t *budget)
{
    int64_t now=esp_timer_get_time();budget->observed_us=now;
    if(now-budget->started_us>=budget->wall_limit_us){budget->wall_exceeded=true;return true;}
    if(now-budget->last_sample_us>=1000){
        uint64_t current=counter();
        budget->scheduled_us=(int64_t)(current-budget->baseline);
        budget->observed_us=budget->last_sample_us=esp_timer_get_time();++budget->samples;
        if(budget->last_sample_us-budget->started_us>=budget->wall_limit_us){budget->wall_exceeded=true;return true;}
    }
    return budget->scheduled_us>=budget->limit_us;
}
bool ouse_budget_slice_done(ouse_budget_t *budget)
{
    // Once a callback returns, defer additional callbacks after the ordinary
    // elapsed slice as well. Preemption is not itself a script exception.
    return ouse_budget_exceeded(budget) || esp_timer_get_time()-budget->started_us>=budget->limit_us;
}
