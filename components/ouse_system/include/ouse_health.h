// SPDX-License-Identifier: MIT
#pragma once
#include "ouse_tasks.h"
#define OUSE_HEALTH_SERVICES 8
#define OUSE_HEALTH_ACTIVITIES 12
typedef enum { OUSE_SERVICE_STARTING,OUSE_SERVICE_READY,OUSE_SERVICE_DEGRADED,
    OUSE_SERVICE_RECOVERING,OUSE_SERVICE_UNAVAILABLE } ouse_service_status_t;
typedef struct {
    char name[16];
    ouse_service_status_t status;
    esp_err_t error;
    unsigned active;
    uint64_t oldest_ms;
} ouse_service_health_t;
typedef struct {
    char app_id[48],reason[64];
    uint32_t task_id,instance_id;
    bool javascript,cancellable,stopping;
} ouse_activity_t;
typedef enum { OUSE_CONTROL_RESTART,OUSE_CONTROL_RESUME,OUSE_CONTROL_STOP } ouse_control_action_t;
typedef struct {ouse_control_action_t action;char app_id[48];uint32_t task_id,instance_id;} ouse_control_t;
typedef struct {
    uint64_t sampled_ms;
    char phase[16],blocker[64];
    bool available,quiescing;
    unsigned service_count,activity_count,activities_total;
    ouse_service_health_t services[OUSE_HEALTH_SERVICES];
    ouse_activity_t activities[OUSE_HEALTH_ACTIVITIES];
} ouse_health_snapshot_t;
// A bounded cache. Native coordinator publishes at 2 Hz; readers never do I/O.
void ouse_health_get(ouse_health_snapshot_t *out);
void ouse_health_publish(const ouse_health_snapshot_t *snapshot);
void ouse_health_enable(void);
// Native system UI only. JS may cancel only a matching owner app's task below.
esp_err_t ouse_control_request(ouse_control_action_t action,const char *app_id,uint32_t task_id,uint32_t instance_id);
bool ouse_control_take(ouse_control_t *out);
esp_err_t ouse_activity_stop_owned(const char *app_id,uint32_t task_id);
const char *ouse_service_status_name(ouse_service_status_t status);
