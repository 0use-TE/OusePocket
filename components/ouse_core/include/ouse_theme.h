// SPDX-License-Identifier: Apache-2.0
// Palette values from theme/colors.json (tools/gen-theme-colors.py).
#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { OUSE_THEME_LIGHT = 0, OUSE_THEME_DARK = 1 } ouse_theme_t;
typedef struct {
    uint32_t background, desktop, surface, text, secondary, accent, button,
             pressed, icon, border, disabled, error;
} ouse_theme_colors_t;
ouse_theme_colors_t ouse_theme_colors(void);
typedef enum {
    OUSE_STYLE_DESKTOP, OUSE_STYLE_APP, OUSE_STYLE_PANEL, OUSE_STYLE_BUTTON,
    OUSE_STYLE_PRESSED, OUSE_STYLE_MUTED, OUSE_STYLE_ACCENT, OUSE_STYLE_TRACK,
    OUSE_STYLE_PILL, OUSE_STYLE_ICON, OUSE_STYLE_ACCENT_TEXT, OUSE_STYLE_COUNT
} ouse_style_role_t;
// UI task/lock only. Shared styles also update cached app instances in place.
void ouse_theme_set(ouse_theme_t theme);
ouse_theme_t ouse_theme_get(void);
void ouse_theme_apply(lv_obj_t *object, ouse_style_role_t role, lv_style_selector_t selector);
#ifdef __cplusplus
}
#endif
