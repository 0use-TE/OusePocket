#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum {OUSE_MANAGER_REFRESH,OUSE_MANAGER_CLEAR,OUSE_MANAGER_UNINSTALL,OUSE_MANAGER_RETRY} ouse_manager_action_t;
typedef struct {
    char id[48],name[48],name_zh[48],name_en[48];
    size_t package_bytes,data_bytes;unsigned alarms;bool system,measured;
    unsigned permissions;
    uint32_t version_code;
    char version_name[32];
    char description[513],author[65],released_at[11];
} ouse_managed_app_t;
typedef struct {
    bool ready,busy,recovery_pending;unsigned count;
    uint32_t revision,catalog_revision,request_id,completed_id;ouse_manager_action_t action;
    char target[48];esp_err_t error;size_t total_bytes,used_bytes,free_bytes;
    ouse_managed_app_t *apps;
    unsigned capacity;
} ouse_manager_state_t;
typedef struct {ouse_manager_action_t action;uint32_t id;char target[48];} ouse_manager_request_t;
esp_err_t ouse_manager_init(void);
void ouse_manager_get(ouse_manager_state_t *out);
void ouse_manager_release(ouse_manager_state_t *snapshot);
esp_err_t ouse_manager_request(ouse_manager_action_t action,const char *id,uint32_t *request_id);
bool ouse_manager_pending(void);
bool ouse_manager_take(ouse_manager_request_t *out);
void ouse_manager_complete(const ouse_manager_request_t *request,esp_err_t error);
void ouse_manager_provider(bool ready,bool busy,bool recovery);
void ouse_manager_publish(const ouse_manager_state_t *snapshot);
