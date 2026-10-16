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
    ouse_audio_request_t audio_request;
    ouse_wifi_request_t wifi_request;
    ouse_device_save_state_t save;
    uint32_t save_id;
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
    lock();if(!devices.initialized){unlock();return ESP_ERR_INVALID_STATE;}
    if(devices.config.sleep_seconds!=seconds){devices.config.sleep_seconds=seconds;devices.power.sleep_seconds=seconds;++devices.power.revision;dirty();}
    unlock();notify(0);return ESP_OK;
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
    lock();if(!devices.initialized){unlock();return ESP_ERR_INVALID_STATE;}
    if(devices.config.volume!=volume){devices.config.volume=volume;devices.audio.volume=volume;++devices.audio.revision;dirty();}
    unlock();notify(0);notify(1);return ESP_OK;
}
esp_err_t ouse_audio_set_muted(bool muted)
{
    lock();if(!devices.initialized){unlock();return ESP_ERR_INVALID_STATE;}
    if(devices.config.muted!=muted){devices.config.muted=muted;devices.audio.muted=muted;++devices.audio.revision;dirty();}
    unlock();notify(0);notify(1);return ESP_OK;
}
static esp_err_t audio_request(ouse_audio_action_t action,unsigned seconds)
{
    lock();if(!devices.audio.available){unlock();return ESP_ERR_NOT_SUPPORTED;}
    if(action==OUSE_AUDIO_REPLAY && !devices.audio.recorded_ms){unlock();return ESP_ERR_INVALID_STATE;}
    devices.audio_request=(ouse_audio_request_t){.id=++devices.audio.request_id,.action=action,.seconds=seconds};
    devices.audio_pending=true;devices.audio.mode=OUSE_AUDIO_STARTING;devices.audio.error=ESP_OK;++devices.audio.revision;
    unlock();notify(1);return ESP_OK;
}
esp_err_t ouse_audio_test_tone(void) {return audio_request(OUSE_AUDIO_TEST,0);}
esp_err_t ouse_audio_record(unsigned seconds) {return seconds>=1 && seconds<=3 ? audio_request(OUSE_AUDIO_CAPTURE,seconds) : ESP_ERR_INVALID_ARG;}
esp_err_t ouse_audio_play_recording(void) {return audio_request(OUSE_AUDIO_REPLAY,0);}
esp_err_t ouse_audio_stop(void) {return audio_request(OUSE_AUDIO_STOP,0);}
bool ouse_devices_take_audio(ouse_audio_request_t *out) {lock();bool pending=devices.audio_pending;if(pending){*out=devices.audio_request;devices.audio_pending=false;}unlock();return pending;}
bool ouse_devices_audio_current(uint32_t id) {lock();bool current=devices.audio.request_id==id;unlock();return current;}
void ouse_devices_publish_audio(const ouse_audio_state_t *state,uint32_t id)
{
    lock();if(devices.audio.request_id==id){uint32_t revision=devices.audio.revision;devices.audio=*state;devices.audio.available=true;
        devices.audio.volume=devices.config.volume;devices.audio.muted=devices.config.muted;devices.audio.request_id=id;devices.audio.revision=revision+1;}
    unlock();
}
static esp_err_t wifi_request(ouse_wifi_action_t action,const char *ssid,const char *password)
{
    lock();if(!devices.wifi.available){unlock();return ESP_ERR_NOT_SUPPORTED;}
    if(action!=OUSE_WIFI_ENABLE && action!=OUSE_WIFI_FORGET && action!=OUSE_WIFI_DISCONNECT && !devices.config.wifi_enabled){unlock();return ESP_ERR_INVALID_STATE;}
    if(action==OUSE_WIFI_SCAN && devices.wifi.connecting){unlock();return ESP_ERR_INVALID_STATE;}
    devices.wifi_request=(ouse_wifi_request_t){.id=++devices.wifi.request_id,.action=action,.enabled=devices.config.wifi_enabled};
    strcpy(devices.wifi_request.ssid,ssid ? ssid : devices.config.ssid);
    strcpy(devices.wifi_request.password,password ? password : devices.config.password);
    devices.wifi_pending=true;devices.wifi.error=ESP_OK;devices.wifi.reason=0;++devices.wifi.revision;
    if(action==OUSE_WIFI_SCAN)devices.wifi.scanning=true;
    if(action==OUSE_WIFI_CONNECT){devices.wifi.connecting=true;devices.wifi.scanning=false;devices.wifi.connected=false;strcpy(devices.wifi.ssid,ssid);devices.wifi.ip[0]=0;}
    if(action==OUSE_WIFI_DISCONNECT || action==OUSE_WIFI_FORGET){devices.wifi.connecting=false;devices.wifi.connected=false;devices.wifi.scanning=false;devices.wifi.ip[0]=0;}
    unlock();notify(2);return ESP_OK;
}
esp_err_t ouse_wifi_set_enabled(bool enabled)
{
    lock();if(!devices.wifi.available){unlock();return ESP_ERR_NOT_SUPPORTED;}
    if(devices.config.wifi_enabled!=enabled){devices.config.wifi_enabled=enabled;dirty();}
    devices.wifi.enabled=enabled;
    if(!enabled){devices.wifi.connecting=false;devices.wifi.scanning=false;devices.wifi.connected=false;devices.wifi.ip[0]=0;}
    unlock();notify(0);return wifi_request(OUSE_WIFI_ENABLE,NULL,NULL);
}
esp_err_t ouse_wifi_scan(void) {return wifi_request(OUSE_WIFI_SCAN,NULL,NULL);}
esp_err_t ouse_wifi_connect(const char *ssid,const char *password)
{
    if(!ssid || !password || !*ssid || strlen(ssid)>32 || strlen(password)>64)return ESP_ERR_INVALID_ARG;
    size_t n=strlen(password);if(n && n<8)return ESP_ERR_INVALID_ARG;
    if(n==64)for(unsigned i=0;i<64;++i)if(!((password[i]>='0'&&password[i]<='9')||(password[i]>='a'&&password[i]<='f')||(password[i]>='A'&&password[i]<='F')))return ESP_ERR_INVALID_ARG;
    return wifi_request(OUSE_WIFI_CONNECT,ssid,password);
}
esp_err_t ouse_wifi_disconnect(void) {return wifi_request(OUSE_WIFI_DISCONNECT,NULL,NULL);}
esp_err_t ouse_wifi_forget(void)
{
    lock();if(!devices.wifi.available){unlock();return ESP_ERR_NOT_SUPPORTED;}
    devices.config.ssid[0]=devices.config.password[0]=0;dirty();unlock();notify(0);
    return wifi_request(OUSE_WIFI_FORGET,"","");
}
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
    if(pending){*out=devices.config;*id=devices.save_id;devices.save_pending=false;devices.save_inflight=true;}unlock();return pending;
}
void ouse_devices_saved(uint32_t id,esp_err_t error)
{
    lock();devices.save_inflight=false;if(id==devices.save_id){devices.save.error=error;devices.save.pending=false;++devices.save.revision;}unlock();
}
void ouse_devices_save_state(ouse_device_save_state_t *out) {lock();*out=devices.save;unlock();}
void ouse_devices_retry_save(void) {lock();if(devices.initialized)dirty();unlock();notify(0);}
