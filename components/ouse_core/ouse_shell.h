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
    void (*open_settings)(unsigned page);
    void (*navigation_changed)(int height);
} ouse_shell_config_t;
void ouse_shell_init(const ouse_shell_config_t *config);
void ouse_shell_foreground(void);
void ouse_shell_set_navigation(unsigned mode);
unsigned ouse_shell_navigation(void);
typedef struct {
    bool quick,recents,tracking,notifications;
    int quick_y,back_progress,recent_scroll_x;
    unsigned cards;
    unsigned notification_cards;
    size_t snapshot_bytes;
    int bar_offset;
} ouse_shell_state_t;
void ouse_shell_get_state(ouse_shell_state_t *state);
