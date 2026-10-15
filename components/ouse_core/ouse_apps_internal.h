#pragma once
#include "ouse_app.h"
esp_err_t ouse_apps_init(lv_obj_t *host, void (*changed)(bool active));
esp_err_t ouse_apps_validate(const ouse_app_definition_t *app);
esp_err_t ouse_apps_register(const ouse_app_definition_t *app);
void ouse_apps_seal(void);
