// SPDX-License-Identifier: MIT
// All private-data filesystem work belongs to this task. Request submission,
// UI queries and completion delivery contain no filesystem operations.
#include "ouse_storage.h"
#include "ouse_tasks.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <dirent.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DATA_DIRECTORY OUSE_STORAGE_BASE "/ouse-data"
#define PARTITION_LABEL "littlefs_data"
#define PATH_CAPACITY 192
#define STORAGE_TASK_STACK (10 * 1024)
static const char *TAG="OuseStorage";

enum { JOB_PURGE=OUSE_STORAGE_INFO+1, JOB_REFRESH };
typedef struct {
    int operation;
    char app_id[48],key[OUSE_STORAGE_KEY_MAX+1];
    char *value;
    size_t value_size;
    uint32_t token;
    ouse_storage_complete_t complete;
    void *context;
    SemaphoreHandle_t barrier;
    esp_err_t barrier_error;
} storage_job_t;
typedef struct {
    size_t bytes;
    unsigned count;
    char keys[OUSE_STORAGE_KEYS_MAX][OUSE_STORAGE_KEY_MAX+1];
} app_scan_t;
static QueueHandle_t jobs;
static SemaphoreHandle_t started;
static TaskHandle_t storage_task;
static esp_err_t startup_error=ESP_ERR_INVALID_STATE;
static portMUX_TYPE cache_lock=portMUX_INITIALIZER_UNLOCKED;
static ouse_storage_stats_t cache;
static atomic_bool refresh_requested;

static bool valid_app(const char *id)
{
    if(!id||strncmp(id,"user.",5)||!id[5]||strnlen(id,48)>=48)return false;
    for(const char *p=id;*p;++p)
        if(!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='.'||*p=='_'||*p=='-'))return false;
    return true;
}
static bool alphanumeric(unsigned char c)
{ return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9'); }
static bool valid_key(const char *key)
{
    if(!key||!alphanumeric((unsigned char)*key)||strnlen(key,OUSE_STORAGE_KEY_MAX+1)>OUSE_STORAGE_KEY_MAX)return false;
    for(const unsigned char *p=(const unsigned char *)key;*p;++p)
        if(!alphanumeric(*p)&&*p!='.'&&*p!='_'&&*p!='-')return false;
    return true;
}
static bool json_filename(const char *name,char *key)
{
    size_t length=strlen(name);
    if(length<=5||length>OUSE_STORAGE_KEY_MAX+5||strcmp(name+length-5,".json"))return false;
    memcpy(key,name,length-5);key[length-5]=0;return valid_key(key);
}
static bool suffix(const char *name,const char *ending)
{ size_t n=strlen(name),e=strlen(ending);return n>e&&!strcmp(name+n-e,ending); }
static void *allocate(size_t size)
{
    void *result=heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    return result ? result : malloc(size);
}
static char *json_copy(const char *text)
{
    size_t size=strlen(text)+1;char *result=allocate(size);
    if(result)memcpy(result,text,size);
    return result;
}
static esp_err_t directory(const char *path)
{
    if(mkdir(path,0755)==0)return ESP_OK;
    if(errno!=EEXIST)return ESP_FAIL;
    struct stat state;return stat(path,&state)==0&&S_ISDIR(state.st_mode) ? ESP_OK : ESP_FAIL;
}
static void app_path(const char *id,char *path,size_t capacity)
{ snprintf(path,capacity,DATA_DIRECTORY "/%s",id); }
static void key_path(const char *id,const char *key,char *path,size_t capacity,bool temporary)
{ snprintf(path,capacity,DATA_DIRECTORY "/%s/%s.%s",id,key,temporary ? "tmp" : "json"); }

static esp_err_t mount_storage(void)
{
    esp_vfs_littlefs_conf_t config={.base_path=OUSE_STORAGE_BASE,.partition_label=PARTITION_LABEL,.format_if_mount_failed=false};
    esp_err_t error=esp_vfs_littlefs_register(&config);
    if(error!=ESP_OK) {
        // A corrupt/nonempty partition is never a reason to format. Scan every
        // byte of the partition before provisioning an entirely erased device.
        const esp_partition_t *partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_LITTLEFS,PARTITION_LABEL);
        bool blank=partition!=NULL;unsigned char *buffer=allocate(4096);
        if(!buffer)return ESP_ERR_NO_MEM;
        for(size_t offset=0;blank&&offset<partition->size;offset+=4096) {
            size_t length=partition->size-offset;if(length>4096)length=4096;
            if(esp_partition_read(partition,offset,buffer,length)!=ESP_OK){blank=false;break;}
            for(size_t i=0;i<length;++i)if(buffer[i]!=0xff){blank=false;break;}
            // Provisioning can read several MiB. Yield between blocks without
            // holding a UI lock or starving the native display task.
            vTaskDelay(1);
        }
        free(buffer);if(!blank)return error;
        config.format_if_mount_failed=true;error=esp_vfs_littlefs_register(&config);
    }
    if(error==ESP_OK)error=directory(OUSE_STORAGE_PACKAGE_DIRECTORY);
    if(error==ESP_OK)error=directory(DATA_DIRECTORY);
    return error;
}
static esp_err_t scan_app(const char *app_id,app_scan_t *scan)
{
    memset(scan,0,sizeof(*scan));char path[PATH_CAPACITY];app_path(app_id,path,sizeof(path));
    DIR *dir=opendir(path);if(!dir)return errno==ENOENT ? ESP_OK : ESP_FAIL;
    esp_err_t error=ESP_OK;struct dirent *entry;
    while((entry=readdir(dir))!=NULL) {
        char key[OUSE_STORAGE_KEY_MAX+1],file[PATH_CAPACITY];
        if(!json_filename(entry->d_name,key))continue;
        key_path(app_id,key,file,sizeof(file),false);struct stat state;
        if(stat(file,&state)!=0||!S_ISREG(state.st_mode)||state.st_size<0){error=ESP_FAIL;break;}
        if(scan->count<OUSE_STORAGE_KEYS_MAX)strcpy(scan->keys[scan->count],key);
        ++scan->count;scan->bytes+=(size_t)state.st_size;
    }
    if(closedir(dir)!=0)error=ESP_FAIL;
    return error;
}
static esp_err_t scan_packages(size_t *bytes,unsigned *count)
{
    DIR *dir=opendir(OUSE_STORAGE_PACKAGE_DIRECTORY);if(!dir)return ESP_FAIL;
    esp_err_t error=ESP_OK;struct dirent *entry;
    while((entry=readdir(dir))!=NULL) {
        if(!suffix(entry->d_name,".bpk")||strchr(entry->d_name,'/'))continue;
        char path[PATH_CAPACITY];int n=snprintf(path,sizeof(path),OUSE_STORAGE_PACKAGE_DIRECTORY "/%s",entry->d_name);
        if(n<0||(size_t)n>=sizeof(path)){error=ESP_ERR_INVALID_SIZE;break;}
        struct stat state;
        // The installer atomically renames/removes packages on another task.
        // A vanished entry is a normal concurrent snapshot, not corruption.
        if(stat(path,&state)!=0){if(errno==ENOENT)continue;error=ESP_FAIL;break;}
        if(!S_ISREG(state.st_mode)||state.st_size<0)continue;
        *bytes+=(size_t)state.st_size;++*count;
    }
    if(closedir(dir)!=0)error=ESP_FAIL;
    return error;
}
static esp_err_t scan_private(size_t *bytes,unsigned *count)
{
    DIR *dir=opendir(DATA_DIRECTORY);if(!dir)return ESP_FAIL;
    esp_err_t error=ESP_OK;struct dirent *entry;
    while((entry=readdir(dir))!=NULL) {
        if(!valid_app(entry->d_name))continue;
        app_scan_t scan;error=scan_app(entry->d_name,&scan);if(error!=ESP_OK)break;
        *bytes+=scan.bytes;if(scan.count)++*count;
    }
    if(closedir(dir)!=0)error=ESP_FAIL;
    return error;
}
static void refresh_stats(esp_err_t operation_error)
{
    ouse_storage_stats_t next={.ready=startup_error==ESP_OK,.last_error=operation_error};
    if(next.ready) {
        esp_err_t error=esp_littlefs_info(PARTITION_LABEL,&next.total_bytes,&next.used_bytes);
        if(error==ESP_OK)error=scan_packages(&next.package_bytes,&next.package_count);
        if(error==ESP_OK)error=scan_private(&next.private_bytes,&next.private_apps);
        if(error!=ESP_OK)next.last_error=error;
        next.free_bytes=next.used_bytes<next.total_bytes ? next.total_bytes-next.used_bytes : 0;
    }
    portENTER_CRITICAL(&cache_lock);next.revision=cache.revision+1;cache=next;portEXIT_CRITICAL(&cache_lock);
}
static esp_err_t read_value(const storage_job_t *job,char **result)
{
    char path[PATH_CAPACITY];key_path(job->app_id,job->key,path,sizeof(path),false);
    struct stat state;
    if(stat(path,&state)!=0) {
        if(errno!=ENOENT)return ESP_FAIL;
        *result=json_copy("null");return *result ? ESP_OK : ESP_ERR_NO_MEM;
    }
    if(!S_ISREG(state.st_mode)||state.st_size<1||state.st_size>OUSE_STORAGE_VALUE_MAX)return ESP_ERR_INVALID_SIZE;
    size_t length=(size_t)state.st_size;char *value=allocate(length+1);if(!value)return ESP_ERR_NO_MEM;
    FILE *file=fopen(path,"rb");bool ok=file!=NULL;
    if(file){ok=fread(value,1,length,file)==length&&!ferror(file);if(fclose(file)!=0)ok=false;}
    if(!ok){free(value);return ESP_FAIL;}
    if(memchr(value,0,length)){free(value);return ESP_ERR_INVALID_RESPONSE;}
    value[length]=0;*result=value;return ESP_OK;
}
static esp_err_t write_value(const storage_job_t *job,char **result)
{
    app_scan_t scan;esp_err_t error=scan_app(job->app_id,&scan);if(error!=ESP_OK)return error;
    char path[PATH_CAPACITY],temporary[PATH_CAPACITY],parent[PATH_CAPACITY];
    key_path(job->app_id,job->key,path,sizeof(path),false);key_path(job->app_id,job->key,temporary,sizeof(temporary),true);
    struct stat old;size_t old_size=0;bool exists=stat(path,&old)==0;
    if(exists){if(!S_ISREG(old.st_mode)||old.st_size<0)return ESP_FAIL;old_size=(size_t)old.st_size;}
    else if(errno!=ENOENT)return ESP_FAIL;
    if((!exists&&scan.count>=OUSE_STORAGE_KEYS_MAX)||old_size>scan.bytes||
       scan.bytes-old_size>OUSE_STORAGE_QUOTA_BYTES-job->value_size)return ESP_ERR_INVALID_SIZE;
    // Allocate the completion before committing: an allocation failure must
    // never report a write as failed after that write has become durable.
    char *reply=json_copy("true");if(!reply)return ESP_ERR_NO_MEM;
    app_path(job->app_id,parent,sizeof(parent));error=directory(parent);
    if(error!=ESP_OK){free(reply);return error;}
    FILE *file=fopen(temporary,"wb");bool ok=file!=NULL;
    if(file) {
        ok=fwrite(job->value,1,job->value_size,file)==job->value_size;
        if(fflush(file)!=0)ok=false;
        if(fsync(fileno(file))!=0)ok=false;
        if(fclose(file)!=0)ok=false;
    }
    if(!ok||rename(temporary,path)!=0){unlink(temporary);free(reply);return ESP_FAIL;}
    *result=reply;return ESP_OK;
}
static esp_err_t remove_value(const storage_job_t *job,char **result)
{
    char path[PATH_CAPACITY];key_path(job->app_id,job->key,path,sizeof(path),false);
    char *reply=allocate(6);if(!reply)return ESP_ERR_NO_MEM;
    if(unlink(path)==0)strcpy(reply,"true");
    else if(errno==ENOENT)strcpy(reply,"false");
    else {free(reply);return ESP_FAIL;}
    *result=reply;return ESP_OK;
}
static int compare_keys(const void *a,const void *b) {return strcmp(a,b);}
static esp_err_t keys_value(const char *app_id,char **result)
{
    app_scan_t scan;esp_err_t error=scan_app(app_id,&scan);if(error!=ESP_OK)return error;
    if(scan.count>OUSE_STORAGE_KEYS_MAX)return ESP_ERR_INVALID_SIZE;
    qsort(scan.keys,scan.count,sizeof(scan.keys[0]),compare_keys);
    size_t capacity=scan.count*(OUSE_STORAGE_KEY_MAX+3)+3;char *json=allocate(capacity);
    if(!json)return ESP_ERR_NO_MEM;
    size_t used=0;json[used++]='[';
    for(unsigned i=0;i<scan.count;++i){if(i)json[used++]=',';json[used++]='"';size_t n=strlen(scan.keys[i]);memcpy(json+used,scan.keys[i],n);used+=n;json[used++]='"';}
    json[used++]=']';json[used]=0;*result=json;return ESP_OK;
}
static esp_err_t clear_values(const char *app_id,unsigned *removed,bool purge)
{
    char parent[PATH_CAPACITY];app_path(app_id,parent,sizeof(parent));
    DIR *dir=opendir(parent);if(!dir)return errno==ENOENT ? ESP_OK : ESP_FAIL;
    esp_err_t error=ESP_OK;struct dirent *entry;
    while((entry=readdir(dir))!=NULL) {
        if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
        // Only regular files are ever created inside a private app directory;
        // preserve unexpected nested directories rather than recursing through
        // untrusted paths. Their presence makes purge explicitly fail.
        if(strchr(entry->d_name,'/')){error=ESP_FAIL;break;}
        char path[PATH_CAPACITY];int n=snprintf(path,sizeof(path),"%s/%s",parent,entry->d_name);
        if(n<0||(size_t)n>=sizeof(path)){error=ESP_ERR_INVALID_SIZE;break;}
        struct stat state;
        if(stat(path,&state)!=0||!S_ISREG(state.st_mode)){error=ESP_FAIL;break;}
        char key[OUSE_STORAGE_KEY_MAX+1];bool committed=json_filename(entry->d_name,key);
        if(!purge&&!committed&&!suffix(entry->d_name,".tmp"))continue;
        if(unlink(path)!=0){error=ESP_FAIL;break;}
        if(committed)++*removed;
    }
    if(closedir(dir)!=0)error=ESP_FAIL;
    if(error==ESP_OK&&purge&&rmdir(parent)!=0&&errno!=ENOENT)error=ESP_FAIL;
    return error;
}
static esp_err_t execute(storage_job_t *job,char **result)
{
    switch(job->operation) {
    case OUSE_STORAGE_GET:return read_value(job,result);
    case OUSE_STORAGE_SET:return write_value(job,result);
    case OUSE_STORAGE_REMOVE:return remove_value(job,result);
    case OUSE_STORAGE_KEYS:return keys_value(job->app_id,result);
    case OUSE_STORAGE_CLEAR: {
        // Reserve the tiny reply before deleting any files, for the same
        // commit/result reason as SET. At most 32 committed keys exist.
        char *reply=allocate(16);if(!reply)return ESP_ERR_NO_MEM;
        unsigned removed=0;esp_err_t error=clear_values(job->app_id,&removed,false);
        if(error!=ESP_OK){free(reply);return error;}
        snprintf(reply,16,"%u",removed);*result=reply;return ESP_OK;
    }
    case OUSE_STORAGE_INFO: {
        app_scan_t scan;esp_err_t error=scan_app(job->app_id,&scan);if(error!=ESP_OK)return error;
        char text[180];snprintf(text,sizeof(text),"{\"usedBytes\":%zu,\"quotaBytes\":%u,\"keyCount\":%u,\"keyLimit\":%u,\"valueLimit\":%u}",
            scan.bytes,OUSE_STORAGE_QUOTA_BYTES,scan.count,OUSE_STORAGE_KEYS_MAX,OUSE_STORAGE_VALUE_MAX);
        *result=json_copy(text);return *result ? ESP_OK : ESP_ERR_NO_MEM;
    }
    case JOB_PURGE: {unsigned removed=0;return clear_values(job->app_id,&removed,true);}
    default:return ESP_ERR_INVALID_ARG;
    }
}
static esp_err_t cleanup_orphans(void)
{
    DIR *dir=opendir(DATA_DIRECTORY);if(!dir)return ESP_FAIL;
    esp_err_t first_error=ESP_OK;struct dirent *entry;
    while((entry=readdir(dir))!=NULL) {
        if(!valid_app(entry->d_name))continue;
        char package[PATH_CAPACITY];
        snprintf(package,sizeof(package),OUSE_STORAGE_PACKAGE_DIRECTORY "/%.47s.bpk",entry->d_name);
        struct stat state;
        // A damaged, empty or unparseable package still owns its data. Only
        // explicit ENOENT proves the uninstall commit survived a power loss.
        if(stat(package,&state)==0)continue;
        if(errno!=ENOENT) {if(first_error==ESP_OK)first_error=ESP_FAIL;continue;}
        unsigned removed=0;esp_err_t error=clear_values(entry->d_name,&removed,true);
        if(error!=ESP_OK) {
            if(first_error==ESP_OK)first_error=error;
            ESP_LOGW(TAG,"Orphan cleanup deferred for %s: %s",entry->d_name,esp_err_to_name(error));
        } else ESP_LOGI(TAG,"Removed orphan data: %s (%u keys)",entry->d_name,removed);
    }
    if(closedir(dir)!=0&&first_error==ESP_OK)first_error=ESP_FAIL;
    return first_error;
}
static void worker(void *unused)
{
    (void)unused;startup_error=mount_storage();
    esp_err_t recovery_error=startup_error;
    if(startup_error==ESP_OK)recovery_error=cleanup_orphans();
    // Failure to clean an unexpected orphan must not hide unrelated mounted
    // packages. Preserve readiness while exposing recovery failure in stats.
    refresh_stats(recovery_error);
    if(startup_error==ESP_OK&&recovery_error!=ESP_OK)
        ESP_LOGW(TAG,"Orphan recovery incomplete: %s; mounted data remains available",esp_err_to_name(recovery_error));
    ESP_LOGI(TAG,"Storage %s; private app I/O uses native worker",startup_error==ESP_OK ? "ready" : esp_err_to_name(startup_error));
    xSemaphoreGive(started);
    for(;;) {
        storage_job_t *job=NULL;
        if(xQueueReceive(jobs,&job,portMAX_DELAY)!=pdTRUE)continue;
        char *result=NULL;
        bool tracked=job->complete!=NULL;
        bool claimed=!tracked || ouse_task_start(job->token,false);
        esp_err_t error=!claimed ? ESP_ERR_INVALID_STATE : job->operation==JOB_REFRESH ? ESP_OK : execute(job,&result);
        // Clear before scanning. A package change racing with this snapshot
        // can then queue another refresh instead of being silently coalesced
        // into a scan that has already passed the package directory.
        atomic_store(&refresh_requested,false);refresh_stats(error);
        if(tracked){
            ouse_task_result_t summary={.present=claimed && error==ESP_OK,.value.storage={.json_bytes=result ? strlen(result) : 0}};
            ouse_task_finish_result(job->token,error,!claimed,&summary);
        }
        if(job->barrier) {
            job->barrier_error=error;xSemaphoreGive(job->barrier);
            // The synchronous caller owns this job after giving the barrier.
            continue;
        }
        ouse_storage_complete_t complete=job->complete;void *context=job->context;uint32_t token=job->token;
        free(job->value);free(job);
        if(complete)complete(context,token,error,result);else free(result);
    }
}
esp_err_t ouse_storage_start(void)
{
    // Startup is owned by the single installer task. Public submit/stats may
    // race with mounting; they observe ready=false until the worker publishes.
    if(storage_task)return startup_error;
    started=xSemaphoreCreateBinary();jobs=xQueueCreate(OUSE_STORAGE_QUEUE_CAPACITY,sizeof(storage_job_t *));
    if(!started||!jobs) {
        if(started)vSemaphoreDelete(started);
        if(jobs)vQueueDelete(jobs);
        started=NULL;jobs=NULL;return ESP_ERR_NO_MEM;
    }
    atomic_init(&refresh_requested,false);
    if(xTaskCreate(worker,"ouse_storage",STORAGE_TASK_STACK,NULL,2,&storage_task)!=pdPASS) {
        vSemaphoreDelete(started);vQueueDelete(jobs);started=NULL;jobs=NULL;return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(started,portMAX_DELAY);return startup_error;
}
esp_err_t ouse_storage_submit(ouse_storage_operation_t operation,const ouse_task_owner_t *owner,
    const char *key,const char *value,uint32_t *task_id,ouse_storage_complete_t complete,void *context)
{
    if(!owner || !task_id)return ESP_ERR_INVALID_ARG;
    *task_id=0;const char *app_id=owner->app_id;
    if(operation<OUSE_STORAGE_GET||operation>OUSE_STORAGE_INFO||!valid_app(app_id)||!complete)return ESP_ERR_INVALID_ARG;
    bool needs_key=operation==OUSE_STORAGE_GET||operation==OUSE_STORAGE_SET||operation==OUSE_STORAGE_REMOVE;
    if(needs_key&&!valid_key(key))return ESP_ERR_INVALID_ARG;
    size_t size=0;
    if(operation==OUSE_STORAGE_SET) {
        if(!value||(size=strnlen(value,OUSE_STORAGE_VALUE_MAX+1))<1||size>OUSE_STORAGE_VALUE_MAX)return ESP_ERR_INVALID_SIZE;
    }
    ouse_storage_stats_t state;ouse_storage_get_stats(&state);if(!state.ready)return ESP_ERR_INVALID_STATE;
    storage_job_t *job=calloc(1,sizeof(*job));if(!job)return ESP_ERR_NO_MEM;
    job->operation=operation;strcpy(job->app_id,app_id);if(needs_key)strcpy(job->key,key);
    job->complete=complete;job->context=context;
    if(operation==OUSE_STORAGE_SET) {
        job->value=allocate(size+1);if(!job->value){free(job);return ESP_ERR_NO_MEM;}
        memcpy(job->value,value,size+1);job->value_size=size;
    }
    static const char *operations[]={"get","set","remove","keys","clear","info"};
    esp_err_t registered=ouse_task_create(owner,OUSE_TASK_STORAGE,operations[operation],OUSE_TASK_FINISH_ON_CLOSE,true,task_id);
    if(registered!=ESP_OK){free(job->value);free(job);return registered;}
    job->token=*task_id;
    if(xQueueSend(jobs,&job,0)!=pdTRUE){ouse_task_finish(job->token,ESP_ERR_NO_MEM,false);free(job->value);free(job);return ESP_ERR_NO_MEM;}
    return ESP_OK;
}
esp_err_t ouse_storage_purge(const char *app_id)
{
    if(!valid_app(app_id))return ESP_ERR_INVALID_ARG;
    if(xTaskGetCurrentTaskHandle()==storage_task)return ESP_ERR_INVALID_STATE;
    ouse_storage_stats_t state;ouse_storage_get_stats(&state);if(!state.ready)return ESP_ERR_INVALID_STATE;
    storage_job_t *job=calloc(1,sizeof(*job));if(!job)return ESP_ERR_NO_MEM;
    job->operation=JOB_PURGE;strcpy(job->app_id,app_id);job->barrier=xSemaphoreCreateBinary();
    if(!job->barrier){free(job);return ESP_ERR_NO_MEM;}
    xQueueSend(jobs,&job,portMAX_DELAY);xSemaphoreTake(job->barrier,portMAX_DELAY);
    esp_err_t error=job->barrier_error;vSemaphoreDelete(job->barrier);free(job);return error;
}
esp_err_t ouse_storage_refresh(void)
{
    ouse_storage_stats_t state;ouse_storage_get_stats(&state);if(!state.ready)return ESP_ERR_INVALID_STATE;
    // Every queued operation refreshes after its I/O. If the queue is already
    // full, those future operations provide the refresh without another slot.
    if(atomic_exchange(&refresh_requested,true))return ESP_OK;
    storage_job_t *job=calloc(1,sizeof(*job));if(!job){atomic_store(&refresh_requested,false);return ESP_ERR_NO_MEM;}
    job->operation=JOB_REFRESH;
    if(xQueueSend(jobs,&job,0)!=pdTRUE)free(job);
    return ESP_OK;
}
esp_err_t ouse_storage_get_stats(ouse_storage_stats_t *out)
{
    if(!out)return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&cache_lock);*out=cache;portEXIT_CRITICAL(&cache_lock);return ESP_OK;
}
