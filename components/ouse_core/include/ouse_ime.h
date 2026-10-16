// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OUSE_IME_RESIZE = 0,
    OUSE_IME_PAN,
    OUSE_IME_OVERLAY
} ouse_ime_avoid_t;

typedef enum {
    OUSE_IME_TEXT = 0,
    OUSE_IME_NUMBER,
    OUSE_IME_PASSWORD
} ouse_ime_type_t;

typedef struct {
    bool visible;
    ouse_ime_avoid_t mode;
    ouse_ime_type_t keyboard_type;
    unsigned attachments;
    int keyboard_height, viewport_height;
    lv_area_t input_bounds, viewport_bounds, keyboard_bounds;
} ouse_ime_state_t;

// UI task / UI lock only. The system owns one keyboard. A viewport is an app's
// content box, containing the textarea. Resize is the usual Android-style
// behavior; pan moves that box; overlay leaves its layout and scroll intact.
// Attachments are removed automatically when either LVGL object is deleted.
esp_err_t ouse_ime_attach(lv_obj_t *textarea, lv_obj_t *viewport,
                        ouse_ime_avoid_t mode, ouse_ime_type_t type);
void ouse_ime_detach(lv_obj_t *textarea);
void ouse_ime_focus(lv_obj_t *textarea);
bool ouse_ime_focused(const lv_obj_t *textarea);
bool ouse_ime_blur(lv_obj_t *textarea);
void ouse_ime_hide(void);
void ouse_ime_hide_root(lv_obj_t *root);
bool ouse_ime_active(void);
void ouse_ime_get_state(ouse_ime_state_t *state);

// The keyboard forwards LV_EVENT_READY / LV_EVENT_CANCEL to the textarea after
// restoring the viewport. The ordinary textarea VALUE_CHANGED event is intact.
// Text/password input supports English, numbers and symbols. A Chinese
// composition/candidate engine is deliberately outside this first phase.

#ifdef __cplusplus
}
#endif
