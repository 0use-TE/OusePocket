#pragma once
#include "lvgl.h"
void ouse_device_ui_build(lv_obj_t *page,unsigned index);
void ouse_device_ui_refresh(void);
void ouse_device_ui_close(void);
bool ouse_device_ui_modal(void);
void ouse_device_ui_close_modal(void);
