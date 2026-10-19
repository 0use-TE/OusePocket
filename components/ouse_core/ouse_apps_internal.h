#pragma once
#include "ouse_app.h"
esp_err_t ouse_apps_init(lv_obj_t *host, void (*changed)(bool active));
esp_err_t ouse_apps_validate(const ouse_app_definition_t *app);
esp_err_t ouse_apps_register(const ouse_app_definition_t *app);
void ouse_apps_seal(void);
// UI-only first-screen handshake. The script runtime owns this flag on its
// manager-owned root until the first model (or native fault page) is applied.
#define OUSE_APP_LOADING_FLAG LV_OBJ_FLAG_USER_1
lv_obj_t *ouse_apps_root(const char *id);
bool ouse_core_launch_active(void);
void ouse_core_launch_cancel(void);
void ouse_apps_swipe_offset(int offset);
void ouse_apps_resize(int height);
esp_err_t ouse_apps_background_fast(void);

esp_err_t ouse_apps_user_check(const char *id);
esp_err_t ouse_apps_user_apply(const ouse_app_definition_t *definition);
esp_err_t ouse_apps_user_remove(const char *id);
esp_err_t ouse_apps_user_maintenance(const char *id,bool enabled);
