// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    int64_t started_us,last_sample_us,observed_us,scheduled_us,limit_us,slice_limit_us,wall_limit_us;
    uint64_t baseline;
    unsigned samples;
    bool wall_exceeded;
} ouse_budget_t;
void ouse_budget_begin(ouse_budget_t *budget,int64_t limit_us,bool startup);
// Checkpoints yield one tick to flush the kernel's switched-out counter.
// Only call on the JS worker, never inside a critical section or from an ISR.
bool ouse_budget_exceeded(ouse_budget_t *budget);
bool ouse_budget_slice_done(ouse_budget_t *budget);
const char *ouse_budget_clock_name(void);
