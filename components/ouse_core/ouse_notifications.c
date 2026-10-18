// SPDX-License-Identifier: MIT
#include "ouse_notifications.h"
#include <string.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define OPEN_CAPACITY 8
#define OPEN_APP_CAPACITY 4
#define OPEN_TTL_MS (5ULL * 60 * 1000)
typedef struct {
    char app_id[OUSE_NOTIFICATION_APP_ID_SIZE];
    ouse_notification_open_t value;
    uint64_t created_ms;
} open_entry_t;
static struct {
    SemaphoreHandle_t lock;
    StaticSemaphore_t lock_storage;
    ouse_notification_t records[OUSE_NOTIFICATION_CAPACITY];
    open_entry_t opens[OPEN_CAPACITY];
    unsigned count,open_count;
    uint32_t serial,open_serial,generation;
} notifications;

static bool bounded(const char *value,unsigned capacity,bool required)
{ return value && (!required || *value) && strnlen(value,capacity)<capacity; }
static bool app_valid(const char *id)
{
    if(!bounded(id,OUSE_NOTIFICATION_APP_ID_SIZE,true))return false;
    for(const char *p=id;*p;++p)
        if(!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='.'||*p=='_'||*p=='-'))return false;
    return true;
}
static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time()/1000; }
static bool lock(void)
{ return notifications.lock && xSemaphoreTake(notifications.lock,portMAX_DELAY)==pdTRUE; }
static void unlock(void) { xSemaphoreGive(notifications.lock); }
static void remove_open_at(unsigned index)
{
    memmove(&notifications.opens[index],&notifications.opens[index+1],
            (--notifications.open_count-index)*sizeof(notifications.opens[0]));
    memset(&notifications.opens[notifications.open_count],0,sizeof(notifications.opens[0]));
}
static void remove_opens(const char *app_id,const char *id)
{
    for(unsigned i=0;i<notifications.open_count;)
        if(!strcmp(notifications.opens[i].app_id,app_id) && (!id||!strcmp(notifications.opens[i].value.id,id)))remove_open_at(i);
        else ++i;
}
static void expire_opens(void)
{
    uint64_t now=now_ms();
    for(unsigned i=0;i<notifications.open_count;)
        if(now-notifications.opens[i].created_ms>=OPEN_TTL_MS)remove_open_at(i);
        else ++i;
}
static void remove_at(unsigned index)
{
    remove_opens(notifications.records[index].app_id,notifications.records[index].id);
    memmove(&notifications.records[index],&notifications.records[index+1],
            (--notifications.count-index)*sizeof(notifications.records[0]));
    memset(&notifications.records[notifications.count],0,sizeof(notifications.records[0]));
    ++notifications.generation;
}
esp_err_t ouse_notifications_init(void)
{
    if(notifications.lock)return ESP_OK;
    notifications.lock=xSemaphoreCreateMutexStatic(&notifications.lock_storage);
    return notifications.lock ? ESP_OK : ESP_ERR_NO_MEM;
}
esp_err_t ouse_notifications_publish(const char *app_id,const ouse_notification_request_t *request)
{
    if(!app_valid(app_id)||!request||!bounded(request->id,OUSE_NOTIFICATION_ID_SIZE,true)||
       !bounded(request->title,OUSE_NOTIFICATION_TITLE_SIZE,true)||
       (request->body&&!bounded(request->body,OUSE_NOTIFICATION_BODY_SIZE,false))||
       (request->payload&&!bounded(request->payload,OUSE_NOTIFICATION_PAYLOAD_SIZE,false)))return ESP_ERR_INVALID_ARG;
    if(!lock())return ESP_ERR_INVALID_STATE;
    unsigned index=notifications.count,owned=0;
    for(unsigned i=0;i<notifications.count;++i)if(!strcmp(app_id,notifications.records[i].app_id)) {
        ++owned;if(!strcmp(request->id,notifications.records[i].id))index=i;
    }
    if(index==notifications.count) {
        if(notifications.count==OUSE_NOTIFICATION_CAPACITY||owned==OUSE_NOTIFICATION_APP_CAPACITY) {unlock();return ESP_ERR_NO_MEM;}
        ++notifications.count;
        if(!++notifications.serial)++notifications.serial;
        notifications.records[index].serial=notifications.serial;
        notifications.records[index].created_ms=now_ms();
        strcpy(notifications.records[index].app_id,app_id);
        strcpy(notifications.records[index].id,request->id);
    }
    ouse_notification_t *record=&notifications.records[index];
    strcpy(record->title,request->title);strcpy(record->body,request->body ? request->body : "");
    strcpy(record->payload,request->payload ? request->payload : "");
    record->ongoing=request->ongoing;record->updated_ms=now_ms();++notifications.generation;
    unlock();return ESP_OK;
}
esp_err_t ouse_notifications_cancel(const char *app_id,const char *id)
{
    if(!app_valid(app_id)||!bounded(id,OUSE_NOTIFICATION_ID_SIZE,true))return ESP_ERR_INVALID_ARG;
    if(!lock())return ESP_ERR_INVALID_STATE;
    for(unsigned i=0;i<notifications.count;++i)if(!strcmp(notifications.records[i].app_id,app_id)&&!strcmp(notifications.records[i].id,id)) {
        remove_at(i);unlock();return ESP_OK;
    }
    unlock();return ESP_ERR_NOT_FOUND;
}
unsigned ouse_notifications_list(const char *app_id,ouse_notification_t *out,unsigned capacity)
{
    if(!out||!capacity||!lock())return 0;
    // Small bounded insertion into the caller's array avoids a second large
    // snapshot on the UI or script worker stack.
    unsigned count=0;
    for(unsigned i=0;i<notifications.count;++i) {
        const ouse_notification_t *record=&notifications.records[i];
        if(app_id&&strcmp(record->app_id,app_id))continue;
        unsigned at=count;
        while(at && (out[at-1].updated_ms<record->updated_ms ||
              (out[at-1].updated_ms==record->updated_ms&&out[at-1].serial<record->serial)))--at;
        if(at>=capacity)continue;
        unsigned last=count<capacity ? count : capacity-1;
        while(last>at) {out[last]=out[last-1];--last;}
        out[at]=*record;if(count<capacity)++count;
    }
    unlock();return count;
}
unsigned ouse_notifications_count(const char *app_id)
{
    if(!lock())return 0;
    unsigned count=0;
    for(unsigned i=0;i<notifications.count;++i)if(!app_id||!strcmp(app_id,notifications.records[i].app_id))++count;
    unlock();return count;
}
uint32_t ouse_notifications_generation(void)
{ if(!lock())return 0;uint32_t value=notifications.generation;unlock();return value; }
bool ouse_notifications_latest(ouse_notification_t *out)
{
    if(!out || !lock())return false;
    const ouse_notification_t *latest=NULL;
    for(unsigned index=0;index<notifications.count;++index)if(!latest || notifications.records[index].serial>latest->serial)latest=&notifications.records[index];
    if(latest)*out=*latest;
    unlock();return latest!=NULL;
}
esp_err_t ouse_notifications_dismiss(uint32_t serial)
{
    if(!lock())return ESP_ERR_INVALID_STATE;
    for(unsigned i=0;i<notifications.count;++i)if(notifications.records[i].serial==serial) {
        if(notifications.records[i].ongoing) {unlock();return ESP_ERR_INVALID_STATE;}
        remove_at(i);unlock();return ESP_OK;
    }
    unlock();return ESP_ERR_NOT_FOUND;
}
unsigned ouse_notifications_clear_all(void)
{
    if(!lock())return 0;
    unsigned removed=0;
    for(unsigned i=0;i<notifications.count;)if(!notifications.records[i].ongoing){remove_at(i);++removed;}else ++i;
    unlock();return removed;
}
void ouse_notifications_remove_app(const char *app_id)
{
    if(!app_id||!lock())return;
    for(unsigned i=0;i<notifications.count;)if(!strcmp(notifications.records[i].app_id,app_id))remove_at(i);else ++i;
    remove_opens(app_id,NULL);unlock();
}
esp_err_t ouse_notifications_activate(uint32_t serial,char *app_id,unsigned app_id_size,uint32_t *token)
{
    if(!app_id||app_id_size<OUSE_NOTIFICATION_APP_ID_SIZE||!token)return ESP_ERR_INVALID_ARG;
    if(!lock())return ESP_ERR_INVALID_STATE;
    expire_opens();
    const ouse_notification_t *record=NULL;
    for(unsigned i=0;i<notifications.count;++i)if(notifications.records[i].serial==serial){record=&notifications.records[i];break;}
    if(!record){unlock();return ESP_ERR_NOT_FOUND;}
    unsigned owned=0;
    for(unsigned i=0;i<notifications.open_count;++i)if(!strcmp(notifications.opens[i].app_id,record->app_id))++owned;
    if(notifications.open_count==OPEN_CAPACITY||owned==OPEN_APP_CAPACITY){unlock();return ESP_ERR_NO_MEM;}
    open_entry_t *entry=&notifications.opens[notifications.open_count++];
    if(!++notifications.open_serial)++notifications.open_serial;
    entry->value.token=notifications.open_serial;strcpy(entry->app_id,record->app_id);
    strcpy(entry->value.id,record->id);strcpy(entry->value.payload,record->payload);entry->created_ms=now_ms();
    strcpy(app_id,record->app_id);*token=entry->value.token;unlock();return ESP_OK;
}
bool ouse_notifications_peek_open(const char *app_id,ouse_notification_open_t *out)
{
    if(!app_id||!out||!lock())return false;
    expire_opens();
    for(unsigned i=0;i<notifications.open_count;++i)if(!strcmp(notifications.opens[i].app_id,app_id)) {
        *out=notifications.opens[i].value;unlock();return true;
    }
    unlock();return false;
}
bool ouse_notifications_ack_open(const char *app_id,uint32_t token)
{
    if(!app_id||!token||!lock())return false;
    for(unsigned i=0;i<notifications.open_count;++i)if(notifications.opens[i].value.token==token&&!strcmp(notifications.opens[i].app_id,app_id)) {
        remove_open_at(i);unlock();return true;
    }
    unlock();return false;
}
void ouse_notifications_discard_open(const char *app_id,uint32_t token)
{ (void)ouse_notifications_ack_open(app_id,token); }
