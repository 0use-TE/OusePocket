// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define OUSE_CPU_CORES_MAX 2
#define OUSE_CPU_SAMPLE_PERIOD_MS 1000
#define OUSE_CPU_SAMPLER_STACK_BYTES 2048
typedef struct {
    bool available,valid;
    unsigned busy_permille;
    uint64_t idle_us,window_us,sampled_us;
} ouse_cpu_core_t;
typedef struct {
    unsigned core_count;
    ouse_cpu_core_t cores[OUSE_CPU_CORES_MAX];
} ouse_cpu_metrics_t;
// One low-priority pinned sampler per core. Read its OWN core's idle counter
// only after the sampler is scheduled: idle has switched out and is settled.
// Never interpret another core's unflushed current idle segment as busy CPU.
esp_err_t ouse_cpu_metrics_start(void);
void ouse_cpu_metrics_get(ouse_cpu_metrics_t *out);
// Pure counter-window calculation, also used for deterministic verification.
// This measures non-idle scheduled-task share, not JS CPU or ISR utilization.
bool ouse_cpu_metrics_window(uint64_t previous_idle,uint64_t current_idle,
    uint64_t previous_time,uint64_t now,ouse_cpu_core_t *out);
#ifdef __cplusplus
}
#endif
