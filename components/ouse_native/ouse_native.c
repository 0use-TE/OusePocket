// SPDX-License-Identifier: MIT
#include "ouse_native.h"
#include "ouse_native_api.h"
#include "ouse_native_validate.h"
#include "ouse_files.h"
#include "esp_elf.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#if !CONFIG_IDF_TARGET_ESP32P4 || !CONFIG_ELF_LOADER_LOAD_PSRAM
#error "The experimental native runtime requires ESP32-P4 executable PSRAM"
#endif
#if CONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT
#error "ABI v1 preflight requires the ELF app loader, without shared object mode"
#endif

static StaticSemaphore_t mutex_storage;
static SemaphoreHandle_t mutex;
static ouse_native_state_t state;
static uint8_t *queued_image;
extern bool ouse_native_memory_retained(void);
static const ouse_task_owner_t owner={.app_id="system.native",.instance=0};
static void lock(void){xSemaphoreTake(mutex,portMAX_DELAY);}
static void unlock(void){xSemaphoreGive(mutex);}
static void phase(const char *text){lock();strlcpy(state.phase,text,sizeof(state.phase));unlock();}
static int stopped(void)
{
    lock();bool stop=state.stop_requested;uint32_t id=state.task_id;unlock();
    return stop || ouse_task_cancel_requested(id);
}
static void module_log(const char *text)
{if(text)ESP_LOGI("OuseNative","%.*s",192,text);}
static uint32_t milliseconds(void){return (uint32_t)(esp_timer_get_time()/1000);}
static void sleep_ms(uint32_t ms)
{
    // Bounded slices keep cooperative cancellation responsive, even for a
    // long sleep. Always yield at least one tick, including sleep_ms(0).
    if(ms>60000)ms=60000;
    do{uint32_t slice=ms>20?20:ms;vTaskDelay(pdMS_TO_TICKS(slice)+1);ms-=slice;}while(ms && !stopped());
}
static void report(int32_t value){lock();state.result=value;state.reported=true;unlock();}
static const ouse_native_api_t api={OUSE_NATIVE_ABI,sizeof(api),module_log,milliseconds,stopped,sleep_ms,report};
const ouse_native_api_t *ouse_native_get_api(uint32_t version)
{return version==OUSE_NATIVE_ABI?&api:NULL;}
static const struct esp_elfsym symbols[]={
    ESP_ELFSYM_EXPORT(ouse_native_get_api),ESP_ELFSYM_END
};

esp_err_t ouse_native_init(void)
{
    if(mutex)return ESP_ERR_INVALID_STATE;
    mutex=xSemaphoreCreateMutexStatic(&mutex_storage);
    int rc=esp_elf_register_symbol(symbols);
    if(rc){mutex=NULL;return ESP_FAIL;}
    strlcpy(state.phase,"idle",sizeof(state.phase));
    return ESP_OK;
}

static void run(void *unused)
{
    (void)unused;
    lock();uint32_t id=state.task_id;uint8_t *data=queued_image;queued_image=NULL;
    long length=(long)state.file_bytes;unlock();
    esp_err_t error=ESP_OK;FILE *file=NULL;
    esp_elf_t elf={0};bool initialized=false;
    if(!ouse_task_start(id,true))goto done;
    if(!data){
        file=ouse_files_open(state.path);
        if(!file){error=ESP_ERR_NOT_FOUND;goto done;}
        if(fseek(file,0,SEEK_END)){error=ESP_FAIL;goto done;}
        length=ftell(file);
        if(length<52 || length>OUSE_NATIVE_FILE_LIMIT){error=ESP_ERR_INVALID_SIZE;goto done;}
        lock();state.file_bytes=(size_t)length;unlock();
        if(fseek(file,0,SEEK_SET)){error=ESP_FAIL;goto done;}
        data=heap_caps_malloc((size_t)length,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!data){error=ESP_ERR_NO_MEM;goto done;}
        if(fread(data,1,(size_t)length,file)!=(size_t)length){error=ESP_FAIL;goto done;}
        ouse_files_close(file);file=NULL;
    }
    if(stopped())goto done;
    phase("validating");
    if(!ouse_native_validate(data,(size_t)length)){error=ESP_ERR_INVALID_ARG;goto done;}
    phase("loading");
    int rc=esp_elf_init(&elf);initialized=rc==0;
    if(!rc)rc=esp_elf_relocate(&elf,data);
    if(rc){error=rc==-ENOMEM?ESP_ERR_NO_MEM:ESP_ERR_NOT_SUPPORTED;goto done;}
    free(data);data=NULL;
    if(stopped())goto done;
    phase("running");
    char *argv[]={state.path,NULL};
    // esp_elf_request discards the application's return value. Invoke the same
    // relocated entry directly so nonzero exits remain visible to callers.
    int exit_code=elf.entry(1,argv);
    lock();state.exit_code=exit_code;unlock();
    if(exit_code)error=ESP_FAIL;
done:
    if(file)ouse_files_close(file);
    free(data);
    if(initialized)esp_elf_deinit(&elf);
    if(ouse_native_memory_retained())error=ESP_FAIL;
    bool cancelled=stopped();
    ouse_task_finish(id,error,cancelled);
    lock();state.error=error;state.finished_us=esp_timer_get_time();
    state.stop_requested=cancelled;state.active=false;
    strlcpy(state.phase,error!=ESP_OK?"failed":cancelled?"cancelled":"finished",sizeof(state.phase));
    unlock();
    ESP_LOGI("OuseNative","task=%lu finished: %s cancelled=%d",(unsigned long)id,esp_err_to_name(error),cancelled);
    vTaskDelete(NULL);
}

// Called with mutex held. On success the worker owns image; on failure the
// caller retains it. No queued job can outlive its private state and buffer.
static esp_err_t launch(const char *path,uint8_t *image,size_t bytes,uint32_t *task_id)
{
    uint32_t id;esp_err_t error=ouse_task_create(&owner,OUSE_TASK_NATIVE,"execute",OUSE_TASK_STOP_ON_CLOSE,true,&id);
    if(error!=ESP_OK)return error;
    memset(&state,0,sizeof(state));state.active=true;state.task_id=id;state.started_us=esp_timer_get_time();
    state.file_bytes=bytes;queued_image=image;
    strlcpy(state.path,path,sizeof(state.path));strlcpy(state.phase,"queued",sizeof(state.phase));
    // Internal stack remains valid during flash writes by unrelated services.
    if(xTaskCreate(run,"ouse_native",12288,NULL,2,NULL)!=pdPASS){
        state.active=false;state.error=ESP_ERR_NO_MEM;state.finished_us=esp_timer_get_time();
        strlcpy(state.phase,"failed",sizeof(state.phase));
        queued_image=NULL;ouse_task_finish(id,ESP_ERR_NO_MEM,false);return ESP_ERR_NO_MEM;
    }
    *task_id=id;return ESP_OK;
}
esp_err_t ouse_native_start(const char *path,uint32_t *task_id)
{
    if(!mutex)return ESP_ERR_INVALID_STATE;
    if(!path || !task_id)return ESP_ERR_INVALID_ARG;
    size_t length=strnlen(path,sizeof(state.path));char absolute[180];
    if(length<15 || length>=sizeof(state.path) || strncmp(path,"Downloads/",10) ||
       strcmp(path+length-4,".elf") || !ouse_files_path(path,absolute,sizeof(absolute)))return ESP_ERR_INVALID_ARG;
    lock();esp_err_t error=state.active?ESP_ERR_INVALID_STATE:launch(path,NULL,0,task_id);
    unlock();return error;
}
esp_err_t ouse_native_start_image(const uint8_t *image,size_t bytes,uint32_t *task_id)
{
    if(!mutex)return ESP_ERR_INVALID_STATE;
    if(!image || !task_id || bytes<52 || bytes>OUSE_NATIVE_FILE_LIMIT)return ESP_ERR_INVALID_ARG;
    lock();
    if(state.active){unlock();return ESP_ERR_INVALID_STATE;}
    uint8_t *copy=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!copy){unlock();return ESP_ERR_NO_MEM;}
    memcpy(copy,image,bytes);
    esp_err_t error=launch("memory.elf",copy,bytes,task_id);
    if(error!=ESP_OK)free(copy);
    unlock();return error;
}
esp_err_t ouse_native_stop(void)
{
    if(!mutex)return ESP_ERR_INVALID_STATE;
    lock();bool active=state.active;uint32_t id=state.task_id;if(active)state.stop_requested=true;unlock();
    return active?ouse_task_cancel(&owner,id,"native_stop"):ESP_OK;
}
void ouse_native_get(ouse_native_state_t *out)
{
    if(!out)return;
    if(!mutex){memset(out,0,sizeof(*out));out->error=ESP_ERR_INVALID_STATE;return;}
    lock();*out=state;unlock();
}
