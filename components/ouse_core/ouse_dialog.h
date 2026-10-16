#pragma once
#include "lvgl.h"
typedef void (*ouse_dialog_action_t)(void);
typedef void (*ouse_dialog_choice_t)(unsigned index);
void ouse_dialog_init(lv_obj_t *screen);
void ouse_dialog_show(const char *message,const char *action,ouse_dialog_action_t callback);
void ouse_dialog_select(const char *title,const char *const *items,unsigned count,
                        unsigned selected,ouse_dialog_choice_t callback);
bool ouse_dialog_choice_active(void);
bool ouse_dialog_active(void);
void ouse_dialog_dismiss(void);
