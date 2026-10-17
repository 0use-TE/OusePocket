// SPDX-License-Identifier: MIT
#include "ouse_device_driver.h"
#include <string.h>
static struct {
    bool initialized,audio_pending,wifi_pending,save_pending,save_inflight;
    ouse_device_port_t port;
    ouse_device_config_t config;
    ouse_power_state_t power;
    ouse_audio_state_t audio;
    ouse_wifi_state_t wifi;
    ouse_sd_state_t sd;
    ouse_audio_request_t audio_request;
    ouse_wifi_request_t wifi_request;
    ouse_device_save_state_t save;
    uint32_t save_id,save_task;
    uint32_t wifi_task;
    uint32_t suspended_media;
} devices;
static void lock(void) { if(devices.port.lock)devices.port.lock(); }
static void unlock(void) { if(devices.port.unlock)devices.port.unlock(); }
static void notify(unsigned worker) { if(devices.port.notify)devices.port.notify(worker); }
static bool timeout_valid(unsigned s) {return !s || s==15 || s==30 || s==60 || s==120 || s==300;}
static void dirty(void) {++devices.save_id;devices.save_pending=true;devices.save.pending=true;devices.save.error=ESP_OK;++devices.save.revision;}
esp_err_t ouse_devices_init(const ouse_device_port_t *port,const ouse_device_config_t *config)
{
    if(!port || !config || !timeout_valid(config->sleep_seconds) || config->volume>100)return ESP_ERR_INVALID_ARG;
    if(devices.initialized)return ESP_ERR_INVALID_STATE;
    devices.port=*port;devices.config=*config;
    devices.config.ssid[32]=0;devices.config.password[64]=0;
    devices.power=(ouse_power_state_t){.percent=-1,.sleep_seconds=config->sleep_seconds};
    devices.audio=(ouse_audio_state_t){.volume=config->volume,.muted=config->muted};
    devices.wifi.enabled=config->wifi_enabled;devices.initialized=true;return ESP_OK;
}
void ouse_devices_get_config(ouse_device_config_t *config) {lock();*config=devices.config;unlock();}
#define GET(name,field,type) esp_err_t name(type *out) {if(!out)return ESP_ERR_INVALID_ARG;lock();*out=devices.field;bool ready=devices.initialized;unlock();return ready ? ESP_OK : ESP_ERR_INVALID_STATE;}
GET(ouse_power_get,power,ouse_power_state_t)
GET(ouse_audio_get,audio,ouse_audio_state_t)
GET(ouse_wifi_get,wifi,ouse_wifi_state_t)
GET(ouse_sd_get,sd,ouse_sd_state_t)
void ouse_devices_publish_sd(const ouse_sd_state_t *state)
{
    if(!state)return;
    lock();devices.sd=*state;devices.sd.name[sizeof(devices.sd.name)-1]=0;unlock();
}
uint32_t ouse_devices_revision(unsigned group) {lock();uint32_t r=group==0 ? devices.power.revision : group==1 ? devices.audio.revision : devices.wifi.revision;unlock();return r;}
void ouse_devices_set_available(unsigned group,bool available,esp_err_t error)
{
    lock();
    if(group==0){devices.power.available=available;devices.power.error=error;++devices.power.revision;}
    else if(group==1){devices.audio.available=available;devices.audio.error=error;++devices.audio.revision;}
    else {devices.wifi.available=available;devices.wifi.error=error;++devices.wifi.revision;}
    unlock();
}
esp_err_t ouse_power_set_timeout(unsigned seconds)
{
    if(!timeout_valid(seconds))return ESP_ERR_INVALID_ARG;
    if(!ouse_tasks_submission_begin())return ESP_ERR_INVALID_STATE;
    lock();if(!devices.initialized){unlock();ouse_tasks_submission_end();return ESP_ERR_INVALID_STATE;}
    if(devices.config.sleep_seconds!=seconds){devices.config.sleep_seconds=seconds;devices.power.sleep_seconds=seconds;++devices.power.revision;dirty();}
    unlock();ouse_tasks_submission_end();notify(0);return ESP_OK;
}
void ouse_power_set_asleep(bool asleep) {lock();if(devices.power.asleep!=asleep){devices.power.asleep=asleep;++devices.power.revision;}unlock();}
void ouse_devices_publish_power(const ouse_power_state_t *state)
{
    lock();bool asleep=devices.power.asleep;uint32_t revision=devices.power.revision;
    devices.power=*state;devices.power.asleep=asleep;devices.power.sleep_seconds=devices.config.sleep_seconds;devices.power.revision=revision+1;unlock();
}
esp_err_t ouse_audio_set_volume(unsigned volume)
{
    if(volume>100)return ESP_ERR_INVALID_ARG;
    if(!ouse_tasks_submission_begin())return ESP_ERR_INVALID_STATE;
    lock();if(!devices.initialized){unlock();ouse_tasks_submission_end();return ESP_ERR_INVALID_STATE;}
    if(devices.config.volume!=volume){devices.config.volume=volume;devices.audio.volume=volume;++devices.audio.revision;dirty();}
    unlock();ouse_tasks_submission_end();notify(0);notify(1);return ESP_OK;
}
esp_err_t ouse_audio_set_muted(bool muted)
{
    if(!ouse_tasks_submission_begin())return ESP_ERR_INVALID_STATE;
    lock();if(!devices.initialized){unlock();ouse_tasks_submission_end();return ESP_ERR_INVALID_STATE;}
    if(devices.config.muted!=muted){devices.config.muted=muted;devices.audio.muted=muted;++devices.audio.revision;dirty();}
    unlock();ouse_tasks_submission_end();notify(0);notify(1);return ESP_OK;
}
static const ouse_task_owner_t system_owner={.app_id="ouse.system"};
void ouse_devices_wake_workers(void){notify(0);notify(1);notify(2);}
esp_err_t ouse_audio_submit(const ouse_task_owner_t *owner,ouse_audio_action_t action,unsigned seconds,uint32_t *id)
{
    if(!id || (unsigned)action>OUSE_AUDIO_RING || (action==OUSE_AUDIO_CAPTURE && (seconds<1 || seconds>3)) ||
       (action==OUSE_AUDIO_RING && (!owner || owner->instance || strcmp(owner->app_id,"ouse.alarm") || seconds<1 || seconds>60)))return ESP_ERR_INVALID_ARG;
    lock();if(!devices.audio.available){unlock();return ESP_ERR_NOT_SUPPORTED;}
    // A queued media consumer owns a separate producer rendezvous. Do not lose
    // it by coalescing a short test request; an alarm preserves it explicitly.
    if(action!=OUSE_AUDIO_RING && devices.audio_pending && devices.audio_request.action==OUSE_AUDIO_MEDIA){unlock();return ESP_ERR_INVALID_STATE;}
    ouse_task_snapshot_t active;
    if(action!=OUSE_AUDIO_RING && action!=OUSE_AUDIO_STOP && devices.audio_request.action==OUSE_AUDIO_USB &&
       ouse_task_get(NULL,devices.audio.request_id,&active) && active.state<OUSE_TASK_SUCCEEDED){unlock();return ESP_ERR_INVALID_STATE;}
    if(action!=OUSE_AUDIO_RING && (devices.suspended_media || (devices.audio_request.action==OUSE_AUDIO_RING &&
       ouse_task_get(NULL,devices.audio.request_id,&active) && active.state<OUSE_TASK_SUCCEEDED))){unlock();return ESP_ERR_INVALID_STATE;}
    if(action==OUSE_AUDIO_REPLAY && !devices.audio.recorded_ms){unlock();return ESP_ERR_INVALID_STATE;}
    static const char *names[]={"stop","testTone","record","playRecording","alarmRing"};
    esp_err_t error=ouse_task_create(owner ? owner : &system_owner,OUSE_TASK_AUDIO,names[action],OUSE_TASK_STOP_ON_CLOSE,true,id);
    if(error!=ESP_OK){unlock();return error;}
    // Queued work can be retired now. Running work is retired by the board only
    // after codec close; replacing its ID is merely a cooperative stop request.
    if(devices.audio.request_id){
        if(action==OUSE_AUDIO_RING && devices.audio_request.action==OUSE_AUDIO_MEDIA)devices.suspended_media=devices.audio.request_id;
        else{
            (void)ouse_task_cancel(NULL,devices.audio.request_id,
                action==OUSE_AUDIO_RING && devices.audio_request.action==OUSE_AUDIO_USB ? "alarm_preempt" : "superseded");
            if(devices.audio_pending)ouse_task_finish(devices.audio.request_id,ESP_OK,true);
        }
    }
    devices.audio_request=(ouse_audio_request_t){.id=*id,.action=action,.seconds=seconds};
    devices.audio.request_id=*id;devices.audio_pending=true;devices.audio.mode=OUSE_AUDIO_STARTING;
    devices.audio.error=ESP_OK;++devices.audio.revision;unlock();notify(1);return ESP_OK;
}
esp_err_t ouse_audio_test_tone(void) {uint32_t id;return ouse_audio_submit(NULL,OUSE_AUDIO_TEST,0,&id);}
esp_err_t ouse_audio_record(unsigned seconds) {uint32_t id;return ouse_audio_submit(NULL,OUSE_AUDIO_CAPTURE,seconds,&id);}
esp_err_t ouse_audio_play_recording(void) {uint32_t id;return ouse_audio_submit(NULL,OUSE_AUDIO_REPLAY,0,&id);}
esp_err_t ouse_audio_stop(void) {uint32_t id;return ouse_audio_submit(NULL,OUSE_AUDIO_STOP,0,&id);}
bool ouse_devices_take_audio(ouse_audio_request_t *out) {lock();bool pending=devices.audio_pending;if(pending){*out=devices.audio_request;devices.audio_pending=false;}unlock();return pending;}
esp_err_t ouse_devices_queue_media(uint32_t id)
{
    if(!id)return ESP_ERR_INVALID_ARG;
    lock();ouse_task_snapshot_t active;
    if(!devices.audio.available){unlock();return ESP_ERR_NOT_SUPPORTED;}
    if(devices.audio_request.action==OUSE_AUDIO_USB &&
       ouse_task_get(NULL,devices.audio.request_id,&active) && active.state<OUSE_TASK_SUCCEEDED){unlock();return ESP_ERR_INVALID_STATE;}
    if(devices.suspended_media || (devices.audio_request.action==OUSE_AUDIO_RING &&
       ouse_task_get(NULL,devices.audio.request_id,&active) && active.state<OUSE_TASK_SUCCEEDED)){unlock();return ESP_ERR_INVALID_STATE;}
    if(devices.audio.request_id){
        (void)ouse_task_cancel(NULL,devices.audio.request_id,"media_focus");
        if(devices.audio_pending)ouse_task_finish(devices.audio.request_id,ESP_OK,true);
    }
    devices.audio_request=(ouse_audio_request_t){.id=id,.action=OUSE_AUDIO_MEDIA};
    devices.audio.request_id=id;devices.audio_pending=true;devices.audio.mode=OUSE_AUDIO_STARTING;
    devices.audio.error=ESP_OK;++devices.audio.revision;unlock();notify(1);return ESP_OK;
}
esp_err_t ouse_devices_queue_usb(uint32_t *id)
{
    if(!id)return ESP_ERR_INVALID_ARG;
    lock();ouse_task_snapshot_t active;
    if(!devices.audio.available){unlock();return ESP_ERR_NOT_SUPPORTED;}
    if(devices.suspended_media || (devices.audio.request_id &&
       ouse_task_get(NULL,devices.audio.request_id,&active) && active.state<OUSE_TASK_SUCCEEDED &&
       devices.audio_request.action!=OUSE_AUDIO_MEDIA)){unlock();return ESP_ERR_INVALID_STATE;}
    static const ouse_task_owner_t owner={.app_id="ouse.usb"};
    esp_err_t error=ouse_task_create(&owner,OUSE_TASK_AUDIO,"usbDuplex",OUSE_TASK_FINISH_ON_CLOSE,true,id);
    if(error!=ESP_OK){unlock();return error;}
    if(devices.audio.request_id){
        (void)ouse_task_cancel(NULL,devices.audio.request_id,"usb_focus");
        if(devices.audio_pending)ouse_task_finish(devices.audio.request_id,ESP_OK,true);
    }
    devices.audio_request=(ouse_audio_request_t){.id=*id,.action=OUSE_AUDIO_USB};
    devices.audio.request_id=*id;devices.audio_pending=true;devices.audio.mode=OUSE_AUDIO_STARTING;
    devices.audio.error=ESP_OK;++devices.audio.revision;unlock();notify(1);return ESP_OK;
}
bool ouse_devices_take_alarm(ouse_audio_request_t *out)
{
    lock();bool found=devices.audio_pending && devices.audio_request.action==OUSE_AUDIO_RING;
    if(found){*out=devices.audio_request;devices.audio_pending=false;}
    unlock();return found;
}
uint32_t ouse_devices_resume_media(bool enqueue)
{
    lock();uint32_t id=0;
    if(devices.suspended_media && !devices.audio_pending && devices.audio_request.action==OUSE_AUDIO_RING &&
       devices.audio.completed_id==devices.audio.request_id){
        id=devices.suspended_media;devices.suspended_media=0;
        devices.audio_request=(ouse_audio_request_t){.id=id,.action=OUSE_AUDIO_MEDIA};devices.audio.request_id=id;
        devices.audio_pending=enqueue;devices.audio.mode=OUSE_AUDIO_STARTING;++devices.audio.revision;
    }
    unlock();if(id && enqueue)notify(1);return id;
}
bool ouse_devices_audio_current(uint32_t id) {lock();bool current=devices.audio.request_id==id;unlock();return current && !ouse_task_cancel_requested(id);}
void ouse_devices_publish_audio(const ouse_audio_state_t *state,uint32_t id)
{
    lock();if(devices.audio.request_id==id){uint32_t revision=devices.audio.revision;devices.audio=*state;devices.audio.available=true;
        devices.audio.volume=devices.config.volume;devices.audio.muted=devices.config.muted;devices.audio.request_id=id;devices.audio.revision=revision+1;}
    unlock();
}
esp_err_t ouse_wifi_submit(const ouse_task_owner_t *owner,ouse_wifi_action_t action,bool enabled,
    const char *ssid,const char *password,uint32_t *id)
{
    if(!id || (unsigned)action>OUSE_WIFI_FORGET)return ESP_ERR_INVALID_ARG;
    if(action==OUSE_WIFI_CONNECT){
        if(!ssid || !password || !*ssid || strlen(ssid)>32 || strlen(password)>64)return ESP_ERR_INVALID_ARG;
        size_t n=strlen(password);if(n && n<8)return ESP_ERR_INVALID_ARG;
        if(n==64)for(unsigned i=0;i<64;++i)if(!((password[i]>='0'&&password[i]<='9')||(password[i]>='a'&&password[i]<='f')||(password[i]>='A'&&password[i]<='F')))return ESP_ERR_INVALID_ARG;
    }
    lock();if(!devices.wifi.available){unlock();return ESP_ERR_NOT_SUPPORTED;}
    // ESP Wi-Fi events carry no application instance token. Serialize accepted
    // operations instead of silently overwriting an active request's identity.
    ouse_task_snapshot_t active;
    if(devices.wifi_pending || (ouse_task_get(NULL,devices.wifi_task,&active) && active.state<OUSE_TASK_SUCCEEDED)){
        unlock();return ESP_ERR_INVALID_STATE;
    }
    if(action!=OUSE_WIFI_ENABLE && action!=OUSE_WIFI_FORGET && action!=OUSE_WIFI_DISCONNECT && !devices.config.wifi_enabled){unlock();return ESP_ERR_INVALID_STATE;}
    static const char *names[]={"setEnabled","scan","connect","disconnect","forget"};
    esp_err_t error=ouse_task_create(owner ? owner : &system_owner,OUSE_TASK_WIFI,names[action],OUSE_TASK_FINISH_ON_CLOSE,true,id);
    if(error!=ESP_OK){unlock();return error;}
    if(action==OUSE_WIFI_ENABLE && devices.config.wifi_enabled!=enabled){devices.config.wifi_enabled=enabled;dirty();}
    if(action==OUSE_WIFI_FORGET){devices.config.ssid[0]=devices.config.password[0]=0;dirty();}
    devices.wifi_request=(ouse_wifi_request_t){.id=*id,.action=action,.enabled=devices.config.wifi_enabled};
    strcpy(devices.wifi_request.ssid,action==OUSE_WIFI_FORGET ? "" : ssid ? ssid : devices.config.ssid);
    strcpy(devices.wifi_request.password,action==OUSE_WIFI_FORGET ? "" : password ? password : devices.config.password);
    devices.wifi_task=*id;devices.wifi.request_id=*id;devices.wifi_pending=true;
    devices.wifi.enabled=devices.config.wifi_enabled;devices.wifi.error=ESP_OK;devices.wifi.reason=0;++devices.wifi.revision;
    unlock();notify(0);notify(2);return ESP_OK;
}
esp_err_t ouse_wifi_set_enabled(bool enabled) {uint32_t id;return ouse_wifi_submit(NULL,OUSE_WIFI_ENABLE,enabled,NULL,NULL,&id);}
esp_err_t ouse_wifi_scan(void) {uint32_t id;return ouse_wifi_submit(NULL,OUSE_WIFI_SCAN,false,NULL,NULL,&id);}
esp_err_t ouse_wifi_connect(const char *ssid,const char *password) {uint32_t id;return ouse_wifi_submit(NULL,OUSE_WIFI_CONNECT,false,ssid,password,&id);}
esp_err_t ouse_wifi_disconnect(void) {uint32_t id;return ouse_wifi_submit(NULL,OUSE_WIFI_DISCONNECT,false,NULL,NULL,&id);}
esp_err_t ouse_wifi_forget(void) {uint32_t id;return ouse_wifi_submit(NULL,OUSE_WIFI_FORGET,false,NULL,NULL,&id);}
bool ouse_devices_take_wifi(ouse_wifi_request_t *out) {lock();bool pending=devices.wifi_pending;if(pending){*out=devices.wifi_request;devices.wifi_pending=false;}unlock();return pending;}
bool ouse_devices_wifi_current(uint32_t id) {lock();bool current=devices.wifi.request_id==id;unlock();return current;}
void ouse_devices_publish_wifi(const ouse_wifi_state_t *state,uint32_t id)
{
    lock();if(devices.wifi.request_id==id){uint32_t r=devices.wifi.revision;devices.wifi=*state;devices.wifi.available=true;
        devices.wifi.enabled=devices.config.wifi_enabled;devices.wifi.request_id=id;devices.wifi.revision=r+1;}
    unlock();
}
void ouse_devices_remember_wifi(const ouse_wifi_request_t *request)
{
    lock();if(devices.wifi.request_id==request->id && (strcmp(devices.config.ssid,request->ssid)||strcmp(devices.config.password,request->password))){
        strcpy(devices.config.ssid,request->ssid);strcpy(devices.config.password,request->password);dirty();}
    unlock();notify(0);
}
bool ouse_devices_take_save(ouse_device_config_t *out,uint32_t *id)
{
    lock();bool pending=devices.save_pending;
    if(pending){
        devices.save_task=0;
        if(ouse_task_create(&system_owner,OUSE_TASK_SETTINGS,"devicesSave",OUSE_TASK_FINISH_ON_CLOSE,false,&devices.save_task)==ESP_OK)
            (void)ouse_task_start(devices.save_task,false);
        *out=devices.config;*id=devices.save_id;devices.save_pending=false;devices.save_inflight=true;
    }unlock();return pending;
}
void ouse_devices_saved(uint32_t id,esp_err_t error)
{
    lock();ouse_task_finish(devices.save_task,error,false);devices.save_inflight=false;if(id==devices.save_id){devices.save.error=error;devices.save.pending=false;++devices.save.revision;}unlock();
}
void ouse_devices_save_state(ouse_device_save_state_t *out) {lock();*out=devices.save;unlock();}
void ouse_devices_retry_save(void) {if(!ouse_tasks_submission_begin())return;lock();if(devices.initialized)dirty();unlock();ouse_tasks_submission_end();notify(0);}
