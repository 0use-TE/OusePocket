// SPDX-License-Identifier: MIT
#include "ouse_device_driver.h"
#include "esp_hosted.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
static TaskHandle_t worker;
static portMUX_TYPE event_lock=portMUX_INITIALIZER_UNLOCKED;
static unsigned event_bits;
static int disconnect_reason;
static char expected_ssid[33]; // event_lock protects the request identity seen by callbacks.
static esp_ip4_addr_t ip_address;
enum { EVENT_SCAN=1,EVENT_IP=2,EVENT_DISCONNECT=4,EVENT_DOWN=8 };
static bool hosted,wifi_created,initialized,started,sntp_started;
static ouse_wifi_state_t state;
static ouse_wifi_request_t session;
static int64_t deadline;
static unsigned retries;
static void event(void *argument,esp_event_base_t base,int32_t id,void *data)
{
    (void)argument;unsigned bits=0;
    portENTER_CRITICAL(&event_lock);
    if(base==WIFI_EVENT && id==WIFI_EVENT_SCAN_DONE)bits=EVENT_SCAN;
    else if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnected=data;
        // A previous AP can report its final failure after a new request starts.
        // Ignore it before it can replace the new request's queued IP event.
        // Keep untagged events usable for co-processors that omit the SSID.
        if(disconnected->ssid_len && (disconnected->ssid_len>32 ||
           strlen(expected_ssid)!=disconnected->ssid_len ||
           memcmp(expected_ssid,disconnected->ssid,disconnected->ssid_len))){
            portEXIT_CRITICAL(&event_lock);return;
        }
        // Connectivity notifications can be coalesced before the worker wakes.
        // Keep the last transition rather than processing IP then disconnect
        // regardless of their actual order. Scan/transport events stay separate.
        event_bits&=~EVENT_IP;
        bits=EVENT_DISCONNECT;disconnect_reason=disconnected->reason;
    } else if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        event_bits&=~EVENT_DISCONNECT;
        bits=EVENT_IP;ip_address=((ip_event_got_ip_t *)data)->ip_info.ip;
    }
    else if(base==ESP_HOSTED_EVENT && (id==ESP_HOSTED_EVENT_TRANSPORT_DOWN||id==ESP_HOSTED_EVENT_TRANSPORT_FAILURE))bits=EVENT_DOWN;
    event_bits|=bits;portEXIT_CRITICAL(&event_lock);
    if(worker)xTaskNotifyGive(worker);
}
static esp_err_t init(void)
{
    if(initialized) {
        if(state.ready)return ESP_OK;
        return esp_hosted_connect_to_slave();
    }
    esp_err_t e=esp_netif_init();if(e!=ESP_OK)return e;
    e=esp_event_loop_create_default();if(e!=ESP_OK && e!=ESP_ERR_INVALID_STATE)return e;
    if(!hosted) {
        e=esp_event_handler_register(ESP_HOSTED_EVENT,ESP_EVENT_ANY_ID,event,NULL);if(e!=ESP_OK)return e;
        e=esp_hosted_init();if(e!=ESP_OK)return e;hosted=true;
    }
    // No UI lock is held. The transport has a bounded handshake and restart
    // on loss of C6 is disabled in product Kconfig.
    e=esp_hosted_connect_to_slave();if(e!=ESP_OK)return e;
    // Follow archived Waveshare 04_wifistation: the mandatory readiness RPC is
    // esp_wifi_init, not GetCoprocessorFwVersion. Older C6 firmware can omit the
    // version extension while still implementing the Station API. Keep upstream
    // version warnings and propagate every actual Wi-Fi RPC failure below.
    static esp_netif_t *sta;
    if(!sta)sta=esp_netif_create_default_wifi_sta();
    if(!sta)return ESP_ERR_NO_MEM;
    static bool handlers;
    if(!handlers) {
        e=esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,NULL);if(e!=ESP_OK)return e;
        e=esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event,NULL);if(e!=ESP_OK)return e;
        handlers=true;
    }
    wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();
    if(!wifi_created){
        e=esp_wifi_init(&cfg);
        if(e!=ESP_OK){ESP_LOGW("OuseWiFi","esp_wifi_init failed: %s",esp_err_to_name(e));return e;}
        wifi_created=true;
    }
    e=esp_wifi_set_storage(WIFI_STORAGE_RAM);if(e==ESP_OK)e=esp_wifi_set_mode(WIFI_MODE_STA);
    if(e!=ESP_OK){
        ESP_LOGW("OuseWiFi","Wi-Fi storage/mode failed: %s",esp_err_to_name(e));
        esp_err_t cleanup=esp_wifi_deinit();
        if(cleanup==ESP_OK || cleanup==ESP_ERR_WIFI_NOT_INIT)wifi_created=false;
        else ESP_LOGW("OuseWiFi","Wi-Fi cleanup failed: %s",esp_err_to_name(cleanup));
        // A failed deinit may leave the driver allocated. Retry configuration
        // on that instance rather than losing it and initializing a second one.
        return e;
    }
    ESP_LOGI("OuseWiFi","C6 Wi-Fi STA initialized through esp_wifi_remote");
    initialized=true;return ESP_OK;
}
static esp_err_t begin_connection(void)
{
    wifi_config_t cfg={0};
    memcpy(cfg.sta.ssid,session.ssid,strlen(session.ssid));
    memcpy(cfg.sta.password,session.password,strlen(session.password));
    cfg.sta.threshold.authmode=*session.password ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable=true;cfg.sta.pmf_cfg.required=false;
    esp_err_t e=esp_wifi_set_config(WIFI_IF_STA,&cfg);
    memset(cfg.sta.password,0,sizeof(cfg.sta.password));
    if(e==ESP_OK)e=esp_wifi_connect();
    state.connecting=e==ESP_OK;state.connected=false;state.ip[0]=0;
    snprintf(state.ssid,sizeof(state.ssid),"%s",session.ssid);
    deadline=esp_timer_get_time()+15000000;return e;
}
static esp_err_t request(ouse_wifi_request_t command)
{
    session=command;state.request_id=command.id;state.error=ESP_OK;state.reason=0;
    portENTER_CRITICAL(&event_lock);memcpy(expected_ssid,command.ssid,sizeof(expected_ssid));portEXIT_CRITICAL(&event_lock);
    if(!command.enabled) {
        state.connecting=state.scanning=state.connected=false;state.ip[0]=0;
        if(started){
            (void)esp_wifi_scan_stop();(void)esp_wifi_disconnect();esp_err_t e=esp_wifi_stop();
            if(e!=ESP_OK && e!=ESP_ERR_WIFI_NOT_STARTED)return e;
            started=false;
        }
        return ESP_OK;
    }
    esp_err_t e=init();if(e!=ESP_OK)return e;
    if(!ouse_devices_wifi_current(command.id))return ESP_OK;
    state.ready=true;
    if(!started){e=esp_wifi_start();if(e!=ESP_OK)return e;started=true;}
    if(command.action==OUSE_WIFI_DISCONNECT || command.action==OUSE_WIFI_FORGET) {
        state.connecting=state.connected=false;state.ssid[0]=state.ip[0]=0;
        e=esp_wifi_disconnect();return e==ESP_ERR_WIFI_NOT_CONNECT ? ESP_OK : e;
    }
    if(command.action==OUSE_WIFI_SCAN || (command.action==OUSE_WIFI_ENABLE && !*command.ssid)) {
        if(state.connecting)return ESP_ERR_INVALID_STATE;
        wifi_scan_config_t scan={.show_hidden=false};
        e=esp_wifi_scan_start(&scan,false);state.scanning=e==ESP_OK;deadline=esp_timer_get_time()+15000000;return e;
    }
    (void)esp_wifi_scan_stop();state.scanning=false;
    state.connecting=false;(void)esp_wifi_disconnect();
    portENTER_CRITICAL(&event_lock);event_bits&=~(EVENT_DISCONNECT|EVENT_IP);portEXIT_CRITICAL(&event_lock);
    retries=0;return begin_connection();
}
static void handle_events(unsigned bits,int reason,esp_ip4_addr_t ip)
{
    if(!ouse_devices_wifi_current(session.id))return;
    if(bits&EVENT_DOWN) {
        state.error=ESP_ERR_TIMEOUT;state.ready=false;state.connected=state.connecting=state.scanning=false;state.ip[0]=0;
        state.completed_id=session.id;return;
    }
    if(!state.enabled)return;
    if((bits&EVENT_SCAN) && state.scanning) {
        wifi_ap_record_t aps[OUSE_WIFI_AP_MAX];uint16_t count=OUSE_WIFI_AP_MAX;
        esp_err_t e=esp_wifi_scan_get_ap_records(&count,aps);state.scanning=false;state.count=0;state.error=e;
        if(e==ESP_OK)for(unsigned i=0;i<count;++i) {
            if(!aps[i].ssid[0])continue;
            bool duplicate=false;
            for(unsigned j=0;j<state.count;++j)if(!strncmp(state.aps[j].ssid,(char *)aps[i].ssid,32))duplicate=true;
            if(duplicate)continue;
            ouse_wifi_ap_t *ap=&state.aps[state.count++];
            memcpy(ap->ssid,aps[i].ssid,32);ap->ssid[32]=0;ap->rssi=aps[i].rssi;ap->channel=aps[i].primary;ap->secured=aps[i].authmode!=WIFI_AUTH_OPEN;
        }
        state.completed_id=session.id;
    }
    if((bits&EVENT_IP) && (state.connecting||state.connected)) {
        wifi_ap_record_t ap;
        if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK && !strncmp((char *)ap.ssid,session.ssid,32)) {
            state.connected=true;state.connecting=false;state.error=ESP_OK;state.rssi=ap.rssi;state.reason=0;
            snprintf(state.ip,sizeof(state.ip),IPSTR,IP2STR(&ip));state.completed_id=session.id;
            ouse_devices_remember_wifi(&session);
            if(!sntp_started) {
                setenv("TZ","CST-8",1);tzset();
                esp_sntp_config_t cfg=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
                if(esp_netif_sntp_init(&cfg)==ESP_OK)sntp_started=true;
            }
        }
    }
    if((bits&EVENT_DISCONNECT) && reason!=WIFI_REASON_ASSOC_LEAVE && (state.connected||state.connecting)) {
        state.connected=false;state.ip[0]=0;state.reason=reason;
        if(retries++<2 && reason!=WIFI_REASON_AUTH_FAIL && reason!=WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT && reason!=WIFI_REASON_HANDSHAKE_TIMEOUT){
            state.error=begin_connection();
            if(state.error!=ESP_OK)state.completed_id=session.id;
        }
        else {state.connecting=false;state.error=ESP_FAIL;state.completed_id=session.id;}
    }
}
void ouse_board_wifi_worker(void *argument)
{
    (void)argument;worker=xTaskGetCurrentTaskHandle();
    int64_t next_signal=0;
    for(;;) {
        bool changed=false;ouse_wifi_request_t command;
        if(ouse_devices_take_wifi(&command)) {
            ouse_wifi_get(&state);state.error=request(command);memset(command.password,0,sizeof(command.password));
            if(state.error!=ESP_OK){state.scanning=state.connecting=false;state.completed_id=session.id;ESP_LOGW("OuseWiFi","Operation: %s",esp_err_to_name(state.error));}
            else if(!state.connecting && !state.scanning)state.completed_id=session.id;
            changed=true;
        }
        unsigned bits;int reason;esp_ip4_addr_t ip;
        portENTER_CRITICAL(&event_lock);bits=event_bits;event_bits=0;reason=disconnect_reason;ip=ip_address;portEXIT_CRITICAL(&event_lock);
        if(bits){handle_events(bits,reason,ip);changed=true;}
        if((state.scanning||state.connecting) && esp_timer_get_time()>deadline) {
            if(state.scanning)(void)esp_wifi_scan_stop();else (void)esp_wifi_disconnect();
            state.scanning=state.connecting=false;state.error=ESP_ERR_TIMEOUT;state.completed_id=session.id;changed=true;
        }
        if(state.connected && esp_timer_get_time()>=next_signal && ouse_devices_wifi_current(session.id)) {
            next_signal=esp_timer_get_time()+5000000;
            wifi_ap_record_t ap;
            if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK && state.rssi!=ap.rssi){state.rssi=ap.rssi;changed=true;}
        }
        if(changed)ouse_devices_publish_wifi(&state,session.id);
        uint32_t wait=(state.scanning||state.connecting) ? pdMS_TO_TICKS(250) : state.connected ? pdMS_TO_TICKS(5000) : portMAX_DELAY;
        ulTaskNotifyTake(pdTRUE,wait);
    }
}
