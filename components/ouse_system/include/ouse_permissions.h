#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
typedef enum {OUSE_PERMISSION_MICROPHONE,OUSE_PERMISSION_CAMERA,OUSE_PERMISSION_DELETE,OUSE_PERMISSION_BLUETOOTH,OUSE_PERMISSION_HID,OUSE_PERMISSION_COUNT} ouse_permission_t;
typedef enum {OUSE_PERMISSION_ASK,OUSE_PERMISSION_ALLOW,OUSE_PERMISSION_DENY} ouse_permission_state_t;
typedef struct {char app_id[48];uint32_t instance;ouse_permission_t permission;} ouse_permission_prompt_t;
esp_err_t ouse_permissions_init(void);
ouse_permission_state_t ouse_permissions_get(const char *app_id,ouse_permission_t permission);
esp_err_t ouse_permissions_set(const char *app_id,ouse_permission_t permission,ouse_permission_state_t state);
esp_err_t ouse_permissions_request(const char *app_id,uint32_t instance,ouse_permission_t permission);
bool ouse_permissions_next(ouse_permission_prompt_t *prompt);
void ouse_permissions_cancel(uint32_t instance);
uint32_t ouse_permissions_revision(void);
bool ouse_permissions_prompt_current(uint32_t instance);
esp_err_t ouse_permissions_purge(const char *app_id);
