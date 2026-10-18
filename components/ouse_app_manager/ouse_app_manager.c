#include "ouse_app_manager.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
static SemaphoreHandle_t mutex;
static QueueHandle_t requests;
static ouse_manager_state_t *state;
static bool pending,provider_busy;
esp_err_t ouse_manager_init(void)
{
    if(state)return ESP_OK;
    state=heap_caps_calloc(1,sizeof(*state),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    mutex=xSemaphoreCreateMutex();requests=xQueueCreate(1,sizeof(ouse_manager_request_t));
    if(state && mutex && requests)return ESP_OK;
    free(state);state=NULL;
    if(mutex)vSemaphoreDelete(mutex);
    mutex=NULL;
    if(requests)vQueueDelete(requests);
    requests=NULL;return ESP_ERR_NO_MEM;
}
void ouse_manager_get(ouse_manager_state_t *out)
{
    if(!out)return;
    if(!state){memset(out,0,sizeof(*out));out->error=ESP_ERR_INVALID_STATE;return;}
    xSemaphoreTake(mutex,portMAX_DELAY);
    ouse_managed_app_t *entries=out->apps;unsigned capacity=out->capacity;
    if(capacity<state->count){
        entries=heap_caps_calloc(state->count,sizeof(*entries),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!entries){out->error=ESP_ERR_NO_MEM;xSemaphoreGive(mutex);return;}
        free(out->apps);capacity=state->count;
    }
    *out=*state;out->apps=entries;out->capacity=capacity;
    if(state->count)memcpy(entries,state->apps,state->count*sizeof(*entries));
    out->busy=pending||provider_busy;xSemaphoreGive(mutex);
}
void ouse_manager_release(ouse_manager_state_t *snapshot)
{if(snapshot){free(snapshot->apps);memset(snapshot,0,sizeof(*snapshot));}}
esp_err_t ouse_manager_request(ouse_manager_action_t action,const char *id,uint32_t *request_id)
{
    if(!state || !request_id || action<OUSE_MANAGER_REFRESH || action>OUSE_MANAGER_RETRY)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex,portMAX_DELAY);esp_err_t error=ESP_OK;
    if(pending || provider_busy || !state->ready || (state->recovery_pending && action!=OUSE_MANAGER_RETRY))error=ESP_ERR_INVALID_STATE;
    if(error==ESP_OK && (action==OUSE_MANAGER_CLEAR || action==OUSE_MANAGER_UNINSTALL)){
        bool found=false;
        for(unsigned index=0;index<state->count;++index)if(id && !state->apps[index].system && !strcmp(id,state->apps[index].id))found=true;
        if(!found)error=ESP_ERR_INVALID_ARG;
    }
    if(error==ESP_OK){
        ouse_manager_request_t request={.action=action,.id=state->request_id+1};
        if(id && strlen(id)>=sizeof(request.target))error=ESP_ERR_INVALID_ARG;
        else {
            if(id)strcpy(request.target,id);
            if(xQueueSend(requests,&request,0)!=pdTRUE)error=ESP_ERR_NO_MEM;
            else {pending=true;state->request_id=request.id;state->action=action;strcpy(state->target,request.target);state->error=ESP_OK;++state->revision;*request_id=request.id;}
        }
    }
    xSemaphoreGive(mutex);return error;
}
bool ouse_manager_pending(void)
{
    if(!state)return false;
    xSemaphoreTake(mutex,portMAX_DELAY);bool value=pending;xSemaphoreGive(mutex);return value;
}
bool ouse_manager_take(ouse_manager_request_t *out)
{return requests && out && xQueueReceive(requests,out,0)==pdTRUE;}
void ouse_manager_complete(const ouse_manager_request_t *request,esp_err_t error)
{
    if(!state)return;
    xSemaphoreTake(mutex,portMAX_DELAY);state->completed_id=request->id;state->error=error;pending=false;++state->revision;xSemaphoreGive(mutex);
}
void ouse_manager_provider(bool ready,bool busy,bool recovery)
{
    if(!state)return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(state->ready!=ready || provider_busy!=busy || state->recovery_pending!=recovery)++state->revision;
    state->ready=ready;provider_busy=busy;state->recovery_pending=recovery;xSemaphoreGive(mutex);
}
void ouse_manager_publish(const ouse_manager_state_t *snapshot)
{
    if(!state || !snapshot || snapshot->count>snapshot->capacity || (snapshot->count && !snapshot->apps))return;
    ouse_managed_app_t *entries=NULL;
    if(snapshot->count){
        entries=heap_caps_calloc(snapshot->count,sizeof(*entries),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!entries)return;
        memcpy(entries,snapshot->apps,snapshot->count*sizeof(*entries));
    }
    xSemaphoreTake(mutex,portMAX_DELAY);free(state->apps);state->apps=entries;state->count=state->capacity=snapshot->count;
    state->total_bytes=snapshot->total_bytes;
    state->used_bytes=snapshot->used_bytes;state->free_bytes=snapshot->free_bytes;++state->revision;++state->catalog_revision;xSemaphoreGive(mutex);
}
