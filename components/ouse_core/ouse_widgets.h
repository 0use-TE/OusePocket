// SPDX-License-Identifier: MIT
#pragma once
#include "ouse_core.h"
lv_obj_t *ouse_widget_box(lv_obj_t *parent,ouse_style_role_t role,int radius);
lv_obj_t *ouse_widget_text(lv_obj_t *parent,const char *text,unsigned sp);
lv_obj_t *ouse_widget_button(lv_obj_t *parent,const char *text,lv_event_cb_t callback,void *data);
// Ordinary buttons size to their caption; layouts opt into a complete row.
void ouse_widget_button_expand(lv_obj_t *button);
lv_obj_t *ouse_widget_row(lv_obj_t *parent,const char *icon,const char *title,const char *detail,
                          lv_event_cb_t callback,void *data,lv_obj_t **value);
lv_obj_t *ouse_widget_icon(lv_obj_t *parent,const char *symbol,int size,uint32_t color);
// App metadata controls its own translations and optional image; no JS runs.
lv_obj_t *ouse_widget_app_icon(lv_obj_t *parent,const ouse_app_info_t *info,int size);
void ouse_widget_app_name(lv_obj_t *label,const ouse_app_info_t *info);
uint32_t ouse_widget_app_color(const char *id);
void ouse_widget_slider(lv_obj_t *slider,int height);
lv_obj_t *ouse_widget_switch(lv_obj_t *parent);
