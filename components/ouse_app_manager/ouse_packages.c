#include "ouse_packages.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
static SemaphoreHandle_t mutex;
static struct {ouse_package_request_t request;ouse_package_state_t state;bool queued;} *packages;
esp_err_t ouse_packages_init(void)
{
    if(packages)return ESP_OK;
    packages=heap_caps_calloc(1,sizeof(*packages),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);mutex=xSemaphoreCreateMutex();
    if(packages&&mutex)return ESP_OK;
    free(packages);packages=NULL;if(mutex)vSemaphoreDelete(mutex);mutex=NULL;return ESP_ERR_NO_MEM;
}
esp_err_t ouse_packages_submit(const ouse_package_request_t *request,uint32_t *id)
{
    if(!packages||!request||!id||!request->owner.instance||!request->location[0]||strnlen(request->location,sizeof(request->location))>=sizeof(request->location))return ESP_ERR_INVALID_ARG;
    if(!request->local){
        if(request->size<1||request->size>OUSE_PACKAGE_LIMIT||strnlen(request->sha256,sizeof(request->sha256))!=64||strnlen(request->app_id,sizeof(request->app_id))>=sizeof(request->app_id)||strncmp(request->app_id,"user.",5)||!request->app_id[5]||request->version_code<1||request->version_code>2147483647u||(strncmp(request->location,"http://",7)&&strncmp(request->location,"https://",8)))return ESP_ERR_INVALID_ARG;
        for(const char *letter=request->app_id;*letter;++letter)if(!((*letter>='a'&&*letter<='z')||(*letter>='0'&&*letter<='9')||*letter=='.'||*letter=='_'||*letter=='-'))return ESP_ERR_INVALID_ARG;
        for(unsigned index=0;index<64;++index)if(!((request->sha256[index]>='0'&&request->sha256[index]<='9')||(request->sha256[index]>='a'&&request->sha256[index]<='f')))return ESP_ERR_INVALID_ARG;
    }
    if(!ouse_tasks_submission_begin())return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(mutex,portMAX_DELAY);esp_err_t error=packages->state.busy?ESP_ERR_INVALID_STATE:ESP_OK;
    if(error==ESP_OK)error=ouse_task_create(&request->owner,OUSE_TASK_PACKAGE,"install",OUSE_TASK_STOP_ON_CLOSE,true,id);
    if(error==ESP_OK){packages->request=*request;packages->request.task=*id;packages->state=(ouse_package_state_t){.busy=true,.task=*id,.total=request->size};strcpy(packages->state.phase,"queued");packages->queued=true;}
    xSemaphoreGive(mutex);ouse_tasks_submission_end();return error;
}
bool ouse_packages_take(ouse_package_request_t *out)
{
    if(!packages||!out)return false;
    xSemaphoreTake(mutex,portMAX_DELAY);bool queued=packages->queued;if(queued){*out=packages->request;packages->queued=false;}xSemaphoreGive(mutex);return queued;
}
void ouse_packages_get(ouse_package_state_t *out)
{
    if(!out)return;
    if(!packages){memset(out,0,sizeof(*out));return;}
    xSemaphoreTake(mutex,portMAX_DELAY);*out=packages->state;xSemaphoreGive(mutex);
}
void ouse_packages_progress(const char *phase,size_t received,size_t total,const char *id)
{
    xSemaphoreTake(mutex,portMAX_DELAY);snprintf(packages->state.phase,sizeof(packages->state.phase),"%s",phase);packages->state.received=received;packages->state.total=total;
    if(id)snprintf(packages->state.app_id,sizeof(packages->state.app_id),"%s",id);
    xSemaphoreGive(mutex);
}
void ouse_packages_finish(esp_err_t error,bool cancelled)
{
    xSemaphoreTake(mutex,portMAX_DELAY);uint32_t task=packages->state.task;packages->state.busy=false;packages->state.error=error;strcpy(packages->state.phase,cancelled?"cancelled":error==ESP_OK?"succeeded":"failed");xSemaphoreGive(mutex);
    ouse_task_finish(task,error,cancelled);
}
