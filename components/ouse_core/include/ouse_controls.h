// SPDX-License-Identifier: MIT
#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
// UI task/lock only. Complete switch skin for a build without an LVGL theme.
// Leaves size, state and event ownership to the caller. Apply once at creation;
// shared semantic colors continue to follow the system theme.
void ouse_control_skin_switch(lv_obj_t *object);
#ifdef __cplusplus
}
#endif
