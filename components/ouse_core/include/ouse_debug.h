// SPDX-License-Identifier: MIT
#pragma once
#include "ouse_core.h"
#ifdef __cplusplus
extern "C" {
#endif
// UI task only; external tasks use ouse_core_post_ui.
esp_err_t ouse_debug_touch(int x,int y,bool pressed);
esp_err_t ouse_debug_home(void);
esp_err_t ouse_debug_open(const char *id);
esp_err_t ouse_debug_state(char *out,size_t capacity);
esp_err_t ouse_debug_tree(char *out,size_t capacity);
typedef struct { uint8_t *pixels;size_t size;int width,height; } ouse_debug_image_t;
esp_err_t ouse_debug_snapshot(ouse_debug_image_t *image);
void ouse_debug_snapshot_free(ouse_debug_image_t *image);
#ifdef __cplusplus
}
#endif

esp_err_t ouse_debug_touch_contact(unsigned slot,int x,int y,bool pressed);
