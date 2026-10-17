// SPDX-License-Identifier: MIT
#include "ouse_widgets.h"
#include "ouse_controls.h"
#include <string.h>

lv_obj_t *ouse_widget_box(lv_obj_t *parent,ouse_style_role_t role,int radius)
{
    lv_obj_t *obj=lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj,LV_OBJ_FLAG_CLICKABLE);
    ouse_theme_apply(obj,role,0);
    lv_obj_set_style_radius(obj,ouse_ui_dp(radius),0);
    lv_obj_set_style_pad_all(obj,0,0);
    return obj;
}
lv_obj_t *ouse_widget_text(lv_obj_t *parent,const char *text,unsigned sp)
{
    lv_obj_t *obj=lv_label_create(parent);
    lv_obj_remove_flag(obj,LV_OBJ_FLAG_CLICKABLE);
    ouse_ui_text(obj,text);
    ouse_ui_use_font(obj,sp);
    lv_label_set_long_mode(obj,LV_LABEL_LONG_WRAP);
    return obj;
}
static void button_caption_limit(lv_obj_t *button)
{
    if(!lv_obj_get_child_count(button))return;
    lv_obj_t *parent=lv_obj_get_parent(button),*caption=lv_obj_get_child(button,0);
    int available=lv_obj_get_content_width(parent);
    if(available<=0)available=lv_display_get_horizontal_resolution(lv_obj_get_display(button));
    int maximum=lv_obj_get_style_max_width(button,0),width=lv_obj_get_style_width(button,0);
    if(LV_COORD_IS_PCT(maximum))maximum=lv_pct_to_px(maximum,available);
    if(maximum<available)available=maximum;
    if(width!=LV_SIZE_CONTENT) {
        if(LV_COORD_IS_PCT(width))width=lv_pct_to_px(width,lv_obj_get_content_width(parent));
        else if(!width)width=lv_obj_get_width(button); // assigned by flex
        if(width>0 && width<available)available=width;
    }
    available-=lv_obj_get_style_pad_left(button,0)+lv_obj_get_style_pad_right(button,0);
    if(available<1)available=1;
    // A percentage here would depend on this content-sized button's own
    // caption, collapsing both to the minimum width. Bound it in pixels.
    if(lv_obj_get_style_max_width(caption,0)!=available)lv_obj_set_style_max_width(caption,available,0);
}
static void button_layout(lv_event_t *event)
{
    lv_event_code_t code=lv_event_get_code(event);
    if(code==LV_EVENT_SIZE_CHANGED || code==LV_EVENT_STYLE_CHANGED)button_caption_limit(lv_event_get_current_target_obj(event));
}
lv_obj_t *ouse_widget_button(lv_obj_t *parent,const char *text,lv_event_cb_t callback,void *data)
{
    lv_obj_t *obj=lv_button_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_width(obj,LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(obj,LV_PCT(100),0);
    lv_obj_set_height(obj,LV_SIZE_CONTENT);
    lv_obj_set_style_min_width(obj,ouse_ui_dp(40),0);
    lv_obj_set_style_min_height(obj,ouse_ui_dp(40),0);
    lv_obj_set_style_radius(obj,ouse_ui_dp(12),0);
    lv_obj_set_style_pad_hor(obj,ouse_ui_dp(14),0);
    lv_obj_set_style_pad_ver(obj,ouse_ui_dp(5),0);
    ouse_theme_apply(obj,OUSE_STYLE_BUTTON,0);
    ouse_theme_apply(obj,OUSE_STYLE_PRESSED,LV_STATE_PRESSED);
    if(callback)lv_obj_add_event_cb(obj,callback,LV_EVENT_CLICKED,data);
    lv_obj_t *caption=ouse_widget_text(obj,text,16);
    lv_obj_set_width(caption,LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(caption,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_center(caption);
    lv_obj_add_event_cb(obj,button_layout,LV_EVENT_ALL,NULL);button_caption_limit(obj);
    return obj;
}
void ouse_widget_button_expand(lv_obj_t *button)
{
    lv_obj_set_width(button,LV_PCT(100));
    lv_obj_set_width(lv_obj_get_child(button,0),LV_PCT(100));
}
lv_obj_t *ouse_widget_icon(lv_obj_t *parent,const char *symbol,int size,uint32_t color)
{
    lv_obj_t *box=ouse_widget_box(parent,OUSE_STYLE_PANEL,14);
    lv_obj_set_size(box,ouse_ui_dp(size),ouse_ui_dp(size));
    lv_obj_set_style_bg_color(box,lv_color_hex(color),0);
    lv_obj_set_style_bg_opa(box,LV_OPA_COVER,0);
    lv_obj_t *glyph=lv_label_create(box);
    lv_obj_remove_flag(glyph,LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(glyph,symbol);
    lv_obj_set_style_text_font(glyph,&lv_font_montserrat_24,0);
    lv_obj_set_style_text_color(glyph,lv_color_white(),0);
    int scale=ouse_ui_dp(size<40 ? 192 : 256);
    lv_obj_set_style_transform_pivot_x(glyph,LV_PCT(50),0);
    lv_obj_set_style_transform_pivot_y(glyph,LV_PCT(50),0);
    lv_obj_set_style_transform_scale_x(glyph,scale,0);
    lv_obj_set_style_transform_scale_y(glyph,scale,0);
    lv_obj_center(glyph);
    return box;
}
uint32_t ouse_widget_app_color(const char *id)
{
    if(!strcmp(id,"ouse.settings"))return 0x367BEF;
    if(!strcmp(id,"ouse.counter"))return 0x219F79;
    if(!strcmp(id,"ouse.resources"))return 0x8657D9;
    if(!strcmp(id,"ouse.api-test"))return 0xE18736;
    return 0x60748E;
}
lv_obj_t *ouse_widget_app_icon(lv_obj_t *parent,const ouse_app_info_t *info,int size)
{
    if(!info->icon_image)return ouse_widget_icon(parent,info->icon,size,ouse_widget_app_color(info->id));
    // The application supplies all icon pixels, including alpha. Keep a fixed
    // dp box and preserve aspect ratio without adding the system symbol/tile.
    lv_obj_t *image=lv_image_create(parent);
    lv_obj_remove_style_all(image);
    lv_obj_remove_flag(image,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    lv_image_set_src(image,info->icon_image);
    lv_obj_set_size(image,ouse_ui_dp(size),ouse_ui_dp(size));
    lv_image_set_inner_align(image,LV_IMAGE_ALIGN_CONTAIN);
    lv_image_set_antialias(image,true);
    return image;
}
void ouse_widget_app_name(lv_obj_t *label,const ouse_app_info_t *info)
{
    const char *fallback=info->default_name[0] ? info->default_name : info->name;
    if(info->name_zh[0] || info->name_en[0] || !strncmp(info->id,"user.",5))
        ouse_ui_text_pair(label,info->name_zh[0] ? info->name_zh : fallback,info->name_en[0] ? info->name_en : fallback);
    else ouse_ui_text(label,fallback);
}
lv_obj_t *ouse_widget_row(lv_obj_t *parent,const char *icon,const char *title,const char *detail,
                          lv_event_cb_t callback,void *data,lv_obj_t **value)
{
    lv_obj_t *row=lv_button_create(parent);
    lv_obj_remove_style_all(row);
    ouse_theme_apply(row,OUSE_STYLE_PANEL,0);
    ouse_theme_apply(row,OUSE_STYLE_PRESSED,LV_STATE_PRESSED);
    lv_obj_set_width(row,LV_PCT(100));lv_obj_set_height(row,LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row,ouse_ui_dp(detail ? 64 : 52),0);
    lv_obj_set_style_radius(row,ouse_ui_dp(12),0);
    lv_obj_set_style_pad_hor(row,ouse_ui_dp(12),0);
    lv_obj_set_style_pad_ver(row,ouse_ui_dp(10),0);
    lv_obj_set_style_pad_column(row,ouse_ui_dp(12),0);
    lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    if(callback)lv_obj_add_event_cb(row,callback,LV_EVENT_CLICKED,data);
    if(icon) {
        lv_obj_t *symbol=lv_label_create(row);
        lv_label_set_text(symbol,icon);lv_obj_remove_flag(symbol,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_font(symbol,&lv_font_montserrat_24,0);
        ouse_theme_apply(symbol,OUSE_STYLE_ACCENT_TEXT,0);
        lv_obj_set_style_transform_scale_x(symbol,ouse_ui_dp(256),0);
        lv_obj_set_style_transform_scale_y(symbol,ouse_ui_dp(256),0);
        lv_obj_set_style_margin_right(symbol,ouse_ui_dp(4),0);
    }
    lv_obj_t *copy=ouse_widget_box(row,OUSE_STYLE_PANEL,0);
    lv_obj_set_style_bg_opa(copy,LV_OPA_TRANSP,0);
    lv_obj_set_flex_grow(copy,1);lv_obj_set_width(copy,0);lv_obj_set_height(copy,LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(copy,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(copy,ouse_ui_dp(4),0);
    lv_obj_t *label=ouse_widget_text(copy,title,16);lv_obj_set_width(label,LV_PCT(100));
    label=ouse_widget_text(copy,detail ? detail : "",13);lv_obj_set_width(label,LV_PCT(100));
    if(!detail)lv_obj_add_flag(label,LV_OBJ_FLAG_HIDDEN);
    ouse_theme_apply(label,OUSE_STYLE_MUTED,0);
    if(value)*value=label;
    lv_obj_t *arrow=lv_label_create(row);lv_label_set_text(arrow,LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(arrow,&lv_font_montserrat_16,0);
    lv_obj_remove_flag(arrow,LV_OBJ_FLAG_CLICKABLE);
    ouse_theme_apply(arrow,OUSE_STYLE_MUTED,0);
    return row;
}
void ouse_widget_slider(lv_obj_t *slider,int height)
{
    lv_obj_set_height(slider,ouse_ui_dp(height));
    lv_obj_set_style_border_width(slider,0,LV_PART_MAIN);
    lv_obj_set_style_outline_width(slider,0,LV_PART_MAIN);
    lv_obj_set_style_radius(slider,LV_RADIUS_CIRCLE,LV_PART_MAIN);
    lv_obj_set_style_radius(slider,LV_RADIUS_CIRCLE,LV_PART_INDICATOR);
    ouse_theme_apply(slider,OUSE_STYLE_TRACK,LV_PART_MAIN);
    ouse_theme_apply(slider,OUSE_STYLE_ACCENT,LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider,LV_OPA_TRANSP,LV_PART_KNOB);
    lv_obj_set_style_border_width(slider,0,LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider,0,LV_PART_KNOB);
    lv_obj_set_style_outline_width(slider,0,LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider,0,LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider,ouse_ui_dp(8));
}
void ouse_control_skin_switch(lv_obj_t *sw)
{
    ouse_theme_apply(sw,OUSE_STYLE_TRACK,0);ouse_theme_apply(sw,OUSE_STYLE_TRACK,LV_PART_INDICATOR);
    ouse_theme_apply(sw,OUSE_STYLE_ACCENT,LV_PART_INDICATOR|LV_STATE_CHECKED);
    const lv_part_t parts[]={LV_PART_MAIN,LV_PART_INDICATOR,LV_PART_KNOB};
    for(unsigned i=0;i<3;++i) {
        lv_obj_set_style_radius(sw,LV_RADIUS_CIRCLE,parts[i]);
        lv_obj_set_style_border_width(sw,0,parts[i]);lv_obj_set_style_outline_width(sw,0,parts[i]);
        lv_obj_set_style_shadow_width(sw,0,parts[i]);lv_obj_set_style_pad_all(sw,0,parts[i]);
    }
    lv_obj_set_style_bg_color(sw,lv_color_white(),LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sw,LV_OPA_COVER,LV_PART_KNOB);
    lv_obj_set_style_pad_all(sw,-ouse_ui_dp(3),LV_PART_KNOB);
    lv_obj_set_style_anim_duration(sw,150,0);
    lv_obj_set_style_opa(sw,LV_OPA_50,LV_STATE_DISABLED);
    // Keep the compact visual track, with at least a 40dp-high touch target.
    lv_obj_set_ext_click_area(sw,ouse_ui_dp(6));
}
lv_obj_t *ouse_widget_switch(lv_obj_t *parent)
{
    lv_obj_t *sw=lv_switch_create(parent);lv_obj_remove_style_all(sw);
    lv_obj_set_size(sw,ouse_ui_dp(48),ouse_ui_dp(28));
    ouse_control_skin_switch(sw);
    return sw;
}
