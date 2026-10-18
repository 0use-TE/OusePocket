#pragma once
#include "lvgl.h"
void ouse_manager_ui_build(lv_obj_t *root,void (*back)(void));
void ouse_manager_ui_update(void);
void ouse_manager_ui_refresh(void);
bool ouse_manager_ui_back(void);
void ouse_manager_ui_release_icons(void);
void ouse_manager_ui_close(void);
