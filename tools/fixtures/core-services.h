#include "ouse_usb.h"
#include "ouse_ble.h"
#include "ouse_storage.h"
#include "ouse_ime.h"
#include "ouse_clock.h"
#include "ouse_files.h"
#include "ouse_permissions.h"
#include "ouse_notifications.h"
#include "ouse_app_manager_ui.h"
#include "ouse_app_manager.h"
#include "ouse_health_ui.h"
#include "ouse_health.h"
#include <string.h>
#include <stdlib.h>
#include "identity-services.h"
static char *host_launcher_order;
static bool host_launcher_save_error;
static char *host_file_defaults;
static bool host_files_ready;
static unsigned host_control_requests;
static ouse_control_action_t host_control_action;
esp_err_t ouse_control_request(ouse_control_action_t action,const char *id,uint32_t task,uint32_t instance)
{(void)id;(void)task;(void)instance;++host_control_requests;host_control_action=action;return ESP_OK;}
bool ouse_sd_browser_path(const char *path,char *absolute,size_t capacity)
{if(!path || path[0]!='/' || strstr(path,"..") || strchr(path,'\\'))return false;int size=snprintf(absolute,capacity,"/sdcard%s",path);return size>0 && (size_t)size<capacity;}
esp_err_t ouse_storage_file_defaults(bool write,const char *value,ouse_storage_complete_t complete,void *context)
{if(write){free(host_file_defaults);host_file_defaults=_strdup(value);}complete(context,0,ESP_OK,write ? NULL : _strdup(host_file_defaults ? host_file_defaults : ""));return ESP_OK;}
static unsigned host_uninstall_requests;
static char host_uninstall_target[48];
void ouse_manager_get(ouse_manager_state_t *out){out->completed_id=host_uninstall_requests;out->error=ESP_OK;}
void ouse_manager_release(ouse_manager_state_t *snapshot){free(snapshot->apps);memset(snapshot,0,sizeof(*snapshot));}
esp_err_t ouse_manager_request(ouse_manager_action_t action,const char *id,uint32_t *request_id)
{if(action!=OUSE_MANAGER_UNINSTALL || !id || strncmp(id,"user.",5))return ESP_ERR_INVALID_ARG;strcpy(host_uninstall_target,id);*request_id=++host_uninstall_requests;return ESP_OK;}
static ouse_ble_state_t host_ble={.available=true};
void ouse_ble_get(ouse_ble_state_t *out){*out=host_ble;}
esp_err_t ouse_ble_enable(bool enabled){host_ble.enabled=enabled;host_ble.ready=enabled;host_ble.advertising=enabled;++host_ble.revision;return ESP_OK;}
esp_err_t ouse_ble_scan(void){host_ble.scanning=true;++host_ble.revision;return ESP_OK;}
esp_err_t ouse_ble_discoverable(void){host_ble.advertising=true;++host_ble.revision;return ESP_OK;}
esp_err_t ouse_ble_disconnect(void){host_ble.connected=false;++host_ble.revision;return ESP_OK;}
esp_err_t ouse_ble_connect(const char *address,uint8_t type){(void)type;strcpy(host_ble.peer,address);host_ble.connected=true;++host_ble.revision;return ESP_OK;}
esp_err_t ouse_ble_forget(const char *address,uint8_t type){(void)address;(void)type;host_ble.count=0;host_ble.connected=false;++host_ble.revision;return ESP_OK;}
esp_err_t ouse_ble_confirm(uint32_t id,bool accept){if(id!=host_ble.pair_id)return ESP_ERR_INVALID_ARG;host_ble.pair_pending=false;host_ble.bonded=accept;++host_ble.revision;return ESP_OK;}
esp_err_t ouse_storage_get_stats(ouse_storage_stats_t *out){memset(out,0,sizeof(*out));out->ready=true;return ESP_OK;}
esp_err_t ouse_storage_launcher(bool write,const char *value,ouse_storage_complete_t complete,void *context)
{
    esp_err_t error=write && host_launcher_save_error ? ESP_FAIL : ESP_OK;
    if(write && error==ESP_OK){free(host_launcher_order);host_launcher_order=_strdup(value);}
    complete(context,0,error,write ? NULL : _strdup(host_launcher_order ? host_launcher_order : ""));return ESP_OK;
}
static bool host_take_wifi(ouse_wifi_request_t *request)
{
    if(!ouse_devices_take_wifi(request))return false;
    (void)ouse_task_start(request->id,true);ouse_task_finish(request->id,ESP_OK,false);return true;
}
bool ouse_camera_media_active(void){return false;}
static ouse_usb_state_t host_usb;
void ouse_usb_get(ouse_usb_state_t *out){*out=host_usb;}
esp_err_t ouse_usb_set_enabled(bool enabled){host_usb.enabled=enabled;return ESP_OK;}
esp_err_t ouse_usb_set_audio(bool enabled){(void)enabled;return ESP_OK;}
esp_err_t ouse_usb_set_network(bool enabled){(void)enabled;return ESP_OK;}
void ouse_clock_get(ouse_clock_state_t *out){memset(out,0,sizeof(*out));}
static bool host_debug;
esp_err_t ouse_system_request_debug(bool enabled){host_debug=enabled;return ESP_OK;}
bool ouse_system_debug_enabled(void){return host_debug;}
esp_err_t ouse_system_request_navigation(uint8_t mode){(void)mode;return ESP_OK;}
void ouse_files_get(ouse_files_state_t *out){memset(out,0,sizeof(*out));out->ready=out->present=host_files_ready;}
esp_err_t ouse_files_eject(void){return ESP_ERR_INVALID_STATE;}
esp_err_t ouse_files_recover(void){return ESP_ERR_INVALID_STATE;}
void ouse_health_ui_build(lv_obj_t *root){(void)root;}
void ouse_health_ui_refresh(void){}
void ouse_health_ui_pause(void){}
void ouse_health_ui_close(void){}
void ouse_manager_ui_build(lv_obj_t *root,void (*back)(void)){(void)root;(void)back;}
void ouse_manager_ui_update(void){}
void ouse_manager_ui_refresh(void){}
bool ouse_manager_ui_back(void){return false;}
void ouse_manager_ui_release_icons(void){}
void ouse_manager_ui_close(void){}
esp_err_t ouse_permissions_init(void){return ESP_OK;}
esp_err_t ouse_permissions_set(const char *app,ouse_permission_t permission,ouse_permission_state_t state){(void)app;(void)permission;(void)state;return ESP_OK;}
bool ouse_permissions_next(ouse_permission_prompt_t *out){(void)out;return false;}
bool ouse_permissions_prompt_current(uint32_t instance){(void)instance;return false;}
esp_err_t ouse_notifications_init(void){return ESP_OK;}
unsigned ouse_notifications_list(const char *app,ouse_notification_t *out,unsigned capacity){(void)app;(void)out;(void)capacity;return 0;}
uint32_t ouse_notifications_generation(void){return 0;}
bool ouse_notifications_latest(ouse_notification_t *out){(void)out;return false;}
esp_err_t ouse_notifications_dismiss(uint32_t serial){(void)serial;return ESP_ERR_NOT_FOUND;}
unsigned ouse_notifications_clear_all(void){return 0;}
void ouse_notifications_remove_app(const char *app){(void)app;}
esp_err_t ouse_notifications_activate(uint32_t serial,char *app,unsigned size,uint32_t *token){(void)serial;(void)app;(void)size;(void)token;return ESP_ERR_NOT_FOUND;}
void ouse_notifications_discard_open(const char *app,uint32_t token){(void)app;(void)token;}
const char *esp_err_to_name(int error){(void)error;return "host error";}
