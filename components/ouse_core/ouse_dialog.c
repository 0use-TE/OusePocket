// SPDX-License-Identifier: MIT
#include "ouse_dialog.h"
#include "ouse_widgets.h"
#include <stdint.h>
static lv_obj_t *overlay,*card,*body,*accept,*cancel,*choices;
static ouse_dialog_action_t action;
static ouse_dialog_choice_t choice_action;
static void close_event(lv_event_t *e) {(void)e;ouse_dialog_dismiss();}
static void accept_event(lv_event_t *e) {(void)e;ouse_dialog_action_t callback=action;ouse_dialog_dismiss();if(callback)callback();}
static void choice_event(lv_event_t *e)
{
    unsigned index=(unsigned)(uintptr_t)lv_event_get_user_data(e);
    ouse_dialog_choice_t callback=choice_action;
    ouse_dialog_dismiss();if(callback)callback(index);
}
void ouse_dialog_init(lv_obj_t *screen)
{
    overlay=ouse_widget_box(screen,OUSE_STYLE_DESKTOP,0);lv_obj_set_size(overlay,LV_PCT(100),LV_PCT(100));
    lv_obj_set_style_bg_color(overlay,lv_color_black(),0);lv_obj_set_style_bg_opa(overlay,LV_OPA_50,0);
    lv_obj_add_flag(overlay,LV_OBJ_FLAG_CLICKABLE);
    card=ouse_widget_box(overlay,OUSE_STYLE_PANEL,20);lv_obj_set_width(card,LV_PCT(94));lv_obj_set_height(card,LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(card,LV_PCT(90),0);lv_obj_set_style_pad_all(card,ouse_ui_dp(16),0);
    lv_obj_set_flex_flow(card,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(card,ouse_ui_dp(12),0);lv_obj_center(card);
    body=ouse_widget_text(card,"",18);lv_obj_set_width(body,LV_PCT(100));
    choices=ouse_widget_box(card,OUSE_STYLE_PANEL,0);
    lv_obj_set_width(choices,LV_PCT(100));lv_obj_set_height(choices,LV_SIZE_CONTENT);
    lv_obj_add_flag(choices,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(choices,LV_DIR_VER);lv_obj_set_flex_flow(choices,LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(choices,LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *buttons=ouse_widget_box(card,OUSE_STYLE_PANEL,0);
    lv_obj_set_size(buttons,LV_PCT(100),LV_SIZE_CONTENT);lv_obj_set_flex_flow(buttons,LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons,LV_FLEX_ALIGN_END,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(buttons,ouse_ui_dp(8),0);
    cancel=ouse_widget_button(buttons,"取消",close_event,NULL);
    accept=ouse_widget_button(buttons,"确定",accept_event,NULL);
    lv_obj_set_style_max_width(cancel,LV_PCT(48),0);lv_obj_set_style_max_width(accept,LV_PCT(48),0);
    lv_obj_add_flag(overlay,LV_OBJ_FLAG_HIDDEN);
}
bool ouse_dialog_active(void) {return overlay && !lv_obj_has_flag(overlay,LV_OBJ_FLAG_HIDDEN);}
bool ouse_dialog_action_is(ouse_dialog_action_t callback){return ouse_dialog_active()&&action==callback;}
bool ouse_dialog_choice_active(void) {return ouse_dialog_active() && choice_action;}
void ouse_dialog_dismiss(void)
{
    if(overlay)lv_obj_add_flag(overlay,LV_OBJ_FLAG_HIDDEN);
    action=NULL;choice_action=NULL;if(choices)lv_obj_clean(choices);
}
void ouse_dialog_show(const char *message,const char *caption,ouse_dialog_action_t callback)
{
    if(!overlay)return;
    choice_action=NULL;lv_obj_clean(choices);lv_obj_add_flag(choices,LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(accept,LV_OBJ_FLAG_HIDDEN);lv_obj_center(card);
    action=callback;ouse_ui_text(body,message);ouse_ui_text(lv_obj_get_child(accept,0),caption ? caption : "确定");
    if(callback)lv_obj_remove_flag(cancel,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(cancel,LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(overlay,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(overlay);
}
void ouse_dialog_select(const char *title,const char *const *items,unsigned count,
                        unsigned selected,ouse_dialog_choice_t callback)
{
    if(!overlay || !items || !count || count>12 || !callback)return;
    action=NULL;choice_action=callback;lv_obj_clean(choices);ouse_ui_text(body,title);
    lv_obj_remove_flag(choices,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(accept,LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(cancel,LV_OBJ_FLAG_HIDDEN);
    int available=lv_display_get_vertical_resolution(NULL)-ouse_ui_dp(148);
    lv_obj_set_style_max_height(choices,available>ouse_ui_dp(44) ? available : ouse_ui_dp(44),0);
    for(unsigned i=0;i<count;++i){
        lv_obj_t *row=lv_button_create(choices);lv_obj_remove_style_all(row);
        lv_obj_set_size(row,LV_PCT(100),LV_SIZE_CONTENT);lv_obj_set_style_min_height(row,ouse_ui_dp(44),0);
        lv_obj_set_style_pad_all(row,ouse_ui_dp(8),0);lv_obj_set_style_radius(row,ouse_ui_dp(8),0);
        lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
        ouse_theme_apply(row,OUSE_STYLE_PANEL,0);ouse_theme_apply(row,OUSE_STYLE_PRESSED,LV_STATE_PRESSED);
        lv_obj_t *label=ouse_widget_text(row,items[i],16);lv_obj_set_width(label,0);lv_obj_set_flex_grow(label,1);
        label=lv_label_create(row);lv_label_set_text(label,i==selected ? LV_SYMBOL_OK : "");
        lv_obj_remove_flag(label,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_style_text_font(label,&lv_font_montserrat_16,0);
        ouse_theme_apply(label,OUSE_STYLE_ACCENT_TEXT,0);
        lv_obj_add_event_cb(row,choice_event,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
    }
    lv_obj_scroll_to_y(choices,0,LV_ANIM_OFF);
    lv_obj_align(card,LV_ALIGN_BOTTOM_MID,0,-ouse_ui_dp(12));
    lv_obj_remove_flag(overlay,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(overlay);
}

void ouse_dialog_resize(void)
{
    if(!card)return;
    lv_obj_set_width(card,LV_PCT(94));lv_obj_set_style_max_height(card,LV_PCT(90),0);
    lv_obj_add_flag(card,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(card,LV_DIR_VER);
    if(ouse_dialog_choice_active()){
        int height=lv_display_get_vertical_resolution(NULL)-ouse_ui_dp(148);
        lv_obj_set_style_max_height(choices,height>ouse_ui_dp(44)?height:ouse_ui_dp(44),0);
        lv_obj_align(card,LV_ALIGN_BOTTOM_MID,0,-ouse_ui_dp(12));
    }else lv_obj_center(card);
}
