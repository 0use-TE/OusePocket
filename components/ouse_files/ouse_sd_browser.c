#include "ouse_files.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef OUSE_SD_ROOT
#define OUSE_SD_ROOT "/sdcard"
#endif
typedef struct {ouse_task_owner_t owner;ouse_sd_directory_t result;} browse_job_t;
typedef struct {ouse_task_owner_t owner;uint32_t id;ouse_sd_directory_t *result;bool pending;} cache_t;
static cache_t cache[OUSE_TASK_OWNERS_MAX];
static SemaphoreHandle_t guard;
static QueueHandle_t queue;
static bool same_owner(const ouse_task_owner_t *first,const ouse_task_owner_t *second)
{return first->instance==second->instance && !strcmp(first->app_id,second->app_id);}
bool ouse_sd_browser_path(const char *path,char *absolute,size_t capacity)
{
    if(!path || path[0]!='/' || strnlen(path,OUSE_SD_PATH_MAX)>=OUSE_SD_PATH_MAX)return false;
    if(path[1]){
        const char *component=path+1;
        for(const char *cursor=component;;++cursor){
            unsigned char value=(unsigned char)*cursor;
            if(value && (value<32 || value==127 || strchr("\\:<>\"|?*",value)))return false;
            if(!value || value=='/'){
                size_t length=(size_t)(cursor-component);
                if(!length || component[length-1]=='.' || component[length-1]==' ')return false;
                if(!value)break;
                component=cursor+1;
            }
        }
    }
    int length=snprintf(absolute,capacity,OUSE_SD_ROOT "%s",path);
    return length>0 && (size_t)length<capacity;
}
static cache_t *find(const ouse_task_owner_t *owner)
{
    for(unsigned index=0;index<OUSE_TASK_OWNERS_MAX;++index)if(cache[index].id && same_owner(&cache[index].owner,owner))return &cache[index];
    return NULL;
}
static void enumerate(browse_job_t *job)
{
    ouse_sd_directory_t *result=&job->result;
    ouse_files_state_t state;ouse_files_get(&state);
    if(!state.ready){result->error=ESP_ERR_INVALID_STATE;return;}
    char *absolute=heap_caps_calloc(1,OUSE_SD_PATH_MAX+sizeof(OUSE_SD_ROOT),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!absolute){result->error=ESP_ERR_NO_MEM;return;}
    if(!ouse_sd_browser_path(result->path,absolute,OUSE_SD_PATH_MAX+sizeof(OUSE_SD_ROOT))){free(absolute);result->error=ESP_ERR_INVALID_ARG;return;}
    DIR *directory=opendir(absolute);
    if(!directory){result->error=errno==ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;free(absolute);return;}
    uint32_t skipped=0;int64_t deadline=esp_timer_get_time()+5000000;
    for(;;){
        if(ouse_task_cancel_requested(result->id)){result->error=ESP_ERR_INVALID_STATE;break;}
        if(esp_timer_get_time()>deadline){result->error=ESP_ERR_TIMEOUT;break;}
        errno=0;struct dirent *entry=readdir(directory);
        if(!entry){if(errno)result->error=ESP_FAIL;break;}
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,".."))continue;
        if(skipped<result->offset){++skipped;continue;}
        if(result->count==OUSE_SD_PAGE_SIZE){result->more=true;break;}
        ouse_sd_entry_t *item=&result->entries[result->count++];
        snprintf(item->name,sizeof(item->name),"%s",entry->d_name);
        size_t parent_length=strlen(result->path),name_length=strlen(entry->d_name);
        bool separator=strcmp(result->path,"/")!=0;
        if(parent_length+separator+name_length>=sizeof(item->path))continue;
        memmove(item->path,result->path,parent_length);
        if(separator)item->path[parent_length++]='/';
        memcpy(item->path+parent_length,entry->d_name,name_length+1);
        if(!ouse_sd_browser_path(item->path,absolute,OUSE_SD_PATH_MAX+sizeof(OUSE_SD_ROOT))){item->path[0]=0;continue;}
        struct stat info;
        if(stat(absolute,&info)==0){item->directory=S_ISDIR(info.st_mode);item->bytes=info.st_size;item->accessible=true;}
    }
    closedir(directory);free(absolute);
}
static void worker(void *unused)
{
    (void)unused;browse_job_t *job;
    for(;;){
        if(xQueueReceive(queue,&job,portMAX_DELAY)!=pdTRUE)continue;
        bool started=ouse_task_start(job->result.id,true);
        if(started)enumerate(job);
        bool cancelled=ouse_task_cancel_requested(job->result.id);
        xSemaphoreTake(guard,portMAX_DELAY);
        cache_t *slot=find(&job->owner);
        if(slot && slot->id==job->result.id){
            slot->pending=false;
            if(started && !cancelled){
                slot->result=heap_caps_calloc(1,sizeof(*slot->result),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
                if(slot->result)*slot->result=job->result;
                else job->result.error=ESP_ERR_NO_MEM;
            }
        }
        xSemaphoreGive(guard);
        if(started)ouse_task_finish(job->result.id,cancelled ? ESP_OK : job->result.error,cancelled);
        free(job);
    }
}
esp_err_t ouse_sd_browser_init(void)
{
    if(queue)return ESP_OK;
    guard=xSemaphoreCreateMutex();if(!guard)return ESP_ERR_NO_MEM;
    queue=xQueueCreate(4,sizeof(browse_job_t *));
    if(queue && xTaskCreateWithCaps(worker,"ouse_sd_browse",6144,NULL,2,NULL,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS)return ESP_OK;
    if(queue)vQueueDelete(queue);
    queue=NULL;vSemaphoreDelete(guard);guard=NULL;return ESP_ERR_NO_MEM;
}
esp_err_t ouse_sd_browser_request(const ouse_task_owner_t *owner,const char *path,uint32_t offset,uint32_t *id)
{
    if(!owner || !id || !queue || offset>UINT32_MAX-OUSE_SD_PAGE_SIZE)return ESP_ERR_INVALID_ARG;
    browse_job_t *job=heap_caps_calloc(1,sizeof(*job),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!job)return ESP_ERR_NO_MEM;
    char *absolute=heap_caps_calloc(1,OUSE_SD_PATH_MAX+sizeof(OUSE_SD_ROOT),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    bool valid=absolute && ouse_sd_browser_path(path,absolute,OUSE_SD_PATH_MAX+sizeof(OUSE_SD_ROOT));free(absolute);
    if(!valid){free(job);return ESP_ERR_INVALID_ARG;}
    xSemaphoreTake(guard,portMAX_DELAY);cache_t *slot=find(owner);
    if(!slot)for(unsigned index=0;index<OUSE_TASK_OWNERS_MAX;++index)if(!cache[index].id){slot=&cache[index];break;}
    if(!slot || slot->pending){xSemaphoreGive(guard);free(job);return ESP_ERR_INVALID_STATE;}
    esp_err_t error=ouse_task_create(owner,OUSE_TASK_STORAGE,"sd.browse",OUSE_TASK_STOP_ON_CLOSE,true,id);
    if(error==ESP_OK){
        free(slot->result);*slot=(cache_t){.owner=*owner,.id=*id,.pending=true};
        job->owner=*owner;job->result.id=*id;job->result.offset=offset;strcpy(job->result.path,path);
        if(xQueueSend(queue,&job,0)!=pdTRUE){slot->pending=false;error=ESP_ERR_NO_MEM;ouse_task_finish(*id,error,false);}
    }
    xSemaphoreGive(guard);if(error!=ESP_OK)free(job);return error;
}
bool ouse_sd_browser_result(const ouse_task_owner_t *owner,uint32_t id,ouse_sd_directory_t *out)
{
    if(!guard || !owner || !out)return false;
    xSemaphoreTake(guard,portMAX_DELAY);cache_t *slot=find(owner);bool found=slot && slot->id==id;
    if(found){
        if(slot->result)*out=*slot->result;
        else *out=(ouse_sd_directory_t){.id=id,.pending=slot->pending,.error=slot->pending ? ESP_OK : ESP_FAIL};
    }
    xSemaphoreGive(guard);return found;
}
void ouse_sd_browser_forget(const ouse_task_owner_t *owner)
{
    if(!guard || !owner)return;
    xSemaphoreTake(guard,portMAX_DELAY);cache_t *slot=find(owner);
    if(slot){free(slot->result);memset(slot,0,sizeof(*slot));}
    xSemaphoreGive(guard);
}
