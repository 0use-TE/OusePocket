// SPDX-License-Identifier: MIT
#pragma once
#include "lvgl.h"
#include "esp_err.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// All operations run on the UI task / under the UI lock. Logical density is a
// board setting. System text scale is fixed at 100%; font_percent accepts only
// 100 (or the init default 0). Individual text roles retain their pixel sizes.
esp_err_t ouse_ui_init(unsigned density_dpi, unsigned locale, unsigned font_percent);
esp_err_t ouse_ui_set_preferences(unsigned locale, unsigned font_percent);
int ouse_ui_dp(int value);
int ouse_ui_to_dp(int pixels);
const lv_font_t *ouse_ui_font(unsigned sp);
unsigned ouse_ui_density(void);
unsigned ouse_ui_locale(void);
unsigned ouse_ui_font_percent(void);
uint32_t ouse_ui_revision(unsigned group); // 0 theme, 1 locale, 2 display
uint32_t ouse_ui_generation(void);
void ouse_ui_theme_changed(void);
void ouse_ui_display_changed(void);
const char *ouse_ui_tr(const char *source);
void ouse_ui_text(lv_obj_t *label, const char *source);
void ouse_ui_text_pair(lv_obj_t *label, const char *zh, const char *en);
void ouse_ui_unbind_text(lv_obj_t *label);
void ouse_ui_use_font(lv_obj_t *object, unsigned sp);
unsigned ouse_ui_binding_count(void);
#ifdef __cplusplus
}
#endif
