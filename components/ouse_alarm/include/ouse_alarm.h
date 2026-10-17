// SPDX-License-Identifier: MIT
#pragma once
#include "ouse_tasks.h"
#ifdef __cplusplus
extern "C" {
#endif
#define OUSE_ALARM_CAPACITY 24
#define OUSE_ALARM_APP_LIMIT 4
#define OUSE_ALARM_QUEUE_MAX 8
#define OUSE_CLOCK_MIN_UTC 1704067200LL
#define OUSE_CLOCK_MAX_UTC 4102444800LL
typedef struct {
    char id[32], title[96], payload[128];
    int64_t at_utc; // seconds, one-shot when weekdays=0
    uint16_t minute_of_day; // 0..1439 for weekly recurrence
    int16_t utc_offset_minutes; // -720..840, fixed offset, no automatic DST
    uint8_t weekdays; // bit0 Sunday .. bit6 Saturday, 0 = one shot
} ouse_alarm_spec_t;
typedef struct {
    char app_id[48];
    ouse_alarm_spec_t spec;
    int64_t next_utc,last_utc;
    bool enabled,missed,ringing;
} ouse_alarm_snapshot_t;
typedef struct {
    bool ready,clock_valid;
    unsigned count,ringing;
    uint32_t revision;
    esp_err_t error;
    int64_t utc_seconds;
} ouse_alarm_stats_t;
typedef enum { OUSE_ALARM_SET,OUSE_ALARM_REMOVE,OUSE_ALARM_DISMISS,OUSE_ALARM_SNOOZE,OUSE_ALARM_ENABLE,OUSE_ALARM_DISABLE } ouse_alarm_action_t;
// Pure schedule helpers, UTC seconds. Strictly later than after_utc; weekly
// recurrence uses a per-alarm fixed offset without changing process TZ.
bool ouse_alarm_spec_valid(const ouse_alarm_spec_t *spec);
int64_t ouse_alarm_next(const ouse_alarm_spec_t *spec,int64_t after_utc);
// Installer worker only, after LittleFS mount, before restoring packages.
// Load/validate preserved system file; corruption disables service, no erase.
esp_err_t ouse_alarm_start(const char *const *installed_ids,unsigned installed_count);
// Installer updates authorization on apply/update; does not perform I/O.
// Non-restored packages retain records but cannot trigger or submit alarms.
void ouse_alarm_register_app(const char *app_id,bool allowed);
// Short nonblocking native request, completes through common task events.
// Creation/update persists before success. Alarm belongs to app_id, not the
// submitting instance; closing/updating BPK does not remove accepted alarms.
esp_err_t ouse_alarm_submit(const ouse_task_owner_t *owner,ouse_alarm_action_t action,
    const ouse_alarm_spec_t *spec,unsigned snooze_minutes,uint32_t *id);
unsigned ouse_alarm_list(const char *app_id,ouse_alarm_snapshot_t *out,unsigned capacity);
void ouse_alarm_get_stats(ouse_alarm_stats_t *out);
// Installer-only FIFO barrier after closing/removing app. No UI/JS waits.
esp_err_t ouse_alarm_purge(const char *app_id);
bool ouse_alarm_quiesced(void);
esp_err_t ouse_alarm_dismiss_all(void);
#ifdef __cplusplus
}
#endif
