// SPDX-License-Identifier: MIT
#include "ouse_installer.h"
#include "ouse_bpk.h"
#include "ouse_protocol.h"
#include "ouse_core.h"
#include "ouse_storage.h"
#include "ouse_alarm.h"
#include "ouse_media.h"
#include "ouse_uninstall_journal.h"
#include "ouse_app_manager.h"
#include "ouse_packages.h"
#include "ouse_net.h"
#include "ouse_files.h"
#include "ouse_dialog.h"
#include "ouse_memory_pressure.h"
#include "psa/crypto.h"
#include <stdatomic.h>
#include "ouse_permissions.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#if CONFIG_OUSE_DEVICE_DEBUG
#include "ouse_debug_transport.h"
#endif

#define DIRECTORY OUSE_STORAGE_PACKAGE_DIRECTORY
#define STAGING DIRECTORY "/upload.tmp"
#define TAG "OuseUSB"
static ouse_bpk_t **installed;
static unsigned installed_capacity;
static bool storage_ready,started;
static unsigned char *upload;
static size_t received,expected;
static uint32_t upload_crc,owner;
static int64_t activity;
static SemaphoreHandle_t ui_done;
static portMUX_TYPE status_lock=portMUX_INITIALIZER_UNLOCKED;
static ouse_installer_state_t status;
static char pending_remove[48],pending_reset[48];
static esp_err_t recovery_error;
static atomic_int package_decision;
static EXT_RAM_BSS_ATTR struct {ouse_package_request_t request;unsigned char *bytes;size_t size;ouse_bpk_t *parsed;int64_t since;bool active,waiting;} package_job;
static EXT_RAM_BSS_ATTR char package_prompt[640];
static void package_accept(void){atomic_store(&package_decision,1);}
void ouse_installer_get_state(ouse_installer_state_t *out)
{if(out){portENTER_CRITICAL(&status_lock);*out=status;portEXIT_CRITICAL(&status_lock);}}
static void publish_status(bool busy,esp_err_t error)
{portENTER_CRITICAL(&status_lock);status=(ouse_installer_state_t){.ready=storage_ready,.busy=busy,
    .recovery_pending=pending_remove[0] || pending_reset[0] || recovery_error!=ESP_OK,.error=error};portEXIT_CRITICAL(&status_lock);
    ouse_manager_provider(storage_ready,busy || upload!=NULL,pending_remove[0] || pending_reset[0] || recovery_error!=ESP_OK);}
static struct { int action;ouse_bpk_t *package;const char *id;esp_err_t error;ouse_script_validation_t *validation; } ui;

static void ui_call(void *unused)
{
    (void)unused;
    if(ui.action==0){
        ui.error=ouse_core_user_check(ui.package->id);
        ui.validation=NULL;
        if(ui.error==ESP_OK)ui.error=ouse_script_validate_begin(&ui.package->package,&ui.validation);
    }else if(ui.action==1){ui.error=ouse_core_user_apply(&ui.package->definition);
        if(ui.error==ESP_OK && !strcmp(ui.package->id,pending_reset))ui.error=ouse_core_user_maintenance(pending_reset,true);
    }
    else if(ui.action==2)ui.error=ouse_core_user_remove(ui.id);
    else if(ui.action==7){ui.error=ouse_dialog_active()?ESP_ERR_INVALID_STATE:ESP_OK;if(ui.error==ESP_OK)ouse_dialog_show(package_prompt,"安装",package_accept);}
    else if(ui.action==8){ui.error=ouse_dialog_action_is(package_accept)?ESP_OK:ESP_ERR_INVALID_STATE;}
    else if(ui.action==9){if(ouse_dialog_action_is(package_accept))ouse_dialog_dismiss();ui.error=ESP_OK;}
    else if(ui.action==6){ouse_core_launcher_restored();ui.error=ESP_OK;}
    else if(ui.action==4 || ui.action==5)ui.error=ouse_core_user_maintenance(ui.id,ui.action==4);
    else ui.error=ouse_app_open(ui.id);
    xSemaphoreGive(ui_done);
}
static esp_err_t on_ui(int action,ouse_bpk_t *package,const char *id)
{
    ui.action=action;ui.package=package;ui.id=id;
    esp_err_t error=ouse_core_post_ui(ui_call,NULL);
    if(error!=ESP_OK)return error;
    // Once dispatched, never expire/free the context while LVGL can still use it.
    xSemaphoreTake(ui_done,portMAX_DELAY);
    if(ui.error==ESP_OK && action==0){
        ouse_script_validation_t *validation=ui.validation;ui.validation=NULL;
        return ouse_script_validate_wait(validation);
    }
    return ui.error;
}
static int find_package(const char *id)
{ for(unsigned i=0;i<installed_capacity;++i)if(installed[i] && !strcmp(installed[i]->id,id))return (int)i;return -1; }
static int free_slot(void)
{
    for(unsigned i=0;i<installed_capacity;++i)if(!installed[i])return (int)i;
    unsigned capacity=installed_capacity ? installed_capacity*2 : 8;
    if(capacity<=installed_capacity || capacity>SIZE_MAX/sizeof(*installed))return -1;
    ouse_bpk_t **entries=heap_caps_calloc(capacity,sizeof(*entries),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!entries)return -1;
    if(installed_capacity)memcpy(entries,installed,installed_capacity*sizeof(*entries));
    free(installed);installed=entries;unsigned slot=installed_capacity;installed_capacity=capacity;return (int)slot;
}
static void path_for(const char *id,char path[128]){snprintf(path,128,DIRECTORY "/%s.bpk",id);}
#ifndef OUSE_BUILTIN_STORE_DATA
extern const unsigned char _binary_ouse_builtin_store_start[];
extern const unsigned char _binary_ouse_builtin_store_end[];
#define OUSE_BUILTIN_STORE_DATA _binary_ouse_builtin_store_start
#define OUSE_BUILTIN_STORE_SIZE ((size_t)(_binary_ouse_builtin_store_end-_binary_ouse_builtin_store_start))
#endif
static esp_err_t seed_store(void)
{
    char path[128];path_for("user.store",path);struct stat info;
    if(stat(path,&info)==0 && info.st_size>0 && info.st_size<=OUSE_BPK_MAX){
        unsigned char *data=heap_caps_malloc(info.st_size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!data)return ESP_ERR_NO_MEM;
        FILE *f=fopen(path,"rb");ouse_bpk_t *p=NULL;bool good=f && fread(data,1,info.st_size,f)==(size_t)info.st_size && ouse_bpk_catalog(data,info.st_size,&p)==ESP_OK && !strcmp(p->id,"user.store");
        if(f){fclose(f);}ouse_bpk_free(p);free(data);if(good)return ESP_OK;
    }
    size_t size=OUSE_BUILTIN_STORE_SIZE;FILE *f=fopen(STAGING,"wb");if(!f)return ESP_FAIL;
    bool ok=fwrite(OUSE_BUILTIN_STORE_DATA,1,size,f)==size;if(fflush(f)!=0)ok=false;if(fsync(fileno(f))!=0)ok=false;if(fclose(f)!=0)ok=false;
    if(!ok||rename(STAGING,path)!=0){unlink(STAGING);return ESP_FAIL;}return ESP_OK;
}
static bool valid_id(const char *id)
{
    if(strncmp(id,"user.",5) || !id[5] || strlen(id)>=48)return false;
    for(const char *p=id;*p;++p)if(!((*p>='a' && *p<='z') || (*p>='0' && *p<='9') || *p=='.' || *p=='_' || *p=='-'))return false;
    return true;
}
static esp_err_t load_package(const char *id,uint32_t version,const ouse_script_package_t **out,void **handle)
{
    char path[128];path_for(id,path);struct stat info;
    if(stat(path,&info)!=0 || info.st_size<=0 || info.st_size>OUSE_BPK_MAX)return ESP_ERR_NOT_FOUND;
    unsigned char *bytes=heap_caps_malloc(info.st_size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!bytes)return ESP_ERR_NO_MEM;
    FILE *file=fopen(path,"rb");esp_err_t error=ESP_FAIL;ouse_bpk_t *package=NULL;
    if(file){
        if(fread(bytes,1,info.st_size,file)==(size_t)info.st_size){
            error=ouse_crc32(bytes,info.st_size)==version ? ouse_bpk_parse(bytes,info.st_size,&package) : ESP_ERR_INVALID_CRC;
        }
        fclose(file);
    }
    free(bytes);
    if(error==ESP_OK && strcmp(package->id,id))error=ESP_ERR_INVALID_ARG;
    if(error!=ESP_OK){ouse_bpk_free(package);return error;}
    *handle=package;*out=&package->package;return ESP_OK;
}
static void release_package(void *handle){ouse_bpk_free(handle);}
static esp_err_t activate(ouse_bpk_t *p)
{
    int slot=find_package(p->id);if(slot<0)slot=free_slot();if(slot<0)return ESP_ERR_NO_MEM;
    esp_err_t error=ouse_bpk_make_lazy(p,load_package,release_package);
    if(error==ESP_OK)error=on_ui(1,p,NULL);
    if(error==ESP_OK){ouse_bpk_free(installed[slot]);installed[slot]=p;ouse_alarm_register_app(p->id,p->package.alarms_allowed && strcmp(p->id,pending_reset)!=0);
        if(!p->package.media_allowed)(void)ouse_media_stop(p->id);
    }
    return error;
}
static esp_err_t mount_storage(void)
{
    // Mount and private file operations belong to the storage task. This is
    // called only by the installer task after the desktop has started.
    esp_err_t error=ouse_storage_start();
    storage_ready=error==ESP_OK;return error;
}
static void restore_packages(void)
{
    DIR *dir=opendir(DIRECTORY);if(!dir)return;
    struct dirent *entry;
    while((entry=readdir(dir))!=NULL){
        size_t n=strlen(entry->d_name);if(n<=4 || n>=52 || strcmp(entry->d_name+n-4,".bpk"))continue;
        char id[48];memcpy(id,entry->d_name,n-4);id[n-4]=0;if(!valid_id(id))continue;
        if(!strcmp(id,pending_remove))continue; // durable removal cannot resurrect
        char path[128];path_for(id,path);struct stat info;
        if(stat(path,&info)!=0 || info.st_size<=0 || info.st_size>OUSE_BPK_MAX)continue;
        unsigned char *bytes=heap_caps_malloc((size_t)info.st_size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!bytes)break;
        FILE *file=fopen(path,"rb");ouse_bpk_t *p=NULL;esp_err_t error=ESP_FAIL;
        if(file){
            if(fread(bytes,1,(size_t)info.st_size,file)==(size_t)info.st_size)error=ouse_bpk_catalog(bytes,(size_t)info.st_size,&p);
            fclose(file);
        }
        free(bytes);
        if(error==ESP_OK && strcmp(id,p->id))error=ESP_ERR_INVALID_ARG;
        if(error==ESP_OK)error=activate(p);
        if(error!=ESP_OK){ouse_bpk_free(p);ESP_LOGW(TAG,"Skipped %s: %s (file preserved)",id,esp_err_to_name(error));}
        else ESP_LOGI(TAG,"Restored %s",id);
    }
    closedir(dir);unlink(STAGING);(void)ouse_storage_refresh();
}
static void abort_upload(void)
{ free(upload);upload=NULL;received=expected=0;owner=0; }
static esp_err_t finish_uninstall(const char *id)
{
    int slot=find_package(id);
    esp_err_t error=on_ui(2,NULL,id);
    if(error!=ESP_OK && error!=ESP_ERR_NOT_FOUND)return error;
    ouse_alarm_register_app(id,false);
    char path[128];path_for(id,path);
    if(unlink(path)!=0 && errno!=ENOENT)return ESP_FAIL;
    error=ouse_media_purge(id);
    if(error==ESP_OK)error=ouse_alarm_purge(id);
    if(error==ESP_OK)error=ouse_storage_purge(id);
    if(error==ESP_OK)error=ouse_permissions_purge(id);
    if(error==ESP_OK)error=ouse_uninstall_intent_clear();
    if(error==ESP_OK){
        if(slot>=0){ouse_bpk_free(installed[slot]);installed[slot]=NULL;}
        pending_remove[0]=0;recovery_error=ESP_OK;
    }
    (void)ouse_storage_refresh();return error;
}
static esp_err_t finish_reset(const char *id)
{
    int slot=find_package(id);
    esp_err_t error=on_ui(4,NULL,id);
    if(error!=ESP_OK && error!=ESP_ERR_NOT_FOUND)return error;
    error=ouse_media_purge(id);
    if(error==ESP_OK)error=ouse_alarm_purge(id);
    if(error==ESP_OK)error=ouse_storage_purge(id);
    if(error==ESP_OK)error=ouse_permissions_purge(id);
    if(error==ESP_OK)error=ouse_reset_intent_clear();
    if(error==ESP_OK){
        esp_err_t unlocked=on_ui(5,NULL,id);if(unlocked!=ESP_OK && unlocked!=ESP_ERR_NOT_FOUND)return unlocked;
        if(slot>=0)ouse_alarm_register_app(id,installed[slot]->package.alarms_allowed);
        pending_reset[0]=0;recovery_error=ESP_OK;
    }
    return error;
}
static esp_err_t refresh_manager(void)
{
    ouse_manager_state_t *snapshot=heap_caps_calloc(1,sizeof(*snapshot),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!snapshot)return ESP_ERR_NO_MEM;
    snapshot->capacity=installed_capacity+1;
    snapshot->apps=heap_caps_calloc(snapshot->capacity,sizeof(*snapshot->apps),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!snapshot->apps){free(snapshot);return ESP_ERR_NO_MEM;}
    snapshot->count=1;snapshot->apps[0]=(ouse_managed_app_t){.system=true};
    strcpy(snapshot->apps[0].id,"ouse.settings");strcpy(snapshot->apps[0].name,"Settings");strcpy(snapshot->apps[0].name_zh,"设置");strcpy(snapshot->apps[0].name_en,"Settings");
    esp_err_t error=ouse_storage_refresh();
    for(unsigned index=0;index<installed_capacity;++index)if(installed[index]){
        ouse_bpk_t *package=installed[index];ouse_managed_app_t *app=&snapshot->apps[snapshot->count++];
        app->system=!strcmp(package->id,"user.store");
        app->permissions=(package->package.media_allowed ? 1u : 0u)|(package->package.camera_allowed ? 2u : 0u)|((package->package.files_allowed || package->package.photos_allowed) ? 4u : 0u)|(package->package.bluetooth_allowed ? 8u : 0u)|(package->package.hid_allowed ? 16u : 0u);
        strcpy(app->id,package->id);strcpy(app->name,package->name);strcpy(app->name_zh,package->name_zh);strcpy(app->name_en,package->name_en);
        app->version_code=package->version_code;strcpy(app->version_name,package->version_name);
        strcpy(app->description,package->description);strcpy(app->author,package->author);strcpy(app->released_at,package->released_at);
        char path[128];path_for(app->id,path);struct stat info;
        app->measured=stat(path,&info)==0 && info.st_size>=0;
        if(app->measured)app->package_bytes=info.st_size;
        esp_err_t measured=ouse_storage_measure(app->id,&app->data_bytes);if(measured!=ESP_OK){app->measured=false;error=measured;}
        ouse_alarm_snapshot_t alarms[OUSE_ALARM_APP_LIMIT];app->alarms=ouse_alarm_list(app->id,alarms,OUSE_ALARM_APP_LIMIT);
    }
    ouse_storage_stats_t storage;ouse_storage_get_stats(&storage);
    snapshot->total_bytes=storage.total_bytes;snapshot->used_bytes=storage.used_bytes;snapshot->free_bytes=storage.free_bytes;
    ouse_manager_publish(snapshot);ouse_manager_release(snapshot);free(snapshot);return error;
}
static esp_err_t begin_uninstall(const char *id)
{
    if(!strcmp(id,"user.store"))return ESP_ERR_NOT_SUPPORTED;
    if(!valid_id(id) || (find_package(id)<0 && strcmp(id,pending_remove)))return ESP_ERR_NOT_FOUND;
    esp_err_t error=ouse_uninstall_intent_write(id);
    if(error==ESP_OK){strcpy(pending_remove,id);error=finish_uninstall(id);}
    recovery_error=error;return error;
}
static void process_manager(void)
{
    ouse_manager_request_t request;if(!ouse_manager_take(&request))return;
    publish_status(true,ESP_OK);esp_err_t error=ESP_ERR_INVALID_STATE;
    const ouse_task_owner_t system={.app_id="ouse.manager"};uint32_t task_id=0;
    if(!upload && !ouse_tasks_quiescing())error=ouse_task_create(&system,OUSE_TASK_PACKAGE,"manage",OUSE_TASK_FINISH_ON_CLOSE,false,&task_id);
    if(error==ESP_OK){
        (void)ouse_task_start(task_id,false);
        if(request.action==OUSE_MANAGER_UNINSTALL)error=begin_uninstall(request.target);
        else if(request.action==OUSE_MANAGER_CLEAR){
            if(!valid_id(request.target) || find_package(request.target)<0)error=ESP_ERR_NOT_FOUND;
            else {error=ouse_reset_intent_write(request.target);
                if(error==ESP_OK){strcpy(pending_reset,request.target);error=finish_reset(request.target);}
                recovery_error=error;
            }
        }else if(request.action==OUSE_MANAGER_RETRY){
            char saved[48];error=ouse_uninstall_intent_read(saved);
            if(error==ESP_OK)strcpy(pending_remove,saved);else if(error==ESP_ERR_NOT_FOUND)error=ESP_OK;
            if(error==ESP_OK){esp_err_t reset=ouse_reset_intent_read(saved);if(reset==ESP_OK)strcpy(pending_reset,saved);else if(reset!=ESP_ERR_NOT_FOUND)error=reset;}
            if(error==ESP_OK && pending_remove[0])error=finish_uninstall(pending_remove);
            if(error==ESP_OK && pending_reset[0])error=finish_reset(pending_reset);
            recovery_error=error;
        }else error=ESP_OK;
        esp_err_t measured=refresh_manager();if(error==ESP_OK)error=measured;
        ouse_task_finish(task_id,error,false);
    }
    publish_status(false,error);ouse_manager_complete(&request,error);
}
static esp_err_t commit_package(ouse_bpk_t *p,const unsigned char *bytes,size_t size,char *result,size_t capacity)
{
    snprintf(result,capacity,"storage-commit");char path[128];path_for(p->id,path);
    FILE *file=fopen(STAGING,"wb");bool ok=false;
    if(file){ok=fwrite(bytes,1,size,file)==size;if(fflush(file)!=0)ok=false;if(fsync(fileno(file))!=0)ok=false;if(fclose(file)!=0)ok=false;}
    if(!ok||rename(STAGING,path)!=0){unlink(STAGING);ouse_bpk_free(p);return ESP_FAIL;}
    snprintf(result,capacity,"activation");esp_err_t error=activate(p);(void)ouse_storage_refresh();
    if(error==ESP_OK)snprintf(result,capacity,"%s",p->id);else{ouse_bpk_free(p);snprintf(result,capacity,"committed-reboot-to-restore");}return error;
}
static esp_err_t install_upload(char *result,size_t capacity)
{
    snprintf(result,capacity,"upload-integrity");
    if(!upload || received!=expected)return ESP_ERR_INVALID_ARG;
    uint32_t actual_crc=ouse_crc32(upload,received);
    if(actual_crc!=upload_crc){
        ESP_LOGE(TAG,"Upload CRC mismatch: received=%u expected=%u crc=%08lx expected_crc=%08lx",
            (unsigned)received,(unsigned)expected,(unsigned long)actual_crc,(unsigned long)upload_crc);
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(result,capacity,"archive-manifest");
    ouse_bpk_t *p=NULL;esp_err_t error=ouse_bpk_parse(upload,received,&p);
    if(error!=ESP_OK){ESP_LOGE(TAG,"BPK rejected: archive/manifest (%s)",esp_err_to_name(error));return error;}
    if(!valid_id(p->id)){snprintf(result,capacity,"package-id");ouse_bpk_free(p);return ESP_ERR_INVALID_ARG;}
    snprintf(result,capacity,"schema-syntax-capacity");
    error=on_ui(0,p,NULL); // full schema/syntax/capacity check BEFORE Flash writes
    if(error!=ESP_OK){ESP_LOGE(TAG,"BPK rejected: schema/syntax/capacity (%s)",esp_err_to_name(error));ouse_bpk_free(p);return error;}
    return commit_package(p,upload,received,result,capacity);
}
static bool package_stop(void *unused){(void)unused;return ouse_tasks_quiescing()||ouse_task_cancel_requested(package_job.request.task);}
static esp_err_t package_sink(void *unused,const unsigned char *bytes,size_t size)
{
    (void)unused;if(package_stop(NULL))return ESP_ERR_INVALID_STATE;
    if(size>package_job.request.size-package_job.size)return ESP_ERR_INVALID_SIZE;
    memcpy(package_job.bytes+package_job.size,bytes,size);package_job.size+=size;ouse_packages_progress("downloading",package_job.size,package_job.request.size,NULL);return ESP_OK;
}
static esp_err_t package_read(void)
{
    if(package_job.request.local){
        char *path=heap_caps_malloc(1100,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!path)return ESP_ERR_NO_MEM;
        bool valid=ouse_sd_browser_path(package_job.request.location,path,1100);size_t length=strlen(package_job.request.location);
        if(!valid||length<4||strcasecmp(package_job.request.location+length-4,".bpk")){free(path);return ESP_ERR_INVALID_ARG;}
        if(!ouse_files_lease_begin()){free(path);return ESP_ERR_INVALID_STATE;}
        struct stat info;esp_err_t error=ESP_ERR_NOT_FOUND;
        if(stat(path,&info)==0&&S_ISREG(info.st_mode)&&info.st_size>0&&info.st_size<=OUSE_BPK_MAX){
            package_job.request.size=info.st_size;package_job.bytes=heap_caps_malloc(info.st_size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
            FILE *file=package_job.bytes?fopen(path,"rb"):NULL;error=package_job.bytes?ESP_FAIL:ESP_ERR_NO_MEM;
            if(file){package_job.size=fread(package_job.bytes,1,info.st_size,file);error=package_job.size==(size_t)info.st_size&&fgetc(file)==EOF&&!ferror(file)?ESP_OK:ESP_FAIL;fclose(file);}
        }
        ouse_files_lease_end();free(path);return error;
    }
    package_job.bytes=heap_caps_malloc(package_job.request.size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!package_job.bytes)return ESP_ERR_NO_MEM;
    ouse_http_response_t *response=heap_caps_calloc(1,sizeof(*response),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!response)return ESP_ERR_NO_MEM;
    ouse_http_request_t request={.url=package_job.request.location,.method="GET",.timeout_ms=30000};
    esp_err_t error=ouse_http_transfer(&request,esp_timer_get_time()+60000000,package_job.request.size,response,package_sink,package_stop,NULL);
    if(error==ESP_OK&&(response->status!=200||package_job.size!=package_job.request.size))error=ESP_ERR_INVALID_SIZE;
    free(response);return error;
}
static void package_finish(esp_err_t error,bool cancelled)
{
    if(package_job.waiting)(void)on_ui(9,NULL,NULL);
    ouse_bpk_free(package_job.parsed);free(package_job.bytes);memset(&package_job,0,sizeof(package_job));
    // Readers of a terminal package state must already see the committed
    // version in packages.list(), including its native catalog cache.
    (void)refresh_manager();ouse_packages_finish(error,cancelled);
    publish_status(false,error);
}
static void process_package(void)
{
    if(!package_job.active){
        if(upload||ouse_manager_pending())return;
        if(!ouse_packages_take(&package_job.request))return;
        package_job.active=true;publish_status(true,ESP_OK);
        if(!ouse_task_start(package_job.request.task,true)){package_finish(ESP_OK,true);return;}
        if(pending_remove[0]||pending_reset[0]||recovery_error!=ESP_OK||!storage_ready){package_finish(ESP_ERR_INVALID_STATE,false);return;}
        if(ouse_memory_pressure()!=OUSE_MEMORY_NORMAL){package_finish(ESP_ERR_NO_MEM,false);return;}
        ouse_packages_progress("downloading",0,package_job.request.size,NULL);
        esp_err_t error=package_read();if(error==ESP_OK&&package_stop(NULL))error=ESP_ERR_INVALID_STATE;
        if(error==ESP_OK&&!package_job.request.local){
            unsigned char hash[32];size_t length=0;char hex[65];
            if(psa_crypto_init()!=PSA_SUCCESS||psa_hash_compute(PSA_ALG_SHA_256,package_job.bytes,package_job.size,hash,sizeof(hash),&length)!=PSA_SUCCESS||length!=32)error=ESP_FAIL;
            else{for(unsigned index=0;index<32;++index)snprintf(hex+index*2,3,"%02x",hash[index]);if(strcmp(hex,package_job.request.sha256))error=ESP_ERR_INVALID_CRC;}
        }
        if(error==ESP_OK)error=ouse_bpk_parse(package_job.bytes,package_job.size,&package_job.parsed);
        if(error==ESP_OK&&!package_job.request.local&&(strcmp(package_job.parsed->id,package_job.request.app_id)||package_job.parsed->version_code!=package_job.request.version_code))error=ESP_ERR_INVALID_ARG;
        if(error==ESP_OK)error=on_ui(0,package_job.parsed,NULL);
        if(error==ESP_OK&&package_stop(NULL))error=ESP_ERR_INVALID_STATE;
        if(error!=ESP_OK){package_finish(error,package_stop(NULL));return;}
        ouse_bpk_t *parsed=package_job.parsed;int old=find_package(parsed->id);
        if(old>=0&&parsed->version_code<installed[old]->version_code){package_finish(ESP_ERR_INVALID_VERSION,false);return;}
        snprintf(package_prompt,sizeof(package_prompt),"%s\n%s\n%s → %s\n%s\n%.100s\n功能：%s%s%s%s%s%s%s",parsed->name_zh[0]?parsed->name_zh:parsed->name,parsed->id,old>=0?(installed[old]->version_name[0]?installed[old]->version_name:"旧版本"):"未安装",parsed->version_name[0]?parsed->version_name:"无版本",package_job.request.local?"SD 卡安装":!strncmp(package_job.request.location,"http://",7)?"HTTP 未加密来源":"HTTPS 来源",package_job.request.location,parsed->package.network_allowed?"网络 ":"",parsed->package.media_allowed?"音频 ":"",parsed->package.camera_allowed?"摄像头 ":"",(parsed->package.files_allowed||parsed->package.sdcard_allowed||parsed->package.photos_allowed)?"文件 ":"",parsed->package.bluetooth_allowed?"蓝牙 ":"",parsed->package.hid_allowed?"HID ":"",parsed->package.packages_allowed?"安装应用":"");
        atomic_store(&package_decision,0);error=on_ui(7,NULL,NULL);
        if(error!=ESP_OK){package_finish(error,false);return;}
        package_job.waiting=true;package_job.since=esp_timer_get_time();ouse_packages_progress("confirmation",package_job.size,package_job.size,parsed->id);return;
    }
    if(package_stop(NULL)||esp_timer_get_time()-package_job.since>120000000){package_finish(ESP_ERR_INVALID_STATE,true);return;}
    if(!atomic_load(&package_decision)){if(on_ui(8,NULL,NULL)!=ESP_OK)package_finish(ESP_OK,true);return;}
    if(package_stop(NULL)){package_finish(ESP_OK,true);return;}
    ouse_packages_progress("installing",package_job.size,package_job.size,package_job.parsed->id);char result[80];ouse_bpk_t *parsed=package_job.parsed;package_job.parsed=NULL;
    esp_err_t error=commit_package(parsed,package_job.bytes,package_job.size,result,sizeof(result));package_finish(error,false);
}
static bool number8(const char *text,uint32_t *n)
{
    if(strlen(text)!=8)return false;
    for(unsigned i=0;i<8;++i)if(!((text[i]>='0' && text[i]<='9') || (text[i]>='a' && text[i]<='f') || (text[i]>='A' && text[i]<='F')))return false;
    *n=(uint32_t)strtoul(text,NULL,16);return true;
}
static esp_err_t command(ouse_frame_t *f,char *result,size_t cap)
{
    if(package_job.active&&(!strcmp(f->verb,"REMOVE")||!strcmp(f->verb,"BEGIN")))return ESP_ERR_INVALID_STATE;
    if(upload && owner!=f->token)return ESP_ERR_INVALID_STATE;
    activity=esp_timer_get_time();
    if(!strcmp(f->verb,"HELLO")){
        snprintf(result,cap,"core=%s,storage=%s,max=%u,transport=uart0-921600,installStages=1",ouse_core_version(),storage_ready?"ready":"unavailable",OUSE_BPK_MAX);
#if CONFIG_OUSE_DEVICE_DEBUG
        strncat(result,ouse_system_debug_enabled()?",debug=1":",debug=0",cap-strlen(result)-1);
#endif
        return ESP_OK;
    }
    if(!strcmp(f->verb,"ABORT")){abort_upload();return ESP_OK;}
#if CONFIG_OUSE_DEVICE_DEBUG
    if(!strncmp(f->verb,"DBG_",4))return upload?ESP_ERR_INVALID_STATE:ouse_debug_command(f->verb,f->payload,result,cap);
#endif
    if(!storage_ready)return ESP_ERR_INVALID_STATE;
    if(ouse_tasks_quiescing())return ESP_ERR_INVALID_STATE;
    if(!strcmp(f->verb,"LIST") || !strcmp(f->verb,"LIST_PAGE")){
        bool paged=!strcmp(f->verb,"LIST_PAGE");
        uint32_t offset=0;
        if(strcmp(f->payload,"-") && !number8(f->payload,&offset))return ESP_ERR_INVALID_ARG;
        size_t pos=paged ? 9 : 0;unsigned ordinal=0,next=0;
        if(paged)memcpy(result,"00000000:",10);
        for(unsigned index=0;index<installed_capacity;++index)if(installed[index]){
            if(ordinal++<offset)continue;
            bool separator=pos>(paged ? 9 : 0);
            size_t needed=strlen(installed[index]->id)+(separator ? 1 : 0);
            if(needed>=cap-pos){if(!paged)return ESP_ERR_INVALID_SIZE;next=ordinal-1;break;}
            memcpy(result+pos+(separator ? 1 : 0),installed[index]->id,strlen(installed[index]->id)+1);
            if(separator)result[pos]=',';
            pos+=needed;
        }
        if(paged){char header[10];snprintf(header,sizeof(header),"%08x:",next);memcpy(result,header,9);}
        return ESP_OK;
    }
    if(!strcmp(f->verb,"BEGIN")){
        if(package_job.active)return ESP_ERR_INVALID_STATE;
        if(pending_remove[0] || pending_reset[0] || recovery_error!=ESP_OK || ouse_manager_pending())return ESP_ERR_INVALID_STATE;
        if(upload)return ESP_ERR_INVALID_STATE;
        char *colon=strchr(f->payload,':');uint32_t n,crc;
        if(!colon)return ESP_ERR_INVALID_ARG;
        *colon=0;
        if(!number8(f->payload,&n) || !number8(colon+1,&crc) || !n || n>OUSE_BPK_MAX)return ESP_ERR_INVALID_ARG;
        upload=heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!upload)return ESP_ERR_NO_MEM;
        expected=n;received=0;upload_crc=crc;owner=f->token;return ESP_OK;
    }
    if(!strcmp(f->verb,"DATA")){
        if(!upload)return ESP_ERR_INVALID_STATE;
        char *colon=strchr(f->payload,':');uint32_t offset;size_t n;unsigned char data[512];
        if(!colon)return ESP_ERR_INVALID_ARG;
        *colon=0;
        if(!number8(f->payload,&offset) || !ouse_hex_decode(colon+1,data,sizeof(data),&n) || !n ||
           offset!=received || n>expected-received)return ESP_ERR_INVALID_ARG;
        memcpy(upload+received,data,n);received+=n;snprintf(result,cap,"%u",(unsigned)received);return ESP_OK;
    }
    if(!strcmp(f->verb,"END")){
        const ouse_task_owner_t system={.app_id="ouse.installer"};uint32_t task_id=0;
        snprintf(result,cap,"install-task-admission");
        esp_err_t error=ouse_task_create(&system,OUSE_TASK_PACKAGE,"install",OUSE_TASK_FINISH_ON_CLOSE,false,&task_id);
        if(error==ESP_OK){(void)ouse_task_start(task_id,false);error=install_upload(result,cap);ouse_task_finish(task_id,error,false);}
        abort_upload();return error;
    }
    if(upload)return ESP_ERR_INVALID_STATE;
    if(!strcmp(f->verb,"OPEN")){
        if(!valid_id(f->payload) || find_package(f->payload)<0)return ESP_ERR_NOT_FOUND;
        return on_ui(3,NULL,f->payload);
    }
    if(!strcmp(f->verb,"REMOVE")){
        if(ouse_manager_pending() || pending_reset[0])return ESP_ERR_INVALID_STATE;
        if(!valid_id(f->payload))return ESP_ERR_INVALID_ARG;
        int slot=find_package(f->payload);
        if(slot<0 && strcmp(f->payload,pending_remove))return ESP_ERR_NOT_FOUND;
        const ouse_task_owner_t system={.app_id="ouse.installer"};uint32_t task_id=0;
        esp_err_t registered=ouse_task_create(&system,OUSE_TASK_PACKAGE,"uninstall",OUSE_TASK_FINISH_ON_CLOSE,false,&task_id);
        if(registered!=ESP_OK)return registered;
        (void)ouse_task_start(task_id,false);
        esp_err_t error=begin_uninstall(f->payload);
        ouse_task_finish(task_id,error,false);return error;
    }
    return ESP_ERR_NOT_SUPPORTED;
}
static void send_response(const char *response)
{
    // One stdio write, with a leading newline: logs can be discarded by the PC
    // without treating console text as binary protocol or toggling DTR/RTS.
    printf("\n%s\n",response);fflush(stdout);
}
static void process_line(char *line)
{
    static uint32_t token,sequence,checksum;static bool cached;
    static char response[OUSE_WIRE_MAX];
    ouse_frame_t frame;if(!ouse_frame_parse(line,&frame))return;
    static bool previous_debug;
    bool debug_enabled=ouse_system_debug_enabled();
    if(debug_enabled!=previous_debug){cached=false;previous_debug=debug_enabled;}
    if(cached && frame.token==token && frame.sequence==sequence){
        if(frame.checksum==checksum){if(upload)activity=esp_timer_get_time();send_response(response);}
        return; // conflicting retransmission never executes
    }
    if(cached && frame.token==token && frame.sequence<sequence)return;
    bool observing=!strncmp(frame.verb,"DBG_",4) || !strcmp(frame.verb,"HELLO") || !strcmp(frame.verb,"LIST") || !strcmp(frame.verb,"LIST_PAGE");
    if(!observing)publish_status(true,recovery_error);
    char result[OUSE_WIRE_MAX-64]="-";esp_err_t error=command(&frame,result,sizeof(result));
    if(!strcmp(frame.verb,"END") || !strcmp(frame.verb,"REMOVE"))(void)refresh_manager();
    if(!observing)publish_status(false,recovery_error!=ESP_OK ? recovery_error : error);
    char failure[160];
    snprintf(failure,sizeof(failure),"%s%s%.96s",esp_err_to_name(error),
        strcmp(result,"-") ? ":" : "",strcmp(result,"-") ? result : "");
    int n=snprintf(response,sizeof(response),"OPK1 %08lx %08lx %s %s",(unsigned long)frame.token,(unsigned long)frame.sequence,
        error==ESP_OK?"OK":"ERR",error==ESP_OK?(result[0]?result:"-"):failure);
    uint32_t crc=ouse_crc32(response,(size_t)n);
    snprintf(response+n,sizeof(response)-(size_t)n," %08lx",(unsigned long)crc);
    token=frame.token;sequence=frame.sequence;checksum=frame.checksum;cached=true;send_response(response);
}
static char *rx_line;
static size_t rx_length;
static bool rx_dropping;
static void consume_bytes(const unsigned char *bytes,int n)
{
        for(int i=0;i<n;++i){
            char c=(char)bytes[i];
            if(c=='\r')continue;
            if(c=='\n'){
                if(!rx_dropping && rx_length){rx_line[rx_length]=0;process_line(rx_line);}
                rx_length=0;rx_dropping=false;
            }else if(!rx_dropping){
                if(c<32 || c>126 || rx_length>=OUSE_WIRE_MAX-1){rx_length=0;rx_dropping=true;}
                else rx_line[rx_length++]=c;
            }
        }
        if(upload && esp_timer_get_time()-activity>30000000){abort_upload();publish_status(false,recovery_error);rx_length=0;rx_dropping=false;ESP_LOGW(TAG,"Upload timeout; previous app preserved");}
}
static void worker(void *context)
{
    (void)context;
    publish_status(true,ESP_OK);
    esp_err_t error=mount_storage();
    publish_status(true,error);
    if(error==ESP_OK){
        recovery_error=ouse_uninstall_intent_read(pending_remove);
        if(recovery_error==ESP_ERR_NOT_FOUND)recovery_error=ESP_OK;
        if(recovery_error==ESP_OK){esp_err_t reset=ouse_reset_intent_read(pending_reset);if(reset!=ESP_OK && reset!=ESP_ERR_NOT_FOUND)recovery_error=reset;}
        // An unreadable intent cannot identify the retiring app. Preserve all
        // files and defer package activation until explicitly repaired.
        if(recovery_error==ESP_OK)recovery_error=seed_store();
        if(recovery_error==ESP_OK)restore_packages();
        const char **alarm_apps=heap_caps_calloc(installed_capacity ? installed_capacity : 1,sizeof(*alarm_apps),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);unsigned count=0;
        if(alarm_apps)for(unsigned i=0;i<installed_capacity;++i)if(installed[i] && installed[i]->package.alarms_allowed && strcmp(installed[i]->id,pending_reset))alarm_apps[count++]=installed[i]->id;
        esp_err_t alarm_error=alarm_apps ? ouse_alarm_start(alarm_apps,count) : ESP_ERR_NO_MEM;
        free(alarm_apps);
        if(alarm_error!=ESP_OK)ESP_LOGW(TAG,"Alarm service unavailable: %s; file preserved",esp_err_to_name(alarm_error));
        if(pending_remove[0] && recovery_error==ESP_OK)recovery_error=finish_uninstall(pending_remove);
        if(pending_reset[0] && recovery_error==ESP_OK)recovery_error=finish_reset(pending_reset);
        (void)refresh_manager();
    }else ESP_LOGE(TAG,"Storage unavailable: %s; existing data preserved",esp_err_to_name(error));
    if(error==ESP_OK && recovery_error==ESP_OK)(void)on_ui(6,NULL,NULL);
    publish_status(false,error!=ESP_OK ? error : recovery_error);
    ESP_LOGI(TAG,"READY; UART0 921600; protocol=OPK1; storage=%s",storage_ready?"ready":"unavailable");
    unsigned char bytes[256];
    for(;;){
        process_package();
        if(!package_job.active)process_manager();
#if CONFIG_OUSE_DEVICE_DEBUG
        ouse_debug_poll();
#endif
        int n=uart_read_bytes(UART_NUM_0,bytes,1,pdMS_TO_TICKS(250));
        if(n>0){size_t buffered=0;uart_get_buffered_data_len(UART_NUM_0,&buffered);
            if(buffered>sizeof(bytes)-1)buffered=sizeof(bytes)-1;
            if(buffered){int more=uart_read_bytes(UART_NUM_0,bytes+1,buffered,0);if(more>0)n+=more;}
        }
        consume_bytes(bytes,n);
    }
}
esp_err_t ouse_installer_start(void)
{
    if(started || !ouse_core_is_started())return ESP_ERR_INVALID_STATE;
    esp_err_t manager=ouse_manager_init();if(manager!=ESP_OK)return manager;
    manager=ouse_packages_init();if(manager!=ESP_OK)return manager;
    // Refuse to share RX ownership with a console REPL or another installer.
    if(uart_is_driver_installed(UART_NUM_0))return ESP_ERR_INVALID_STATE;
    rx_line=malloc(OUSE_WIRE_MAX);if(!rx_line)return ESP_ERR_NO_MEM;
    ui_done=xSemaphoreCreateBinary();if(!ui_done){free(rx_line);rx_line=NULL;return ESP_ERR_NO_MEM;}
    uart_config_t config={.baud_rate=921600,.data_bits=UART_DATA_8_BITS,.parity=UART_PARITY_DISABLE,
        .stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    esp_err_t error=uart_param_config(UART_NUM_0,&config);
    if(error==ESP_OK)error=uart_driver_install(UART_NUM_0,16384,0,0,NULL,0);
    if(error!=ESP_OK){vSemaphoreDelete(ui_done);ui_done=NULL;free(rx_line);rx_line=NULL;return error;}
    publish_status(true,ESP_OK);
    if(xTaskCreate(worker,"ouse_installer",16384,NULL,3,NULL)!=pdPASS){uart_driver_delete(UART_NUM_0);vSemaphoreDelete(ui_done);ui_done=NULL;free(rx_line);rx_line=NULL;publish_status(false,ESP_ERR_NO_MEM);return ESP_ERR_NO_MEM;}
    started=true;return ESP_OK;
}
