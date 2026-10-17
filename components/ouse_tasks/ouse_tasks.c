// SPDX-License-Identifier: MIT
// Bounded metadata only: no JS, LVGL, hardware I/O or task-owned result buffers.
#include "ouse_tasks.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <limits.h>
#include <string.h>

static portMUX_TYPE registry_lock=portMUX_INITIALIZER_UNLOCKED;
static struct {
    ouse_task_snapshot_t records[OUSE_TASK_CAPACITY];
    ouse_task_owner_t owners[OUSE_TASK_OWNERS_MAX];
    uint32_t next_id,next_instance,revision,rejected,recycled;
    void (*notify)(void);
    bool quiescing;
    unsigned submitting;
} registry;
static bool terminal(ouse_task_state_t state) {return state>=OUSE_TASK_SUCCEEDED;}
static bool same(const ouse_task_owner_t *a,const ouse_task_owner_t *b)
{return a->instance==b->instance && !strcmp(a->app_id,b->app_id);}
static bool alive(const ouse_task_owner_t *owner)
{
    if(!owner || !owner->app_id[0])return false;
    if(!owner->instance)return true;
    for(unsigned i=0;i<OUSE_TASK_OWNERS_MAX;++i)if(same(owner,&registry.owners[i]))return true;
    return false;
}
static ouse_task_snapshot_t *find(uint32_t id)
{
    if(id)for(unsigned i=0;i<OUSE_TASK_CAPACITY;++i)if(registry.records[i].id==id)return &registry.records[i];
    return NULL;
}
static void changed(ouse_task_snapshot_t *task) {task->revision=++registry.revision;}
static void finish(ouse_task_snapshot_t *task,esp_err_t error,bool cancelled)
{
    if(terminal(task->state))return;
    task->state=cancelled ? OUSE_TASK_CANCELLED : error==ESP_OK ? OUSE_TASK_SUCCEEDED : OUSE_TASK_FAILED;
    task->error=error;task->finished_us=esp_timer_get_time();task->cancellable=false;
    task->delivery_pending=task->owner.instance && !task->detached;changed(task);
}
static void cancel(ouse_task_snapshot_t *task,const char *reason)
{
    if(task->cancel_requested || terminal(task->state))return;
    task->cancel_requested=true;
    if(task->state==OUSE_TASK_RUNNING)task->state=OUSE_TASK_CANCELLING;
    size_t n=strnlen(reason,sizeof(task->cancel_reason)-1);memcpy(task->cancel_reason,reason,n);task->cancel_reason[n]=0;changed(task);
}
esp_err_t ouse_task_owner_open(const char *app_id,ouse_task_owner_t *out)
{
    if(!app_id || !out || !*app_id || strnlen(app_id,48)>=48)return ESP_ERR_INVALID_ARG;
    esp_err_t result=ESP_ERR_NO_MEM;
    portENTER_CRITICAL(&registry_lock);
    if(registry.quiescing){portEXIT_CRITICAL(&registry_lock);return ESP_ERR_INVALID_STATE;}
    if(registry.next_instance!=UINT32_MAX)for(unsigned i=0;i<OUSE_TASK_OWNERS_MAX;++i)if(!registry.owners[i].instance){
        strcpy(registry.owners[i].app_id,app_id);registry.owners[i].instance=++registry.next_instance;
        *out=registry.owners[i];++registry.revision;result=ESP_OK;break;
    }
    portEXIT_CRITICAL(&registry_lock);return result;
}
void ouse_task_owner_close(const ouse_task_owner_t *owner)
{
    if(!owner || !owner->instance)return;
    portENTER_CRITICAL(&registry_lock);
    for(unsigned i=0;i<OUSE_TASK_OWNERS_MAX;++i)if(same(owner,&registry.owners[i]))memset(&registry.owners[i],0,sizeof(registry.owners[i]));
    for(unsigned i=0;i<OUSE_TASK_CAPACITY;++i){ouse_task_snapshot_t *task=&registry.records[i];
        if(!task->id || !same(owner,&task->owner))continue;
        task->detached=true;task->delivery_pending=false;task->result_held=false;
        if(task->policy==OUSE_TASK_STOP_ON_CLOSE && task->cancellable)cancel(task,"owner_closed");
        changed(task);
    }
    portEXIT_CRITICAL(&registry_lock);
}
esp_err_t ouse_task_create(const ouse_task_owner_t *owner,ouse_task_kind_t kind,
    const char *operation,ouse_task_policy_t policy,bool cancellable,uint32_t *id)
{
    if((unsigned)kind>OUSE_TASK_ALARM || (unsigned)policy>OUSE_TASK_STOP_ON_CLOSE || !owner || !operation || !id || strnlen(owner->app_id,48)>=48 || !*operation || strnlen(operation,24)>=24)return ESP_ERR_INVALID_ARG;
    esp_err_t result=ESP_ERR_NO_MEM;unsigned active=0,owned=0;
    ouse_task_snapshot_t *slot=NULL,*oldest=NULL;
    portENTER_CRITICAL(&registry_lock);
    if(!alive(owner) || (registry.quiescing && (owner->instance || kind!=OUSE_TASK_SETTINGS))){result=ESP_ERR_INVALID_STATE;goto done;}
    for(unsigned i=0;i<OUSE_TASK_CAPACITY;++i){ouse_task_snapshot_t *task=&registry.records[i];
        if(!task->id){if(!slot)slot=task;continue;}
        if(!terminal(task->state)){++active;if(same(owner,&task->owner))++owned;}
        else if(task->delivery_pending || task->result_held){if(same(owner,&task->owner))++owned;}
        else if(!oldest || task->id<oldest->id)oldest=task;
    }
    if(active>=OUSE_TASK_ACTIVE_LIMIT || (owner->instance && owned>=OUSE_TASK_OWNER_LIMIT) || registry.next_id==UINT32_MAX)goto done;
    if(!slot){slot=oldest;if(!slot)goto done;++registry.recycled;}
    memset(slot,0,sizeof(*slot));slot->id=++registry.next_id;slot->owner=*owner;slot->kind=kind;
    slot->state=OUSE_TASK_QUEUED;slot->policy=policy;slot->cancellable=cancellable;
    strcpy(slot->operation,operation);slot->created_us=esp_timer_get_time();changed(slot);*id=slot->id;result=ESP_OK;
done:
    if(result!=ESP_OK)++registry.rejected;
    portEXIT_CRITICAL(&registry_lock);return result;
}
bool ouse_task_start(uint32_t id,bool cancellable_while_running)
{
    bool started=false;portENTER_CRITICAL(&registry_lock);ouse_task_snapshot_t *task=find(id);
    if(task && task->state==OUSE_TASK_QUEUED){
        if(task->cancel_requested)finish(task,ESP_OK,true);
        else {task->state=OUSE_TASK_RUNNING;task->started_us=esp_timer_get_time();task->cancellable=cancellable_while_running;changed(task);started=true;}
    }
    portEXIT_CRITICAL(&registry_lock);return started;
}
bool ouse_task_cancel_requested(uint32_t id)
{
    portENTER_CRITICAL(&registry_lock);ouse_task_snapshot_t *task=find(id);bool result=!task || task->cancel_requested;
    portEXIT_CRITICAL(&registry_lock);return result;
}
esp_err_t ouse_task_cancel(const ouse_task_owner_t *owner,uint32_t id,const char *reason)
{
    esp_err_t result=ESP_ERR_NOT_FOUND;portENTER_CRITICAL(&registry_lock);ouse_task_snapshot_t *task=find(id);
    if(task && (!owner || same(owner,&task->owner))){
        if(terminal(task->state))result=ESP_OK;
        else if(!task->cancellable)result=ESP_ERR_NOT_SUPPORTED;
        else {cancel(task,reason ? reason : "requested");result=ESP_OK;}
    }
    portEXIT_CRITICAL(&registry_lock);return result;
}
void ouse_task_finish(uint32_t id,esp_err_t error,bool cancelled)
{ouse_task_finish_result(id,error,cancelled,NULL);}
void ouse_task_finish_result(uint32_t id,esp_err_t error,bool cancelled,const ouse_task_result_t *result)
{
    portENTER_CRITICAL(&registry_lock);ouse_task_snapshot_t *task=find(id);
    if(task && !terminal(task->state)){
        if(result){task->result=*result;
            if(task->kind==OUSE_TASK_WIFI){task->result.value.wifi.ssid[32]=0;task->result.value.wifi.ip[15]=0;}
            else if(task->kind==OUSE_TASK_ALARM)task->result.value.alarm.id[31]=0;
        }
        finish(task,error,cancelled);
    }
    void (*notify)(void)=registry.notify;portEXIT_CRITICAL(&registry_lock);if(notify)notify();
}
bool ouse_task_get(const ouse_task_owner_t *owner,uint32_t id,ouse_task_snapshot_t *out)
{
    if(!out)return false;
    portENTER_CRITICAL(&registry_lock);ouse_task_snapshot_t *task=find(id);bool found=task && (!owner || same(owner,&task->owner));
    if(found) *out=*task;
    portEXIT_CRITICAL(&registry_lock);
    return found;
}
unsigned ouse_task_list(const ouse_task_owner_t *owner,ouse_task_snapshot_t *out,unsigned capacity,bool pending_only)
{
    if(!out)return 0;
    unsigned count=0;portENTER_CRITICAL(&registry_lock);
    for(unsigned i=0;i<OUSE_TASK_CAPACITY && count<capacity;++i){ouse_task_snapshot_t *task=&registry.records[i];
        if(task->id && (!owner || same(owner,&task->owner)) && (!pending_only || task->delivery_pending))out[count++]=*task;
    }
    portEXIT_CRITICAL(&registry_lock);return count;
}
void ouse_task_ack(const ouse_task_owner_t *owner,uint32_t id)
{
    portENTER_CRITICAL(&registry_lock);ouse_task_snapshot_t *task=find(id);
    if(task && owner && same(owner,&task->owner)){task->delivery_pending=false;changed(task);}
    portEXIT_CRITICAL(&registry_lock);
}
void ouse_task_hold_result(uint32_t id,bool held)
{
    portENTER_CRITICAL(&registry_lock);ouse_task_snapshot_t *task=find(id);
    if(task && (!held || !task->detached)){task->result_held=held;changed(task);}
    portEXIT_CRITICAL(&registry_lock);
}
bool ouse_task_next(const ouse_task_owner_t *owner,uint32_t after_id,ouse_task_snapshot_t *out,bool pending_only)
{
    if(!out)return false;
    ouse_task_snapshot_t *next=NULL;portENTER_CRITICAL(&registry_lock);
    for(unsigned i=0;i<OUSE_TASK_CAPACITY;++i){ouse_task_snapshot_t *task=&registry.records[i];
        if(task->id>after_id && (!owner || same(owner,&task->owner)) && (!pending_only || task->delivery_pending) && (!next || task->id<next->id))next=task;
    }
    if(next) *out=*next;
    portEXIT_CRITICAL(&registry_lock);
    return next!=NULL;
}
void ouse_tasks_get_stats(ouse_task_stats_t *out)
{
    if(!out)return;
    portENTER_CRITICAL(&registry_lock);*out=(ouse_task_stats_t){.rejected=registry.rejected,.recycled=registry.recycled,
        .revision=registry.revision,.registry_bytes=sizeof(registry),.submitting=registry.submitting};
    for(unsigned i=0;i<OUSE_TASK_CAPACITY;++i)if(registry.records[i].id){
        ++out->records;
        if(!terminal(registry.records[i].state)) ++out->active;
        if(registry.records[i].delivery_pending) ++out->pending_delivery;
        if(registry.records[i].result_held) ++out->pending_results;
    }
    for(unsigned i=0;i<OUSE_TASK_OWNERS_MAX;++i)if(registry.owners[i].instance) ++out->owners;
    portEXIT_CRITICAL(&registry_lock);
}
void ouse_tasks_set_notify(void (*notify)(void))
{portENTER_CRITICAL(&registry_lock);registry.notify=notify;portEXIT_CRITICAL(&registry_lock);}
const char *ouse_task_state_name(ouse_task_state_t state)
{static const char *names[]={"queued","running","cancelling","succeeded","failed","cancelled"};return (unsigned)state<6 ? names[state] : "unknown";}
const char *ouse_task_kind_name(ouse_task_kind_t kind)
{static const char *names[]={"wifi","audio","storage","settings","package","network","alarm"};return (unsigned)kind<7 ? names[kind] : "unknown";}
void ouse_tasks_quiesce(bool enabled)
{portENTER_CRITICAL(&registry_lock);registry.quiescing=enabled;portEXIT_CRITICAL(&registry_lock);}
bool ouse_tasks_quiescing(void)
{portENTER_CRITICAL(&registry_lock);bool value=registry.quiescing;portEXIT_CRITICAL(&registry_lock);return value;}
bool ouse_tasks_submission_begin(void)
{portENTER_CRITICAL(&registry_lock);bool accepted=!registry.quiescing;if(accepted)++registry.submitting;portEXIT_CRITICAL(&registry_lock);return accepted;}
void ouse_tasks_submission_end(void)
{portENTER_CRITICAL(&registry_lock);if(registry.submitting)--registry.submitting;portEXIT_CRITICAL(&registry_lock);}
void ouse_tasks_cancel_active(const char *app_id,const char *reason)
{
    portENTER_CRITICAL(&registry_lock);
    for(unsigned i=0;i<OUSE_TASK_CAPACITY;++i){ouse_task_snapshot_t *task=&registry.records[i];
        if(task->id && task->cancellable && !terminal(task->state) && (!app_id || !strcmp(task->owner.app_id,app_id)))
            cancel(task,reason ? reason : "system_stop");
    }
    void (*notify)(void)=registry.notify;portEXIT_CRITICAL(&registry_lock);if(notify)notify();
}
