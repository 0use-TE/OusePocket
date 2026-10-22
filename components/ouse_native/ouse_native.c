// SPDX-License-Identifier: MIT
#include "ouse_native.h"
#include "ouse_native_api.h"
#include "ouse_native_game.h"
#include "ouse_native_validate.h"
#include "ouse_files.h"
#include "ouse_storage.h"
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
#include <sys/stat.h>
#include <unistd.h>

#define NATIVE_DIRECTORY OUSE_STORAGE_BASE "/ouse-native"
#define NATIVE_PROGRAM NATIVE_DIRECTORY "/desktop.elf"
#define NATIVE_TEMPORARY NATIVE_PROGRAM ".tmp"
#define QUAKE_PROGRAM NATIVE_DIRECTORY "/quake2.elf"
#define DOOM_PROGRAM NATIVE_DIRECTORY "/doom.elf"
#define NES_PROGRAM NATIVE_DIRECTORY "/nes.elf"
static char nes_rom[180]="/sdcard/Ouse/Downloads/2048.nes";
const char *ouse_game_nes_rom(void){return nes_rom;}
extern const uint8_t demo_start[] asm("_binary_ouse_native_demo_start");
extern const uint8_t demo_end[] asm("_binary_ouse_native_demo_end");

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
    if(ouse_game_register_symbols()!=ESP_OK){mutex=NULL;return ESP_FAIL;}
    strlcpy(state.phase,"idle",sizeof(state.phase));
    return ESP_OK;
}

static void run(void *unused)
{
    (void)unused;
    lock();uint32_t id=state.task_id;uint8_t *data=queued_image;queued_image=NULL;
    long length=(long)state.file_bytes;bool installing=state.installing;unlock();
    bool doom=!strcmp(state.path,"doom.elf"),nes=!strcmp(state.path,"nes.elf");
    bool game=nes || doom || !strcmp(state.path,"quake2.elf"),game_started=false;
    const char *program=nes?NES_PROGRAM:doom?DOOM_PROGRAM:game?QUAKE_PROGRAM:NATIVE_PROGRAM;
    const char *temporary=nes?NES_PROGRAM ".tmp":doom?DOOM_PROGRAM ".tmp":game?QUAKE_PROGRAM ".tmp":NATIVE_TEMPORARY;
    esp_err_t error=ESP_OK;FILE *file=NULL;
    bool sd_file=false;
    esp_elf_t elf={0};bool initialized=false;
    if(!ouse_task_start(id,!installing))goto done;
    if(!data){
        if(!strcmp(state.path,"desktop.elf") || game){
            ouse_storage_stats_t storage;ouse_storage_get_stats(&storage);
            if(!storage.ready){error=ESP_ERR_INVALID_STATE;goto done;}
            file=fopen(program,"rb");
            if(!file && errno==ENOENT && !game){
                length=(long)(demo_end-demo_start);
                data=heap_caps_malloc((size_t)length,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
                if(!data){error=ESP_ERR_NO_MEM;goto done;}
                memcpy(data,demo_start,(size_t)length);
                lock();state.file_bytes=(size_t)length;unlock();
            }
        }else{file=ouse_files_open(state.path);sd_file=file!=NULL;}
        if(!data){
            if(!file){error=ESP_ERR_NOT_FOUND;goto done;}
            if(fseek(file,0,SEEK_END)){error=ESP_FAIL;goto done;}
            length=ftell(file);
            if(length<52 || length>OUSE_NATIVE_FILE_LIMIT){error=ESP_ERR_INVALID_SIZE;goto done;}
            lock();state.file_bytes=(size_t)length;unlock();
            if(fseek(file,0,SEEK_SET)){error=ESP_FAIL;goto done;}
            data=heap_caps_malloc((size_t)length,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
            if(!data){error=ESP_ERR_NO_MEM;goto done;}
            if(fread(data,1,(size_t)length,file)!=(size_t)length){error=ESP_FAIL;goto done;}
            if(sd_file)ouse_files_close(file);else fclose(file);file=NULL;
        }
    }
    if(stopped())goto done;
    phase("validating");
    if(ouse_native_validate(data,(size_t)length)!=(game?2:1)){error=ESP_ERR_INVALID_ARG;goto done;}
    if(installing){
        phase("saving");
        if(mkdir(NATIVE_DIRECTORY,0700) && errno!=EEXIST){error=ESP_FAIL;goto done;}
        file=fopen(temporary,"wb");
        bool ok=file!=NULL;
        if(file){
            if(fwrite(data,1,(size_t)length,file)!=(size_t)length)ok=false;
            if(fflush(file))ok=false;
            if(fsync(fileno(file)))ok=false;
            if(fclose(file))ok=false;
            file=NULL;
        }
        if(!ok || rename(temporary,program)){
            unlink(temporary);error=ESP_FAIL;
        }else (void)ouse_storage_refresh();
        goto done;
    }
    phase("loading");
    int rc=esp_elf_init(&elf);initialized=rc==0;
    if(!rc)rc=esp_elf_relocate(&elf,data);
    if(rc){error=rc==-ENOMEM?ESP_ERR_NO_MEM:ESP_ERR_NOT_SUPPORTED;goto done;}
    free(data);data=NULL;
    if(stopped())goto done;
    if(game){error=ouse_game_begin(nes?"nes":doom?"doom":"quake2");if(error!=ESP_OK)goto done;game_started=true;}
    phase("running");
    char *argv[]={state.path,NULL};
    // esp_elf_request discards the application's return value. Invoke the same
    // relocated entry directly so nonzero exits remain visible to callers.
    int exit_code=0;
    if(game)error=ouse_game_execute(elf.entry,1,argv,&exit_code);
    else exit_code=elf.entry(1,argv);
    lock();state.exit_code=exit_code;unlock();
    if(exit_code)error=ESP_FAIL;
done:
    if(game_started)ouse_game_end();
    if(file){if(sd_file)ouse_files_close(file);else fclose(file);}
    free(data);
    if(initialized)esp_elf_deinit(&elf);
    if(ouse_native_memory_retained())error=ESP_FAIL;
    bool cancelled=!installing && stopped();
    ouse_task_finish(id,error,cancelled);
    lock();state.error=error;state.finished_us=esp_timer_get_time();
    state.stop_requested=cancelled;state.active=false;
    strlcpy(state.phase,error!=ESP_OK?"failed":cancelled?"cancelled":installing?"installed":"finished",sizeof(state.phase));
    unlock();
    ESP_LOGI("OuseNative","task=%lu finished: %s cancelled=%d",(unsigned long)id,esp_err_to_name(error),cancelled);
    vTaskDelete(NULL);
}

// Called with mutex held. On success the worker owns image; on failure the
// caller retains it. No queued job can outlive its private state and buffer.
static esp_err_t launch(const char *path,uint8_t *image,size_t bytes,bool installing,uint32_t *task_id)
{
    uint32_t id;esp_err_t error=ouse_task_create(&owner,installing?OUSE_TASK_PACKAGE:OUSE_TASK_NATIVE,
        installing?"native_install":"execute",installing?OUSE_TASK_FINISH_ON_CLOSE:OUSE_TASK_STOP_ON_CLOSE,!installing,&id);
    if(error!=ESP_OK)return error;
    memset(&state,0,sizeof(state));state.active=true;state.task_id=id;state.started_us=esp_timer_get_time();
    state.file_bytes=bytes;state.installing=installing;queued_image=image;
    strlcpy(state.path,path,sizeof(state.path));strlcpy(state.phase,"queued",sizeof(state.phase));
    // Mapping/unmapping briefly disables the caches: its owning loader needs
    // an internal stack. The engine runs in a separate 256 KiB PSRAM task.
    size_t stack=!installing && (!strcmp(path,"quake2.elf") || !strcmp(path,"doom.elf") || !strcmp(path,"nes.elf"))?16384:12288;
    BaseType_t created=xTaskCreate(run,"ouse_native",stack,NULL,2,NULL);
    if(created!=pdPASS){
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
    lock();esp_err_t error=state.active?ESP_ERR_INVALID_STATE:launch(path,NULL,0,false,task_id);
    unlock();return error;
}
static esp_err_t copy_image(const uint8_t *image,size_t bytes,bool installing,const char *game,uint32_t *task_id)
{
    if(!mutex)return ESP_ERR_INVALID_STATE;
    if(!image || !task_id || bytes<52 || bytes>OUSE_NATIVE_FILE_LIMIT)return ESP_ERR_INVALID_ARG;
    lock();
    if(state.active){unlock();return ESP_ERR_INVALID_STATE;}
    uint8_t *copy=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!copy){unlock();return ESP_ERR_NO_MEM;}
    memcpy(copy,image,bytes);
    esp_err_t error=launch(game?game:installing?"desktop.elf":"memory.elf",copy,bytes,installing,task_id);
    if(error!=ESP_OK)free(copy);
    unlock();return error;
}
esp_err_t ouse_native_start_image(const uint8_t *image,size_t bytes,uint32_t *task_id)
{return copy_image(image,bytes,false,NULL,task_id);}
esp_err_t ouse_native_install_image(const uint8_t *image,size_t bytes,uint32_t *task_id)
{
    ouse_storage_stats_t storage;ouse_storage_get_stats(&storage);
    if(!storage.ready)return ESP_ERR_INVALID_STATE;
    return copy_image(image,bytes,true,NULL,task_id);
}
esp_err_t ouse_native_install_quake2(const uint8_t *image,size_t bytes,uint32_t *task_id)
{return copy_image(image,bytes,true,"quake2.elf",task_id);}
esp_err_t ouse_native_install_doom(const uint8_t *image,size_t bytes,uint32_t *task_id)
{return copy_image(image,bytes,true,"doom.elf",task_id);}
esp_err_t ouse_native_install_nes(const uint8_t *image,size_t bytes,uint32_t *task_id)
{return copy_image(image,bytes,true,"nes.elf",task_id);}
esp_err_t ouse_native_select_nes_rom(const char *path)
{
    if(!mutex)return ESP_ERR_INVALID_STATE;
    char absolute[180];size_t n=path?strlen(path):0;
    if(n<15 || strncmp(path,"Downloads/",10) || strcmp(path+n-4,".nes") ||
       !ouse_files_path(path,absolute,sizeof(absolute)))return ESP_ERR_INVALID_ARG;
    lock();esp_err_t error=state.active?ESP_ERR_INVALID_STATE:ESP_OK;
    if(error==ESP_OK)strlcpy(nes_rom,absolute,sizeof(nes_rom));
    unlock();return error;
}
esp_err_t ouse_native_start_nes(uint32_t *task_id)
{
    if(!mutex)return ESP_ERR_INVALID_STATE;
    if(!task_id)return ESP_ERR_INVALID_ARG;
    lock();esp_err_t error=state.active?ESP_ERR_INVALID_STATE:launch("nes.elf",NULL,0,false,task_id);
    unlock();return error;
}
esp_err_t ouse_native_start_doom(uint32_t *task_id)
{
    if(!mutex)return ESP_ERR_INVALID_STATE;
    if(!task_id)return ESP_ERR_INVALID_ARG;
    lock();esp_err_t error=state.active?ESP_ERR_INVALID_STATE:launch("doom.elf",NULL,0,false,task_id);
    unlock();return error;
}
esp_err_t ouse_native_start_quake2(uint32_t *task_id)
{
    if(!mutex)return ESP_ERR_INVALID_STATE;
    if(!task_id)return ESP_ERR_INVALID_ARG;
    lock();esp_err_t error=state.active?ESP_ERR_INVALID_STATE:launch("quake2.elf",NULL,0,false,task_id);
    unlock();return error;
}
esp_err_t ouse_native_start_desktop(uint32_t *task_id)
{
    if(!mutex)return ESP_ERR_INVALID_STATE;
    if(!task_id)return ESP_ERR_INVALID_ARG;
    lock();esp_err_t error=state.active?ESP_ERR_INVALID_STATE:launch("desktop.elf",NULL,0,false,task_id);
    unlock();return error;
}
esp_err_t ouse_native_stop_task(uint32_t id)
{
    if(!mutex)return ESP_ERR_INVALID_STATE;
    lock();bool active=state.active && state.task_id==id && !state.installing;
    if(active)state.stop_requested=true;
    unlock();
    return active?ouse_task_cancel(&owner,id,"native_stop"):ESP_ERR_INVALID_STATE;
}
esp_err_t ouse_native_stop(void)
{
    if(!mutex)return ESP_ERR_INVALID_STATE;
    lock();bool active=state.active && !state.installing;uint32_t id=state.task_id;if(active)state.stop_requested=true;unlock();
    return active?ouse_task_cancel(&owner,id,"native_stop"):ESP_OK;
}
void ouse_native_get(ouse_native_state_t *out)
{
    if(!out)return;
    if(!mutex){memset(out,0,sizeof(*out));out->error=ESP_ERR_INVALID_STATE;return;}
    lock();*out=state;unlock();
}
