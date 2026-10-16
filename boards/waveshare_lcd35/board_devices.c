// SPDX-License-Identifier: MIT
#include "ouse_device_driver.h"
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
static portMUX_TYPE devices_lock=portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t workers[3];
static i2c_master_dev_handle_t pmic;
static void lock(void) {portENTER_CRITICAL(&devices_lock);}
static void unlock(void) {portEXIT_CRITICAL(&devices_lock);}
static void notify(unsigned worker) {if(worker<3 && workers[worker])xTaskNotifyGive(workers[worker]);}
extern void ouse_board_audio_worker(void *);
extern void ouse_board_wifi_worker(void *);
static bool read_reg(uint8_t address,uint8_t *data,size_t size)
{return pmic && i2c_master_transmit_receive(pmic,&address,1,data,size,50)==ESP_OK;}
static void sample_power(void)
{
    ouse_power_state_t state={.available=pmic!=NULL,.percent=-1,.error=ESP_ERR_NOT_FOUND};
    uint8_t s0,s1,soc,voltage[2];
    if(read_reg(0,&s0,1) && read_reg(1,&s1,1)) {
        state.valid=true;state.error=ESP_OK;state.present=(s0&8)!=0;state.external=(s0&32)!=0;
        // AXP2101 status1[7:5]: 1 charging / 2 discharging / 0 standby.
        // Low bits are charge-phase status (4 means charge done).
        state.charging=state.present && ((s1>>5)&3)==1;
        state.full=state.present && (s1&7)==4;
        if(state.present && read_reg(0xa4,&soc,1) && soc<=100)state.percent=soc;
        if(state.present && read_reg(0x34,voltage,2)) {
            int mv=((voltage[0]&63)<<8)|voltage[1];if(mv>=2500 && mv<=4500)state.millivolts=mv;
        }
    } else if(pmic)state.error=ESP_ERR_TIMEOUT;
    ouse_devices_publish_power(&state);
}
static esp_err_t store(const ouse_device_config_t *config)
{
    nvs_handle_t h;esp_err_t e=nvs_open("ouse_devices",NVS_READWRITE,&h);if(e!=ESP_OK)return e;
    e=nvs_set_u16(h,"sleep",config->sleep_seconds);
    if(e==ESP_OK)e=nvs_set_u8(h,"volume",config->volume);
    if(e==ESP_OK)e=nvs_set_u8(h,"muted",config->muted);
    if(e==ESP_OK)e=nvs_set_u8(h,"wifi",config->wifi_enabled);
    if(e==ESP_OK)e=nvs_set_str(h,"ssid",config->ssid);
    if(e==ESP_OK)e=nvs_set_str(h,"password",config->password);
    if(e==ESP_OK)e=nvs_commit(h);
    nvs_close(h);return e;
}
static void power_worker(void *argument)
{
    (void)argument;
    i2c_master_bus_handle_t bus=bsp_i2c_get_handle();
    if(bus && i2c_master_probe(bus,0x34,50)==ESP_OK) {
        i2c_device_config_t cfg={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x34,.scl_speed_hz=100000};
        esp_err_t e=i2c_master_bus_add_device(bus,&cfg,&pmic);if(e!=ESP_OK)ESP_LOGW("OusePower","PMIC: %s",esp_err_to_name(e));
    }
    bool pending=false;
    int64_t next_sample=0,save_after=0;
    ouse_device_config_t config={0};uint32_t id=0;
    for(;;) {
        int64_t now=esp_timer_get_time();
        if(now>=next_sample){sample_power();next_sample=now+2000000;}
        if(ouse_devices_take_save(&config,&id)) {
            // Wait for a quiet interval; continuous dragging does not write
            // Flash and does not delay the independently timed PMIC sample.
            pending=true;save_after=now+300000;
        }
        if(pending && now>=save_after) {
            esp_err_t e=store(&config);ouse_devices_saved(id,e);
            if(e!=ESP_OK)ESP_LOGW("OuseDevices","Settings persistence: %s",esp_err_to_name(e));
            memset(config.password,0,sizeof(config.password));pending=false;
        }
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(pending ? 200 : 2000));
    }
}
esp_err_t ouse_board_devices_init(void)
{
    ouse_device_config_t config={.sleep_seconds=60,.volume=40};
    nvs_handle_t h;esp_err_t e=nvs_open("ouse_devices",NVS_READONLY,&h);
    if(e==ESP_OK) {
        uint16_t timeout;uint8_t volume,muted,wifi;size_t n=sizeof(config.ssid);
        if(nvs_get_u16(h,"sleep",&timeout)==ESP_OK && (!timeout||timeout==15||timeout==30||timeout==60||timeout==120||timeout==300))config.sleep_seconds=timeout;
        if(nvs_get_u8(h,"volume",&volume)==ESP_OK && volume<=100)config.volume=volume;
        if(nvs_get_u8(h,"muted",&muted)==ESP_OK)config.muted=muted==1;
        if(nvs_get_u8(h,"wifi",&wifi)==ESP_OK)config.wifi_enabled=wifi==1;
        if(nvs_get_str(h,"ssid",config.ssid,&n)!=ESP_OK)config.ssid[0]=0;
        n=sizeof(config.password);if(nvs_get_str(h,"password",config.password,&n)!=ESP_OK)config.password[0]=0;
        nvs_close(h);
    }
    ouse_device_port_t port={.lock=lock,.unlock=unlock,.notify=notify};
    e=ouse_devices_init(&port,&config);memset(config.password,0,sizeof(config.password));return e;
}
void ouse_board_start_services(void)
{
    if(xTaskCreate(power_worker,"ouse_power",4096,NULL,2,&workers[0])!=pdPASS)ouse_devices_set_available(0,false,ESP_ERR_NO_MEM);
    bool audio=xTaskCreate(ouse_board_audio_worker,"ouse_audio",8192,NULL,3,&workers[1])==pdPASS;
    ouse_devices_set_available(1,audio,audio ? ESP_OK : ESP_ERR_NO_MEM);
    bool wifi=xTaskCreate(ouse_board_wifi_worker,"ouse_wifi",16384,NULL,3,&workers[2])==pdPASS;
    ouse_devices_set_available(2,wifi,wifi ? ESP_OK : ESP_ERR_NO_MEM);
    ouse_device_config_t config;ouse_devices_get_config(&config);
    if(wifi && config.wifi_enabled)(void)ouse_wifi_set_enabled(true);
    memset(config.password,0,sizeof(config.password));
}
