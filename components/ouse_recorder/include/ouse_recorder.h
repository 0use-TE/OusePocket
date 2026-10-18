#pragma once
#include "ouse_tasks.h"

#define OUSE_RECORDER_PAGE 12
typedef struct {char path[128];uint32_t duration_ms;int64_t ended;size_t bytes;} ouse_recording_entry_t;
typedef struct {
    bool active,stopping,busy,more;
    uint32_t task_id,revision,duration_ms,offset;
    int64_t ended;esp_err_t error;
    char path[128];unsigned count;
    ouse_recording_entry_t entries[OUSE_RECORDER_PAGE];
} ouse_recorder_state_t;
esp_err_t ouse_recorder_init(void);
void ouse_recorder_get(ouse_recorder_state_t *out);
bool ouse_recorder_idle(void);
esp_err_t ouse_recorder_start(const ouse_task_owner_t *owner,uint32_t *id);
bool ouse_recorder_stop(const ouse_task_owner_t *owner);
esp_err_t ouse_recorder_request(const ouse_task_owner_t *owner,unsigned operation,const char *path,const char *name,unsigned offset,uint32_t *id);
bool ouse_recorder_current(uint32_t id);
bool ouse_recorder_write(uint32_t id,const int16_t *samples,unsigned count);
void ouse_recorder_producer_done(uint32_t id,esp_err_t error);
