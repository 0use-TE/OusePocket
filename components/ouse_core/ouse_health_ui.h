// SPDX-License-Identifier: MIT
#pragma once
#include "lvgl.h"
void ouse_health_ui_build(lv_obj_t *parent);
void ouse_health_ui_refresh(void);
// UI task: release diagnostic cards before another app allocates native buffers.
void ouse_health_ui_pause(void);
void ouse_health_ui_close(void);
