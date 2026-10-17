// SPDX-License-Identifier: MIT
#include "ouse_health.h"
#include "freertos/FreeRTOS.h"
#include <string.h>
static portMUX_TYPE health_lock=portMUX_INITIALIZER_UNLOCKED;
static ouse_health_snapshot_t cache;
static ouse_control_t request;
static bool pending,enabled;
void ouse_health_enable(void){portENTER_CRITICAL(&health_lock);enabled=true;portEXIT_CRITICAL(&health_lock);}
void ouse_health_get(ouse_health_snapshot_t *out)
{if(out){portENTER_CRITICAL(&health_lock);*out=cache;portEXIT_CRITICAL(&health_lock);}}
void ouse_health_publish(const ouse_health_snapshot_t *snapshot)
{if(snapshot){portENTER_CRITICAL(&health_lock);cache=*snapshot;portEXIT_CRITICAL(&health_lock);}}
esp_err_t ouse_control_request(ouse_control_action_t action,const char *app_id,uint32_t task_id,uint32_t instance_id)
{
    if((unsigned)action>OUSE_CONTROL_STOP || (app_id && strnlen(app_id,48)>=48) ||
       (action==OUSE_CONTROL_STOP && (!app_id || !*app_id)))return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&health_lock);
    esp_err_t error=(!enabled || pending) ? ESP_ERR_INVALID_STATE : ESP_OK;
    if(error==ESP_OK){request=(ouse_control_t){.action=action,.task_id=task_id,.instance_id=instance_id};
        if(app_id)strcpy(request.app_id,app_id);
        pending=true;}
    portEXIT_CRITICAL(&health_lock);return error;
}
bool ouse_control_take(ouse_control_t *out)
{
    if(!out)return false;
    portENTER_CRITICAL(&health_lock);bool found=pending;
    if(found){*out=request;pending=false;}
    portEXIT_CRITICAL(&health_lock);return found;
}
esp_err_t ouse_activity_stop_owned(const char *app_id,uint32_t task_id)
{
    ouse_task_snapshot_t task;
    if(!app_id || !task_id)return ESP_ERR_INVALID_ARG;
    if(!ouse_task_get(NULL,task_id,&task) || strcmp(task.owner.app_id,app_id))return ESP_ERR_NOT_FOUND;
    return ouse_task_cancel(&task.owner,task_id,"owner_activity_stop");
}
const char *ouse_service_status_name(ouse_service_status_t status)
{static const char *names[]={"starting","ready","degraded","recovering","unavailable"};return (unsigned)status<5 ? names[status] : "unknown";}
