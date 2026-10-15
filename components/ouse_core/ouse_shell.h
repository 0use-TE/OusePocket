#pragma once
#include "ouse_core.h"
typedef struct {
    lv_obj_t *screen;
    lv_indev_t *touch;
    uint8_t (*brightness)(void);
    esp_err_t (*set_brightness)(uint8_t);
    esp_err_t (*save_brightness)(void);
    esp_err_t (*settings_state)(ouse_settings_state_t *);
    void (*back)(void);
} ouse_shell_config_t;
void ouse_shell_init(const ouse_shell_config_t *config);
void ouse_shell_foreground(void);
