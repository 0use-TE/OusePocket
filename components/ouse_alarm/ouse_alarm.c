// SPDX-License-Identifier: MIT
// One native owner performs persistence, recurrence, sound and notification.
// JS receives snapshots/task results and is never a wakeup dependency.
#include "ouse_alarm.h"
#include "ouse_storage.h"
#include "ouse_devices.h"
#include "ouse_notifications.h"
#include "esp_timer.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DIRECTORY OUSE_STORAGE_BASE "/ouse-system"
#define FILE_NAME DIRECTORY "/alarms.bin"
#define STAGING DIRECTORY "/alarms.tmp"
#define RING_US (60LL*1000000)
#define GRACE_SECONDS 120
// Explicit fixed-size disk schema. Never persist compiler structs/padding.
#define DISK_RECORD_SIZE 344
#define DISK_HEADER_SIZE 16
#define DISK_SIZE (DISK_HEADER_SIZE+OUSE_ALARM_CAPACITY*DISK_RECORD_SIZE)
typedef struct {
    ouse_alarm_snapshot_t records[OUSE_ALARM_CAPACITY];
    unsigned count;
} database_t;
typedef struct {
    char app_id[48];
    ouse_alarm_spec_t spec;
    ouse_alarm_action_t action;
    unsigned snooze;
    uint32_t task;
    bool purge,dismiss_all;
    SemaphoreHandle_t barrier;
    esp_err_t result;
} command_t;
static QueueHandle_t commands;
static portMUX_TYPE cache_lock=portMUX_INITIALIZER_UNLOCKED;
static ouse_alarm_stats_t stats;
static database_t db,cache;
static int64_t ringing_until[OUSE_ALARM_CAPACITY];
static uint32_t ring_task;
static bool any_ringing;
static atomic_bool drain_ack;
bool ouse_alarm_quiesced(void){return !commands || atomic_load(&drain_ack);}
static int64_t ring_retry_at;
static int64_t retry_at;
static esp_err_t last_error;
static char authorized[8][48];
static bool app_valid(const char *s)
{
    if(!s || strncmp(s,"user.",5) || !s[5] || strnlen(s,48)>=48)return false;
    for(;*s;++s)if(!((*s>='a'&&*s<='z')||(*s>='0'&&*s<='9')||*s=='.'||*s=='_'||*s=='-'))return false;
    return true;
}
void ouse_alarm_register_app(const char *app_id,bool allowed)
{
    if(!app_valid(app_id))return;
    portENTER_CRITICAL(&cache_lock);int free_slot=-1,found=-1;
    for(unsigned i=0;i<8;++i){if(!authorized[i][0])free_slot=i;else if(!strcmp(authorized[i],app_id))found=i;}
    if(!allowed && found>=0)authorized[found][0]=0;
    else if(allowed && found<0 && free_slot>=0)strcpy(authorized[free_slot],app_id);
    portEXIT_CRITICAL(&cache_lock);
}
static bool permitted(const char *id)
{
    bool found=false;portENTER_CRITICAL(&cache_lock);
    for(unsigned i=0;i<8;++i)if(!strcmp(authorized[i],id))found=true;
    portEXIT_CRITICAL(&cache_lock);return found;
}
static int64_t utc(void){return (int64_t)time(NULL);}
static bool clock_valid(int64_t now){return now>=OUSE_CLOCK_MIN_UTC && now<OUSE_CLOCK_MAX_UTC;}
static uint32_t get32(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(unsigned char *p,uint32_t v)
{for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(v>>(i*8));}
static int64_t get64(const unsigned char *p)
{return (int64_t)((uint64_t)get32(p)|((uint64_t)get32(p+4)<<32));}
static void put64(unsigned char *p,int64_t v)
{put32(p,(uint32_t)v);put32(p+4,(uint32_t)((uint64_t)v>>32));}
static void publish(esp_err_t error)
{
    int64_t now=utc();unsigned rings=0;
    for(unsigned i=0;i<db.count;++i){db.records[i].ringing=ringing_until[i]>esp_timer_get_time();if(db.records[i].ringing)++rings;}
    portENTER_CRITICAL(&cache_lock);cache=db;stats.count=db.count;stats.ringing=rings;
    stats.clock_valid=clock_valid(now);stats.utc_seconds=now;stats.error=error;++stats.revision;
    portEXIT_CRITICAL(&cache_lock);
}
static esp_err_t save(const database_t *candidate)
{
    unsigned char *bytes=calloc(1,DISK_SIZE);if(!bytes)return ESP_ERR_NO_MEM;
    memcpy(bytes,"OAL1",4);put32(bytes+4,1);put32(bytes+8,candidate->count);
    for(unsigned i=0;i<candidate->count;++i){const ouse_alarm_snapshot_t *a=&candidate->records[i];unsigned char *p=bytes+DISK_HEADER_SIZE+i*DISK_RECORD_SIZE;
        memcpy(p,a->app_id,48);memcpy(p+48,a->spec.id,32);memcpy(p+80,a->spec.title,96);memcpy(p+176,a->spec.payload,128);
        put64(p+304,a->spec.at_utc);put64(p+312,a->next_utc);put64(p+320,a->last_utc);
        put32(p+328,a->spec.minute_of_day);put32(p+332,(uint32_t)(int32_t)a->spec.utc_offset_minutes);
        p[336]=a->spec.weekdays;p[337]=a->enabled;p[338]=a->missed;
    }
    put32(bytes+12,esp_rom_crc32_le(0,bytes+DISK_HEADER_SIZE,DISK_SIZE-DISK_HEADER_SIZE));
    FILE *file=fopen(STAGING,"wb");bool ok=false;
    if(file){ok=fwrite(bytes,1,DISK_SIZE,file)==DISK_SIZE;
        if(fflush(file)!=0)ok=false;
        if(fsync(fileno(file))!=0)ok=false;
        if(fclose(file)!=0)ok=false;
    }
    free(bytes);
    if(!ok || rename(STAGING,FILE_NAME)!=0){unlink(STAGING);return ESP_FAIL;}
    return ESP_OK;
}
static esp_err_t load(void)
{
    if(mkdir(DIRECTORY,0700)!=0 && errno!=EEXIST)return ESP_FAIL;
    FILE *file=fopen(FILE_NAME,"rb");if(!file)return errno==ENOENT ? ESP_OK : ESP_FAIL;
    unsigned char *bytes=malloc(DISK_SIZE);if(!bytes){fclose(file);return ESP_ERR_NO_MEM;}
    size_t size=fread(bytes,1,DISK_SIZE,file);bool ok=size==DISK_SIZE && fgetc(file)==EOF && !ferror(file);
    if(fclose(file)!=0)ok=false;
    if(ok)ok=!memcmp(bytes,"OAL1",4) && get32(bytes+4)==1 && get32(bytes+8)<=OUSE_ALARM_CAPACITY &&
        get32(bytes+12)==esp_rom_crc32_le(0,bytes+DISK_HEADER_SIZE,DISK_SIZE-DISK_HEADER_SIZE);
    if(ok){db.count=get32(bytes+8);
        for(unsigned i=0;ok && i<db.count;++i){ouse_alarm_snapshot_t *a=&db.records[i];const unsigned char *p=bytes+DISK_HEADER_SIZE+i*DISK_RECORD_SIZE;
            memcpy(a->app_id,p,48);memcpy(a->spec.id,p+48,32);memcpy(a->spec.title,p+80,96);memcpy(a->spec.payload,p+176,128);
            a->spec.at_utc=get64(p+304);a->next_utc=get64(p+312);a->last_utc=get64(p+320);
            uint32_t minute=get32(p+328);int32_t offset=(int32_t)get32(p+332);
            a->spec.minute_of_day=minute;a->spec.utc_offset_minutes=offset;a->spec.weekdays=p[336];a->enabled=p[337];a->missed=p[338];
            ok=app_valid(a->app_id) && ouse_alarm_spec_valid(&a->spec) && minute<=1439 && offset>= -720 && offset<=840 && p[337]<=1 && p[338]<=1 &&
                (!a->enabled || (a->next_utc>=OUSE_CLOCK_MIN_UTC && a->next_utc<OUSE_CLOCK_MAX_UTC)) &&
                (!a->last_utc || (a->last_utc>=OUSE_CLOCK_MIN_UTC && a->last_utc<OUSE_CLOCK_MAX_UTC));
            unsigned owned=1;
            for(unsigned j=0;ok && j<i;++j)if(!strcmp(db.records[j].app_id,a->app_id)){
                ++owned;if(!strcmp(db.records[j].spec.id,a->spec.id))ok=false;
            }
            if(owned>OUSE_ALARM_APP_LIMIT)ok=false;
        }
    }
    free(bytes);if(!ok){memset(&db,0,sizeof(db));return ESP_ERR_INVALID_STATE;}return ESP_OK;
}
static void notification(const ouse_alarm_snapshot_t *a,bool active)
{
    char id[48];snprintf(id,sizeof(id),"alarm:%s",a->spec.id);
    if(active){ouse_notification_request_t request={.id=id,.title=a->spec.title,.body="闹钟 / Alarm",.payload=a->spec.payload,.ongoing=true};
        (void)ouse_notifications_publish(a->app_id,&request);
    }else (void)ouse_notifications_cancel(a->app_id,id);
}
static void stop_ringing(unsigned index)
{if(ringing_until[index])notification(&db.records[index],false);ringing_until[index]=0;}
static void remove_at(unsigned index)
{
    stop_ringing(index);--db.count;
    memmove(db.records+index,db.records+index+1,(db.count-index)*sizeof(db.records[0]));
    memmove(ringing_until+index,ringing_until+index+1,(db.count-index)*sizeof(ringing_until[0]));
    memset(&db.records[db.count],0,sizeof(db.records[0]));ringing_until[db.count]=0;
}
static esp_err_t execute(command_t *cmd)
{
    database_t *candidate=malloc(sizeof(*candidate));if(!candidate)return ESP_ERR_NO_MEM;
    *candidate=db;unsigned index=0,owned=0;
    for(unsigned i=0;i<db.count;++i)if(!strcmp(db.records[i].app_id,cmd->app_id))++owned;
    while(index<db.count && (strcmp(db.records[index].app_id,cmd->app_id)||strcmp(db.records[index].spec.id,cmd->spec.id)))++index;
    esp_err_t error=ESP_OK;int64_t now=utc();
    if(cmd->purge){
        for(unsigned i=0;i<candidate->count;)if(!strcmp(candidate->records[i].app_id,cmd->app_id)){
            memmove(candidate->records+i,candidate->records+i+1,(--candidate->count-i)*sizeof(candidate->records[0]));
            memset(&candidate->records[candidate->count],0,sizeof(candidate->records[0]));
        }else ++i;
    }else if(cmd->action==OUSE_ALARM_SET){
        if(index==db.count && (owned>=OUSE_ALARM_APP_LIMIT || db.count>=OUSE_ALARM_CAPACITY)){error=ESP_ERR_NO_MEM;goto done;}
        int64_t next=cmd->spec.weekdays ? ouse_alarm_next(&cmd->spec,clock_valid(now) ? now : OUSE_CLOCK_MIN_UTC-1) : cmd->spec.at_utc;
        if(!next || (clock_valid(now) && next<=now)){error=ESP_ERR_INVALID_ARG;goto done;}
        if(index==candidate->count)++candidate->count;
        candidate->records[index]=(ouse_alarm_snapshot_t){.spec=cmd->spec,.next_utc=next,.enabled=true};
        strcpy(candidate->records[index].app_id,cmd->app_id);
    }else if(index==db.count){error=ESP_ERR_NOT_FOUND;goto done;}
    else if(cmd->action==OUSE_ALARM_REMOVE){
        memmove(candidate->records+index,candidate->records+index+1,(--candidate->count-index)*sizeof(candidate->records[0]));
        memset(&candidate->records[candidate->count],0,sizeof(candidate->records[0]));
    }else if(cmd->action==OUSE_ALARM_SNOOZE){
        if(!clock_valid(now) || !ringing_until[index]){error=ESP_ERR_INVALID_STATE;goto done;}
        candidate->records[index].next_utc=now+(int64_t)cmd->snooze*60;candidate->records[index].enabled=true;
    }else if(cmd->action==OUSE_ALARM_DISMISS){stop_ringing(index);goto done;}
    error=save(candidate);if(error!=ESP_OK)goto done;
    // Side effects only after the atomic persistence commit. Keep the volatile
    // ring timers aligned with surviving records after removal.
    if(cmd->purge){for(unsigned i=0;i<db.count;)if(!strcmp(db.records[i].app_id,cmd->app_id))remove_at(i);else ++i;}
    else if(cmd->action==OUSE_ALARM_REMOVE)remove_at(index);
    else stop_ringing(index);
    db=*candidate;
done:
    free(candidate);return error;
}
static void tick(void)
{
    int64_t monotonic=esp_timer_get_time(),now=utc();
    if(clock_valid(now) && monotonic>=retry_at){
        // Commit the consumed occurrence before ringing. This chooses at-most-
        // once delivery across reboot; power loss in the commit/effect gap may
        // miss a ring, but cannot replay an already consumed occurrence.
        database_t *candidate=NULL;bool fire[OUSE_ALARM_CAPACITY]={0};
        for(unsigned i=0;i<db.count;++i){ouse_alarm_snapshot_t *a=&db.records[i];
            if(!permitted(a->app_id) || !a->enabled || a->next_utc>now)continue;
            if(!candidate){candidate=malloc(sizeof(*candidate));if(!candidate){last_error=ESP_ERR_NO_MEM;retry_at=monotonic+10000000;break;}*candidate=db;}
            ouse_alarm_snapshot_t *next=&candidate->records[i];
            fire[i]=now-a->next_utc<=GRACE_SECONDS && a->next_utc>a->last_utc;
            next->last_utc=a->next_utc>a->last_utc ? a->next_utc : a->last_utc;next->missed=!fire[i];
            next->next_utc=ouse_alarm_next(&a->spec,now>next->last_utc ? now : next->last_utc);next->enabled=next->next_utc!=0;
        }
        if(candidate){esp_err_t error=save(candidate);last_error=error;
            if(error==ESP_OK){db=*candidate;for(unsigned i=0;i<db.count;++i)if(fire[i]){ringing_until[i]=monotonic+RING_US;notification(&db.records[i],true);}}
            else retry_at=monotonic+10000000;
            free(candidate);
        }
    }
    bool active=false;
    for(unsigned i=0;i<db.count;++i){if(ringing_until[i] && (monotonic>=ringing_until[i] || !permitted(db.records[i].app_id)))stop_ringing(i);if(ringing_until[i])active=true;}
    ouse_task_snapshot_t tone;bool tone_active=ring_task && ouse_task_get(NULL,ring_task,&tone) && tone.state<OUSE_TASK_SUCCEEDED;
    if(active && (!any_ringing || (!tone_active && monotonic>=ring_retry_at))){
        const ouse_task_owner_t system={.app_id="ouse.alarm"};
        esp_err_t error=ouse_audio_submit(&system,OUSE_AUDIO_RING,60,&ring_task);ring_retry_at=monotonic+1000000;
        if(error!=ESP_OK)last_error=error;
    }else if(!active && any_ringing){(void)ouse_task_cancel(NULL,ring_task,"alarm_dismissed");ring_task=0;}
    any_ringing=active;
}
static void worker(void *unused)
{
    (void)unused;
    for(;;){command_t *cmd=NULL;
        if(xQueueReceive(commands,&cmd,pdMS_TO_TICKS(250))==pdTRUE){
            atomic_store(&drain_ack,false);
            bool started=cmd->purge || ouse_task_start(cmd->task,false);
            if(started && cmd->dismiss_all){for(unsigned i=0;i<db.count;++i)if(ringing_until[i])stop_ringing(i);cmd->result=ESP_OK;}
            else cmd->result=started ? execute(cmd) : ESP_OK;
            if(!cmd->purge){ouse_task_result_t summary={.present=true};strcpy(summary.value.alarm.id,cmd->spec.id);
                ouse_task_finish_result(cmd->task,cmd->result,!started,&summary);}
            last_error=cmd->result;
            if(cmd->barrier)xSemaphoreGive(cmd->barrier);else free(cmd);
        }
        bool draining=ouse_tasks_quiescing();
        if(draining){
            for(unsigned i=0;i<db.count;++i)if(ringing_until[i])stop_ringing(i);
            if(ring_task)(void)ouse_task_cancel(NULL,ring_task,"system_drain");
            ring_task=0;any_ringing=false;
        }else {atomic_store(&drain_ack,false);tick();}
        publish(last_error);atomic_store(&drain_ack,draining);
    }
}
esp_err_t ouse_alarm_start(const char *const *installed_ids,unsigned installed_count)
{
    if(commands)return ESP_ERR_INVALID_STATE;
    if(installed_count>8 || (installed_count && !installed_ids))return ESP_ERR_INVALID_ARG;
    for(unsigned i=0;i<installed_count;++i)if(!app_valid(installed_ids[i]))return ESP_ERR_INVALID_ARG;
    esp_err_t error=load();if(error!=ESP_OK){publish(error);return error;}
    // A temporarily un-restorable package (OOM, invalid file) must not lose its
    // preserved alarms. It is ineligible to trigger until restored/authorized.
    for(unsigned i=0;i<installed_count;++i)ouse_alarm_register_app(installed_ids[i],true);
    commands=xQueueCreate(OUSE_ALARM_QUEUE_MAX,sizeof(command_t *));if(!commands)return ESP_ERR_NO_MEM;
    publish(ESP_OK);
    if(xTaskCreate(worker,"ouse_alarm",4096,NULL,2,NULL)!=pdPASS){
        vQueueDelete(commands);commands=NULL;portENTER_CRITICAL(&cache_lock);stats.ready=false;portEXIT_CRITICAL(&cache_lock);return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&cache_lock);stats.ready=true;portEXIT_CRITICAL(&cache_lock);
    return ESP_OK;
}
esp_err_t ouse_alarm_submit(const ouse_task_owner_t *owner,ouse_alarm_action_t action,
    const ouse_alarm_spec_t *spec,unsigned snooze,uint32_t *id)
{
    if(!id)return ESP_ERR_INVALID_ARG;
    *id=0;
    if(!owner || !app_valid(owner->app_id) || !spec || (unsigned)action>OUSE_ALARM_SNOOZE ||
       !spec->id[0] || strnlen(spec->id,sizeof(spec->id))>=sizeof(spec->id) ||
       (action==OUSE_ALARM_SET && !ouse_alarm_spec_valid(spec)) ||
       (action==OUSE_ALARM_SNOOZE && (snooze<1 || snooze>60)))return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&cache_lock);bool ready=stats.ready;portEXIT_CRITICAL(&cache_lock);
    if(!ready || !permitted(owner->app_id))return ESP_ERR_INVALID_STATE;
    command_t *cmd=calloc(1,sizeof(*cmd));if(!cmd)return ESP_ERR_NO_MEM;
    strcpy(cmd->app_id,owner->app_id);cmd->spec=*spec;cmd->action=action;cmd->snooze=snooze;
    static const char *names[]={"set","remove","dismiss","snooze"};
    esp_err_t error=ouse_task_create(owner,OUSE_TASK_ALARM,names[action],OUSE_TASK_FINISH_ON_CLOSE,true,id);
    if(error!=ESP_OK){free(cmd);return error;}
    cmd->task=*id;
    if(xQueueSend(commands,&cmd,0)==pdTRUE)return ESP_OK;
    ouse_task_finish(*id,ESP_ERR_NO_MEM,false);free(cmd);return ESP_ERR_NO_MEM;
}
unsigned ouse_alarm_list(const char *app_id,ouse_alarm_snapshot_t *out,unsigned capacity)
{
    if(!out)return 0;
    unsigned count=0;portENTER_CRITICAL(&cache_lock);
    for(unsigned i=0;i<cache.count && count<capacity;++i)if(!app_id || !strcmp(app_id,cache.records[i].app_id))out[count++]=cache.records[i];
    portEXIT_CRITICAL(&cache_lock);return count;
}
void ouse_alarm_get_stats(ouse_alarm_stats_t *out)
{if(out){portENTER_CRITICAL(&cache_lock);*out=stats;portEXIT_CRITICAL(&cache_lock);}}
esp_err_t ouse_alarm_purge(const char *app_id)
{
    if(!app_valid(app_id))return ESP_ERR_INVALID_ARG;
    if(!commands)return ESP_ERR_INVALID_STATE;
    command_t *cmd=calloc(1,sizeof(*cmd));if(!cmd)return ESP_ERR_NO_MEM;
    cmd->barrier=xSemaphoreCreateBinary();if(!cmd->barrier){free(cmd);return ESP_ERR_NO_MEM;}
    strcpy(cmd->app_id,app_id);cmd->purge=true;
    xQueueSend(commands,&cmd,portMAX_DELAY);xSemaphoreTake(cmd->barrier,portMAX_DELAY);
    esp_err_t error=cmd->result;vSemaphoreDelete(cmd->barrier);free(cmd);return error;
}
esp_err_t ouse_alarm_dismiss_all(void)
{
    if(!commands)return ESP_ERR_INVALID_STATE;
    command_t *cmd=calloc(1,sizeof(*cmd));if(!cmd)return ESP_ERR_NO_MEM;
    const ouse_task_owner_t owner={.app_id="ouse.alarm"};
    esp_err_t error=ouse_task_create(&owner,OUSE_TASK_ALARM,"dismissAll",OUSE_TASK_FINISH_ON_CLOSE,true,&cmd->task);
    if(error!=ESP_OK){free(cmd);return error;}
    cmd->dismiss_all=true;
    if(xQueueSend(commands,&cmd,0)==pdTRUE)return ESP_OK;
    ouse_task_finish(cmd->task,ESP_ERR_NO_MEM,false);free(cmd);return ESP_ERR_NO_MEM;
}
