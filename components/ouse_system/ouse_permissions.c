#include "ouse_permissions.h"
#include "nvs.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {char app_id[48];uint8_t values[OUSE_PERMISSION_COUNT];} saved_t;
typedef struct entry {struct entry *next;saved_t saved;} entry_t;
static SemaphoreHandle_t mutex;
static entry_t *entries;
static ouse_permission_prompt_t prompts[8];
static unsigned prompt_count;
static uint32_t revision;
static uint32_t active_prompt;
static bool valid(const char *id,ouse_permission_t permission)
{return id && !strncmp(id,"user.",5) && id[5] && strlen(id)<48 && (unsigned)permission<OUSE_PERMISSION_COUNT;}
static void key_for(const char *id,char key[16])
{
    uint64_t hash=14695981039346656037ULL;
    for(const unsigned char *cursor=(const unsigned char *)id;*cursor;++cursor){hash^=*cursor;hash*=1099511628211ULL;}
    snprintf(key,16,"p%014llx",(unsigned long long)(hash&0x00ffffffffffffffULL));
}
esp_err_t ouse_permissions_init(void)
{if(mutex)return ESP_OK;mutex=xSemaphoreCreateMutex();return mutex ? ESP_OK : ESP_ERR_NO_MEM;}
static entry_t *load(const char *id)
{
    for(entry_t *entry=entries;entry;entry=entry->next)if(!strcmp(entry->saved.app_id,id))return entry;
    entry_t *entry=heap_caps_calloc(1,sizeof(*entry),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!entry)return NULL;
    strcpy(entry->saved.app_id,id);nvs_handle_t handle;esp_err_t error=nvs_open("ouse_perms",NVS_READONLY,&handle);
    if(error==ESP_OK){
        saved_t saved={0};size_t size=sizeof(saved);char key[16];key_for(id,key);
        error=nvs_get_blob(handle,key,&saved,&size);nvs_close(handle);
        if(error==ESP_OK){
            if((size!=sizeof(saved) && size!=51) || memcmp(saved.app_id,entry->saved.app_id,sizeof(saved.app_id))){free(entry);return NULL;}
            for(unsigned index=0;index<OUSE_PERMISSION_COUNT;++index)if(saved.values[index]>OUSE_PERMISSION_DENY){free(entry);return NULL;}
            entry->saved=saved;
        }else if(error!=ESP_ERR_NVS_NOT_FOUND){free(entry);return NULL;}
    }else if(error!=ESP_ERR_NVS_NOT_FOUND){free(entry);return NULL;}
    entry->next=entries;entries=entry;return entry;
}
ouse_permission_state_t ouse_permissions_get(const char *id,ouse_permission_t permission)
{
    if(!mutex || !valid(id,permission))return OUSE_PERMISSION_DENY;
    xSemaphoreTake(mutex,portMAX_DELAY);entry_t *entry=load(id);
    ouse_permission_state_t result=entry ? entry->saved.values[permission] : OUSE_PERMISSION_DENY;
    xSemaphoreGive(mutex);return result;
}
esp_err_t ouse_permissions_set(const char *id,ouse_permission_t permission,ouse_permission_state_t state)
{
    if(!mutex || !valid(id,permission) || (unsigned)state>OUSE_PERMISSION_DENY)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex,portMAX_DELAY);entry_t *entry=load(id);esp_err_t error=ESP_ERR_NO_MEM;
    if(entry){
        saved_t saved=entry->saved;saved.values[permission]=state;nvs_handle_t handle;
        error=nvs_open("ouse_perms",NVS_READWRITE,&handle);
        if(error==ESP_OK){char key[16];key_for(id,key);error=nvs_set_blob(handle,key,&saved,sizeof(saved));if(error==ESP_OK)error=nvs_commit(handle);nvs_close(handle);}
        if(error==ESP_OK){entry->saved=saved;++revision;}
    }
    xSemaphoreGive(mutex);return error;
}
esp_err_t ouse_permissions_request(const char *id,uint32_t instance,ouse_permission_t permission)
{
    if(!mutex || !valid(id,permission) || !instance)return ESP_ERR_INVALID_ARG;
    if(ouse_permissions_get(id,permission)!=OUSE_PERMISSION_ASK)return ESP_OK;
    xSemaphoreTake(mutex,portMAX_DELAY);
    for(unsigned index=0;index<prompt_count;++index)if(prompts[index].instance==instance && prompts[index].permission==permission){xSemaphoreGive(mutex);return ESP_OK;}
    esp_err_t error=prompt_count<8 ? ESP_OK : ESP_ERR_NO_MEM;
    if(error==ESP_OK){ouse_permission_prompt_t *prompt=&prompts[prompt_count++];strcpy(prompt->app_id,id);prompt->instance=instance;prompt->permission=permission;}
    xSemaphoreGive(mutex);return error;
}
bool ouse_permissions_next(ouse_permission_prompt_t *prompt)
{
    if(!mutex || !prompt)return false;
    xSemaphoreTake(mutex,portMAX_DELAY);bool found=prompt_count>0;
    if(found){*prompt=prompts[0];active_prompt=prompt->instance;memmove(prompts,prompts+1,--prompt_count*sizeof(*prompts));}
    xSemaphoreGive(mutex);return found;
}
void ouse_permissions_cancel(uint32_t instance)
{
    if(!mutex)return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(active_prompt==instance)active_prompt=0;
    for(unsigned index=0;index<prompt_count;)if(prompts[index].instance==instance){memmove(prompts+index,prompts+index+1,(--prompt_count-index)*sizeof(*prompts));}else ++index;
    xSemaphoreGive(mutex);
}
uint32_t ouse_permissions_revision(void)
{
    if(!mutex)return 0;
    xSemaphoreTake(mutex,portMAX_DELAY);uint32_t value=revision;xSemaphoreGive(mutex);return value;
}
bool ouse_permissions_prompt_current(uint32_t instance)
{
    if(!mutex)return false;
    xSemaphoreTake(mutex,portMAX_DELAY);bool current=instance && active_prompt==instance;xSemaphoreGive(mutex);return current;
}
esp_err_t ouse_permissions_purge(const char *id)
{
    if(!mutex || !valid(id,OUSE_PERMISSION_MICROPHONE))return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex,portMAX_DELAY);char key[16];key_for(id,key);nvs_handle_t handle;
    esp_err_t error=nvs_open("ouse_perms",NVS_READWRITE,&handle);
    if(error==ESP_OK){error=nvs_erase_key(handle,key);if(error==ESP_ERR_NVS_NOT_FOUND)error=ESP_OK;if(error==ESP_OK)error=nvs_commit(handle);nvs_close(handle);}
    if(error==ESP_OK){
        entry_t **link=&entries;
        while(*link){if(!strcmp((*link)->saved.app_id,id)){entry_t *retired=*link;*link=retired->next;free(retired);break;}link=&(*link)->next;}
        ++revision;
    }
    xSemaphoreGive(mutex);return error;
}
