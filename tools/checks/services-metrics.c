#include "platform.h"
#include "ouse_metrics.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned creations,steps,running_core;
static uint64_t idle_counter;
int xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *task,int core)
{(void)name;(void)task;assert(fn&&stack==2048&&priority==1&&(unsigned)(uintptr_t)arg==(unsigned)core);++creations;return core==0 ? pdPASS : pdFALSE;}
TickType_t xTaskGetTickCount(void){return (TickType_t)(test_clock/1000);}
uint64_t ulTaskGetIdleRunTimeCounterForCore(int core){assert((unsigned)core==running_core);return idle_counter;}
void vTaskDelayUntil(TickType_t *wake,TickType_t ticks)
{assert(ticks==1000);if(steps==2)longjmp(worker_exit,1);*wake+=ticks;test_clock+=1000000;idle_counter+=steps++ ? 500000 : 1000000;}
#include "METRICS_SOURCE"
int main(void)
{
    ouse_cpu_core_t sample;
    assert(ouse_cpu_metrics_window(0,1000000,0,1000000,&sample)&&sample.busy_permille==0);
    assert(ouse_cpu_metrics_window(10,10,0,1000000,&sample)&&sample.busy_permille==1000);
    assert(ouse_cpu_metrics_window(0,8000000,0,10000000,&sample)&&sample.busy_permille==200&&sample.window_us==10000000);
    assert(ouse_cpu_metrics_window(UINT32_MAX-500,UINT32_MAX+(uint64_t)19500,0,40000,&sample)&&sample.busy_permille==500);
    assert(!ouse_cpu_metrics_window(1,0,0,1000000,&sample)&&!sample.valid);
    assert(!ouse_cpu_metrics_window(0,1000001,0,1000000,&sample)&&!sample.valid);
    assert(!ouse_cpu_metrics_window(0,1000,0,1000,&sample)&&!sample.valid);
    assert(ouse_cpu_metrics_start()==ESP_ERR_NO_MEM&&creations==2);
    assert(ouse_cpu_metrics_start()==ESP_ERR_INVALID_STATE&&creations==2);
    ouse_cpu_metrics_t cache;ouse_cpu_metrics_get(&cache);
    assert(cache.core_count==2&&cache.cores[0].available&&!cache.cores[0].valid&&!cache.cores[1].available);
    running_core=0;idle_counter=UINT32_MAX-100;test_clock=100000;
    if(!setjmp(worker_exit))sample_core((void*)0);
    ouse_cpu_metrics_get(&cache);assert(cache.cores[0].valid&&cache.cores[0].busy_permille==500&&cache.cores[0].window_us==1000000);
    assert(cache.cores[0].sampled_us==2100000&&!cache.cores[1].valid);
    puts("PASS CPU diagnostics: settled own-core idle sampling, real windows, idle/busy/delayed/64-bit boundaries, invalid data, partial initialization and no duplicate tasks");
}
