// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    int64_t utc_ms,last_sync_uptime_ms;
    bool valid,synced_this_boot;
} ouse_clock_state_t;
// valid means plausible system wall time (2024..2100), not an accuracy claim.
// synced_this_boot is set only by the board SNTP success callback.
void ouse_clock_get(ouse_clock_state_t *out);
void ouse_clock_note_sync(void);
#ifdef __cplusplus
}
#endif
