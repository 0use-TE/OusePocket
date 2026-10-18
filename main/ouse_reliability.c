// SPDX-License-Identifier: MIT
// Native coordinator. No hardware I/O, JS execution or filesystem work on UI.
#include "ouse_core.h"
#include "ouse_health.h"
#include "ouse_device_driver.h"
#include "ouse_runtime.h"
#include "ouse_storage.h"
#include "ouse_installer.h"
#include "ouse_net.h"
#include "ouse_media.h"
#include "ouse_camera.h"
#include "ouse_recorder.h"
#include "ouse_alarm.h"
#include "ouse_usb.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
static atomic_bool ui_done=true;
static char closing_app[48]; // owned until UI completion; never stack context
static bool close_all;
static void close_ui(void *unused)
{
    (void)unused;
    if(close_all){
        for(unsigned i=0;i<OUSE_RECENTS_CAPACITY;++i){ouse_app_info_t app;
            if(ouse_app_get_info(i,&app)==ESP_OK && strcmp(app.id,"ouse.settings"))(void)ouse_app_close_id(app.id);
        }
    }else if(closing_app[0])(void)ouse_app_close_id(closing_app);
    atomic_store(&ui_done,true);
}
static esp_err_t close_request(const char *id)
{
    if(!atomic_load(&ui_done))return ESP_ERR_INVALID_STATE;
    closing_app[0]=0;close_all=!id;if(id)snprintf(closing_app,sizeof(closing_app),"%s",id);
    atomic_store(&ui_done,false);esp_err_t error=ouse_core_post_ui(close_ui,NULL);
    if(error!=ESP_OK)atomic_store(&ui_done,true);
    return error;
}
static void service(ouse_health_snapshot_t *h,const char *name,bool ready,esp_err_t error)
{
    ouse_service_health_t *s=&h->services[h->service_count++];snprintf(s->name,sizeof(s->name),"%s",name);
    s->status=!ready ? OUSE_SERVICE_UNAVAILABLE : error==ESP_OK ? OUSE_SERVICE_READY : OUSE_SERVICE_DEGRADED;s->error=error;
}
static void sample(ouse_health_snapshot_t *h)
{
    memset(h,0,sizeof(*h));h->available=true;h->sampled_ms=esp_timer_get_time()/1000;h->quiescing=ouse_tasks_quiescing();
    ouse_wifi_state_t wifi;ouse_audio_state_t audio;ouse_storage_stats_t storage;ouse_alarm_stats_t alarms;
    ouse_settings_state_t settings;ouse_installer_state_t installer;
    ouse_wifi_get(&wifi);ouse_audio_get(&audio);ouse_storage_get_stats(&storage);ouse_alarm_get_stats(&alarms);
    ouse_system_get_settings_state(&settings);ouse_installer_get_state(&installer);
    service(h,"wifi",wifi.available,wifi.error);
    if(wifi.recovering)h->services[0].status=OUSE_SERVICE_RECOVERING;
    else if(wifi.recovery_failed)h->services[0].status=OUSE_SERVICE_UNAVAILABLE;
    service(h,"audio",audio.available,audio.error);service(h,"storage",storage.ready,storage.last_error);
    service(h,"settings",true,settings.last_error);service(h,"network",ouse_net_available(),ESP_OK);
    service(h,"alarm",alarms.ready,alarms.error);service(h,"installer",installer.ready,installer.error);
    ouse_usb_state_t usb;ouse_usb_get(&usb);
    service(h,"usb",usb.available,usb.error!=ESP_OK ? usb.error : usb.network_error);
    static const unsigned mapping[]={0,1,2,3,6,4,5};
    uint32_t after=0;ouse_task_snapshot_t task;
    while(ouse_task_next(NULL,after,&task,false)){
        after=task.id;if(task.state>=OUSE_TASK_SUCCEEDED)continue;
        unsigned index=(unsigned)task.kind;if(index<7){ouse_service_health_t *s=&h->services[mapping[index]];
            ++s->active;uint64_t created=(uint64_t)task.created_us/1000;
            uint64_t age=h->sampled_ms>=created ? h->sampled_ms-created : 0;if(age>s->oldest_ms)s->oldest_ms=age;}
        // All outstanding native work is visible, including noncancellable
        // commits. It is not automatically a background JS permission.
        ++h->activities_total;if(h->activity_count>=OUSE_HEALTH_ACTIVITIES)continue;
        ouse_activity_t *a=&h->activities[h->activity_count++];strcpy(a->app_id,task.owner.app_id);
        snprintf(a->reason,sizeof(a->reason),"%.15s:%.23s",ouse_task_kind_name(task.kind),task.operation);
        a->task_id=task.id;a->instance_id=task.owner.instance;a->cancellable=task.cancellable;a->stopping=task.cancel_requested;
    }
    ouse_runtime_diagnostics_t runtime;ouse_runtime_get_diagnostics(&runtime);
    for(unsigned i=0;i<runtime.app_count;++i){ouse_runtime_app_stats_t *app=&runtime.apps[i];
        if(!app->background_active)continue;
        ++h->activities_total;if(h->activity_count>=OUSE_HEALTH_ACTIVITIES)continue;
        ouse_activity_t *a=&h->activities[h->activity_count++];strcpy(a->app_id,app->app_id);
        strcpy(a->reason,app->background_reason);a->javascript=true;a->cancellable=true;a->instance_id=app->instance_id;
    }
}
static const char *drain_blocker(int64_t since_us)
{
    if(!atomic_load(&ui_done))return "ui-close";
    ouse_installer_state_t installer;ouse_installer_get_state(&installer);
    if(installer.busy)return "installer-in-flight";
    if(installer.recovery_pending)return "uninstall-recovery-pending";
    ouse_runtime_stats_t runtime;ouse_runtime_get_stats(&runtime);if(runtime.contexts)return "javascript-cleanup";
    ouse_task_stats_t tasks;ouse_tasks_get_stats(&tasks);if(tasks.submitting)return "request-submission";if(tasks.active)return "native-tasks";
    // Includes pending launches/compile-only validation, which may not yet
    // appear in the worker's cached live-context count.
    if(tasks.owners)return "javascript-instances";
    if(!ouse_storage_idle())return "storage-maintenance";
    if(!ouse_camera_idle())return "camera-worker";
    if(!ouse_recorder_idle())return "recorder-worker";
    if(!ouse_alarm_quiesced())return "alarm-worker";
    ouse_usb_state_t usb;ouse_usb_get(&usb);
    if(usb.available && (usb.enabled || usb.audio_running || usb.network_up || usb.network_stopping))return "usb-stop-confirmation";
    ouse_wifi_state_t wifi;ouse_wifi_get(&wifi);if(wifi.available && !wifi.stopped)return "wifi-stop-confirmation";
    ouse_settings_state_t settings;ouse_system_get_settings_state(&settings);
    if(settings.pending)return "settings-write";
    if(settings.last_error!=ESP_OK)return "settings-write-failed";
    ouse_device_save_state_t save;ouse_devices_save_state(&save);
    if(save.pending)return "device-settings-write";
    if(save.error!=ESP_OK)return "device-settings-write-failed";
    uint32_t after=0;ouse_task_snapshot_t task;
    while(ouse_task_next(NULL,after,&task,false)){
        after=task.id;
        if(task.state!=OUSE_TASK_FAILED || task.finished_us<since_us)continue;
        if(task.kind==OUSE_TASK_ALARM)return "alarm-commit-failed";
        if(task.kind==OUSE_TASK_STORAGE && (!strcmp(task.operation,"set") || !strcmp(task.operation,"remove") || !strcmp(task.operation,"clear")))return "storage-write-failed";
    }
    return NULL;
}
static void cancel_transient_work(void)
{
    uint32_t after=0;ouse_task_snapshot_t task;
    while(ouse_task_next(NULL,after,&task,false)){
        after=task.id;
        // Already accepted storage/alarm/settings/package commits must drain,
        // even while queued. They must not disappear just because the UI closed.
        if(task.kind==OUSE_TASK_AUDIO || task.kind==OUSE_TASK_NETWORK || task.kind==OUSE_TASK_WIFI)
            (void)ouse_task_cancel(NULL,task.id,"system_restart");
    }
}
static void worker(void *unused)
{
    (void)unused;bool draining=false,blocked=false;int64_t deadline=0,settled=0,next_memory=0;
    char blocker[64]="";
    for(;;){ouse_control_t control;
        if(ouse_control_take(&control)){
            if(control.action==OUSE_CONTROL_RESTART && !draining && atomic_load(&ui_done)){
                ouse_tasks_quiesce(true);cancel_transient_work();ouse_devices_wake_workers();
                esp_err_t error=close_request(NULL);draining=error==ESP_OK;blocked=!draining;
                snprintf(blocker,sizeof(blocker),"%s",error==ESP_OK ? "" : "ui-dispatch-failed");
                deadline=esp_timer_get_time()+15000000;settled=0;
            }else if(control.action==OUSE_CONTROL_RESUME && blocked && atomic_load(&ui_done)){
                ouse_tasks_quiesce(false);blocked=false;blocker[0]=0;ouse_devices_wake_workers();
            }else if(control.action==OUSE_CONTROL_STOP && !ouse_tasks_quiescing()){
                if(control.task_id){
                    ouse_task_snapshot_t task;
                    if(ouse_task_get(NULL,control.task_id,&task) && !strcmp(task.owner.app_id,control.app_id)){
                        if(!strcmp(task.owner.app_id,"ouse.alarm") && task.kind==OUSE_TASK_AUDIO)(void)ouse_alarm_dismiss_all();
                        else (void)ouse_activity_stop_owned(control.app_id,control.task_id);
                    }
                }else if(control.instance_id)(void)ouse_runtime_end_background(control.instance_id);
            }
        }
        if(draining){
            const char *reason=drain_blocker(deadline-15000000);int64_t now=esp_timer_get_time();
            snprintf(blocker,sizeof(blocker),"%s",reason ? reason : "");
            if(reason)settled=0;else if(!settled)settled=now;
            if(settled && now-settled>=500000)esp_restart();
            if(now>=deadline){draining=false;blocked=true;if(!blocker[0])strcpy(blocker,"settling");}
        }
        if(esp_timer_get_time()>=next_memory){ouse_system_sample_memory();next_memory=esp_timer_get_time()+1000000;}
        ouse_health_snapshot_t health;sample(&health);
        strcpy(health.phase,draining ? "draining" : blocked ? "blocked" : "running");strcpy(health.blocker,blocker);
        ouse_health_publish(&health);vTaskDelay(pdMS_TO_TICKS(500));
    }
}
esp_err_t ouse_reliability_start(void)
{
    if(xTaskCreate(worker,"ouse_health",12288,NULL,2,NULL)!=pdPASS)return ESP_ERR_NO_MEM;
    ouse_health_enable();return ESP_OK;
}
