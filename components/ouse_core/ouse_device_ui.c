// SPDX-License-Identifier: MIT
#include "ouse_device_ui.h"
#include "ouse_devices.h"
#include "ouse_usb.h"
#include "ouse_widgets.h"
#include "ouse_dialog.h"
#include "ouse_screen.h"
#include "ouse_ime.h"
#include "ouse_clock.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static struct {
    lv_obj_t *power,*timeout_value,*volume,*volume_value,*mute;
    lv_obj_t *wifi,*wifi_switch,*networks,*network_group,*available_heading,*scan,*empty;
    lv_obj_t *current_heading,*current,*current_title,*current_detail,*current_signal;
    lv_obj_t *popup,*form,*ssid,*password,*popup_title,*popup_note;
    lv_obj_t *details,*details_ssid,*details_ip,*details_signal,*details_time;
    lv_obj_t *usb_switch,*usb_network,*usb_audio_row,*usb_network_row;
    ouse_wifi_ap_t aps[OUSE_WIFI_AP_MAX];
    ouse_wifi_state_t wifi_state;
    unsigned ap_count;
    bool wifi_initialized,popup_password,popup_secured,scan_visit;
    uint32_t generation;
} ui;
static const unsigned timeouts[]={0,15,30,60,120,300};
static const char *timeout_labels[]={"从不","15 秒","30 秒","1 分钟","2 分钟","5 分钟"};
static void check(esp_err_t error) {if(error!=ESP_OK)ouse_dialog_show("操作失败，请重试",NULL,NULL);}
static void timeout_selected(unsigned index)
{
    if(index<sizeof(timeouts)/sizeof(timeouts[0]))check(ouse_power_set_timeout(timeouts[index]));
    ouse_device_ui_refresh();
}
static void timeout_event(lv_event_t *e)
{
    (void)e;ouse_power_state_t state={0};ouse_power_get(&state);unsigned selected=0;
    for(unsigned i=0;i<sizeof(timeouts)/sizeof(timeouts[0]);++i)if(timeouts[i]==state.sleep_seconds)selected=i;
    ouse_dialog_select("自动熄屏",timeout_labels,sizeof(timeouts)/sizeof(timeouts[0]),selected,timeout_selected);
}
static void sleep_event(lv_event_t *e) {(void)e;check(ouse_screen_sleep());}
static void volume_event(lv_event_t *e) {check(ouse_audio_set_volume(lv_slider_get_value(lv_event_get_target_obj(e))));}
static void usb_event(lv_event_t *e)
{
    (void)e;ouse_usb_state_t s;ouse_usb_get(&s);
    if(s.available && s.enabled && s.mounted && !s.suspended)check(ouse_usb_set_audio(!s.audio_enabled));
}
static void usb_network_event(lv_event_t *e)
{
    (void)e;ouse_usb_state_t s;ouse_usb_get(&s);
    if(s.available && s.enabled && s.mounted && !s.suspended &&
       (s.network_enabled || (s.uplink && !s.subnet_conflict)))check(ouse_usb_set_network(!s.network_enabled));
}
static void mute_event(lv_event_t *e) {(void)e;ouse_audio_state_t s;if(ouse_audio_get(&s)==ESP_OK)check(ouse_audio_set_muted(!s.muted));}
static void wifi_event(lv_event_t *e) {(void)e;ouse_wifi_state_t s;if(ouse_wifi_get(&s)==ESP_OK && s.available)check(ouse_wifi_set_enabled(!s.enabled));}
static void scan_event(lv_event_t *e) {(void)e;check(ouse_wifi_scan());}
static void disconnect_event(lv_event_t *e) {(void)e;ouse_device_ui_close_modal();check(ouse_wifi_disconnect());}
static void forget(void) {check(ouse_wifi_forget());}
static void forget_event(lv_event_t *e) {(void)e;ouse_device_ui_close_modal();ouse_dialog_show("忘记此网络及密码？","忘记网络",forget);}
void ouse_device_ui_close_modal(void)
{
    if(ouse_dialog_choice_active())ouse_dialog_dismiss();
    if(ui.popup){
        ouse_ime_hide_root(ui.popup);
        lv_textarea_set_text(ui.password,"");lv_obj_add_flag(ui.popup,LV_OBJ_FLAG_HIDDEN);
    }
    if(ui.details)lv_obj_add_flag(ui.details,LV_OBJ_FLAG_HIDDEN);
}
bool ouse_device_ui_modal(void) {return (ui.popup && lv_obj_is_visible(ui.popup)) || (ui.details && lv_obj_is_visible(ui.details));}
static void cancel_event(lv_event_t *e) {(void)e;ouse_device_ui_close_modal();}
static void connect_event(lv_event_t *e)
{
    (void)e;const char *password=ui.popup_password ? lv_textarea_get_text(ui.password) : "";
    esp_err_t error=ui.popup_secured && strlen(password)<8 ? ESP_ERR_INVALID_ARG : ouse_wifi_connect(lv_textarea_get_text(ui.ssid),password);
    if(error==ESP_OK)ouse_device_ui_close_modal();
    else ouse_dialog_show(error==ESP_ERR_INVALID_ARG ? "请检查网络名称和密码，密码至少 8 位" : "无法连接，请开启 Wi-Fi 后重试",NULL,NULL);
}
static void network_event(lv_event_t *e)
{
    unsigned index=(unsigned)(uintptr_t)lv_event_get_user_data(e);
    bool manual=!index;
    if(index>ui.ap_count)return;
    const ouse_wifi_ap_t *ap=manual ? NULL : &ui.aps[index-1];
    const char *ssid=ap ? ap->ssid : NULL;
    ui.popup_password=manual || ap->secured;ui.popup_secured=ap && ap->secured;
    ouse_ime_hide_root(ui.popup);
    lv_textarea_set_text(ui.ssid,ssid ? ssid : "");lv_textarea_set_text(ui.password,"");
    if(manual)lv_obj_remove_flag(ui.ssid,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(ui.ssid,LV_OBJ_FLAG_HIDDEN);
    if(ui.popup_password) {
        lv_obj_remove_flag(ui.password,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.popup_note,LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.password,LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui.popup_note,LV_OBJ_FLAG_HIDDEN);
    }
    if(ssid){ouse_ui_unbind_text(ui.popup_title);lv_label_set_text(ui.popup_title,ssid);}
    else ouse_ui_text(ui.popup_title,"添加网络");
    lv_obj_remove_flag(ui.popup,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(ui.popup);
    lv_obj_scroll_to_y(ui.form,0,LV_ANIM_OFF);
    if(manual || ui.popup_password)ouse_ime_focus(manual ? ui.ssid : ui.password);
}
static void build_popup(lv_obj_t *page)
{
    // Parent is the settings app root; app close owns the complete tree.
    lv_obj_t *root=lv_obj_get_parent(page);
    ui.popup=ouse_widget_box(root,OUSE_STYLE_DESKTOP,0);lv_obj_set_size(ui.popup,LV_PCT(100),LV_PCT(100));
    lv_obj_add_flag(ui.popup,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *form=ui.form=ouse_widget_box(ui.popup,OUSE_STYLE_PANEL,20);
    lv_obj_set_width(form,LV_PCT(94));lv_obj_align(form,LV_ALIGN_TOP_MID,0,ouse_ui_dp(8));
    lv_obj_set_height(form,LV_SIZE_CONTENT);lv_obj_set_style_max_height(form,LV_PCT(95),0);
    lv_obj_add_flag(form,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);lv_obj_set_scroll_dir(form,LV_DIR_VER);
    lv_obj_set_style_pad_all(form,ouse_ui_dp(12),0);lv_obj_set_style_pad_row(form,ouse_ui_dp(8),0);lv_obj_set_flex_flow(form,LV_FLEX_FLOW_COLUMN);
    ui.popup_title=ouse_widget_text(form,"",18);lv_obj_set_width(ui.popup_title,LV_PCT(100));
    ui.ssid=lv_textarea_create(form);ui.password=lv_textarea_create(form);
    lv_obj_t *fields[]={ui.ssid,ui.password};
    for(unsigned i=0;i<2;++i) {
        lv_obj_set_width(fields[i],LV_PCT(100));lv_obj_set_height(fields[i],ouse_ui_dp(44));
        lv_textarea_set_one_line(fields[i],true);lv_textarea_set_max_length(fields[i],i ? 64 : 32);
        (void)ouse_ime_attach(fields[i],ui.popup,OUSE_IME_RESIZE,i ? OUSE_IME_PASSWORD : OUSE_IME_TEXT);
        lv_obj_add_event_cb(fields[i],connect_event,LV_EVENT_READY,NULL);
        lv_obj_add_event_cb(fields[i],cancel_event,LV_EVENT_CANCEL,NULL);
        ouse_ui_use_font(fields[i],16);ouse_theme_apply(fields[i],OUSE_STYLE_BUTTON,0);
    }
    lv_textarea_set_placeholder_text(ui.ssid,"SSID");lv_textarea_set_placeholder_text(ui.password,"Password");
    lv_textarea_set_password_mode(ui.password,true);
    lv_textarea_set_password_bullet(ui.password,"*");
    ui.popup_note=ouse_widget_text(form,"开放网络，无需密码",14);lv_obj_set_width(ui.popup_note,LV_PCT(100));
    ouse_theme_apply(ui.popup_note,OUSE_STYLE_MUTED,0);
    lv_obj_t *row=ouse_widget_box(form,OUSE_STYLE_PANEL,0);lv_obj_set_width(row,LV_PCT(100));lv_obj_set_height(row,LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW);lv_obj_set_style_pad_column(row,ouse_ui_dp(8),0);
    lv_obj_t *button=ouse_widget_button(row,"连接",connect_event,NULL);lv_obj_set_width(button,0);lv_obj_set_flex_grow(button,1);
    button=ouse_widget_button(row,"取消",cancel_event,NULL);lv_obj_set_width(button,0);lv_obj_set_flex_grow(button,1);
    lv_obj_add_flag(ui.popup,LV_OBJ_FLAG_HIDDEN);
}
static void set_visible(lv_obj_t *object,bool visible)
{
    if(visible)lv_obj_remove_flag(object,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(object,LV_OBJ_FLAG_HIDDEN);
}
static void raw_text(lv_obj_t *label,const char *value)
{
    // SSIDs and IP addresses are network data, never keys in the system dictionary.
    ouse_ui_unbind_text(label);lv_label_set_text(label,value);
}
static unsigned signal_level(int rssi) {return rssi>=-55 ? 3 : rssi>=-70 ? 2 : 1;}
static void signal_draw(lv_event_t *event)
{
    lv_obj_t *object=lv_event_get_current_target_obj(event);
    unsigned flags=(unsigned)(uintptr_t)lv_obj_get_user_data(object),level=flags&3;
    lv_area_t area;lv_obj_get_coords(object,&area);
    lv_layer_t *layer=lv_event_get_layer(event);
    lv_color_t color=lv_obj_get_style_text_color(object,0);
    lv_draw_arc_dsc_t arc;lv_draw_arc_dsc_init(&arc);
    arc.color=color;arc.width=ouse_ui_dp(2);arc.rounded=1;
    arc.start_angle=225;arc.end_angle=315;
    arc.center=(lv_point_t){area.x1+ouse_ui_dp(16),area.y1+ouse_ui_dp(26)};
    for(unsigned i=0;i<3;++i) {
        arc.radius=ouse_ui_dp(9+7*i);arc.opa=i<level ? LV_OPA_COVER : LV_OPA_20;
        lv_draw_arc(layer,&arc);
    }
    lv_draw_rect_dsc_t rect;lv_draw_rect_dsc_init(&rect);
    rect.bg_color=color;rect.bg_opa=LV_OPA_COVER;rect.radius=LV_RADIUS_CIRCLE;
    lv_area_t dot={area.x1+ouse_ui_dp(14),area.y1+ouse_ui_dp(24),area.x1+ouse_ui_dp(18),area.y1+ouse_ui_dp(28)};
    lv_draw_rect(layer,&rect,&dot);
    if(flags&4) {
        // A small native lock avoids depending on a font containing a lock glyph.
        lv_color_t surface=lv_obj_get_style_bg_color(lv_obj_get_parent(object),0);
        lv_area_t clear={area.x1+ouse_ui_dp(22),area.y1+ouse_ui_dp(18),area.x1+ouse_ui_dp(35),area.y1+ouse_ui_dp(31)};
        rect.bg_color=surface;rect.radius=ouse_ui_dp(2);lv_draw_rect(layer,&rect,&clear);
        arc.center=(lv_point_t){area.x1+ouse_ui_dp(28),area.y1+ouse_ui_dp(24)};
        arc.radius=ouse_ui_dp(4);arc.start_angle=180;arc.end_angle=360;arc.opa=LV_OPA_COVER;
        lv_draw_arc(layer,&arc);
        lv_area_t body={area.x1+ouse_ui_dp(23),area.y1+ouse_ui_dp(23),area.x1+ouse_ui_dp(33),area.y1+ouse_ui_dp(30)};
        rect.bg_color=surface;rect.border_color=color;rect.border_width=ouse_ui_dp(2);rect.border_opa=LV_OPA_COVER;
        lv_draw_rect(layer,&rect,&body);
    }
}
static void update_signal(lv_obj_t *object,int rssi,bool secured)
{
    uintptr_t flags=signal_level(rssi)|(secured ? 4 : 0);
    if((uintptr_t)lv_obj_get_user_data(object)!=flags){lv_obj_set_user_data(object,(void *)flags);lv_obj_invalidate(object);}
}
static lv_obj_t *network_row(lv_obj_t *parent,const char *ssid,const char *detail,int rssi,bool secured,
                              lv_event_cb_t callback,void *data,lv_obj_t **title,lv_obj_t **value,lv_obj_t **signal)
{
    lv_obj_t *row=ouse_widget_row(parent,NULL,"",detail,callback,data,value);
    lv_obj_t *copy=lv_obj_get_child(row,0),*label=lv_obj_get_child(copy,0);
    raw_text(label,ssid);lv_label_set_long_mode(label,LV_LABEL_LONG_WRAP);
    if(title)*title=label;
    lv_obj_t *icon=ouse_widget_box(row,OUSE_STYLE_PANEL,0);
    lv_obj_set_style_bg_opa(icon,LV_OPA_TRANSP,0);ouse_theme_apply(icon,OUSE_STYLE_MUTED,0);
    lv_obj_set_size(icon,ouse_ui_dp(36),ouse_ui_dp(32));lv_obj_move_to_index(icon,0);
    lv_obj_add_event_cb(icon,signal_draw,LV_EVENT_DRAW_MAIN,NULL);update_signal(icon,rssi,secured);
    if(signal)*signal=icon;
    lv_obj_set_style_min_height(row,ouse_ui_dp(detail ? 72 : 60),0);
    lv_obj_set_style_pad_hor(row,ouse_ui_dp(12),0);lv_obj_set_style_pad_column(row,ouse_ui_dp(10),0);
    return row;
}
static void details_event(lv_event_t *event)
{
    (void)event;if(!ui.wifi_state.connected)return;
    lv_obj_remove_flag(ui.details,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(ui.details);
}
static void build_details(lv_obj_t *page)
{
    ui.details=ouse_widget_box(lv_obj_get_parent(page),OUSE_STYLE_DESKTOP,0);
    lv_obj_set_size(ui.details,LV_PCT(100),LV_PCT(100));
    lv_obj_set_style_bg_color(ui.details,lv_color_black(),0);lv_obj_set_style_bg_opa(ui.details,LV_OPA_50,0);
    lv_obj_add_flag(ui.details,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *card=ouse_widget_box(ui.details,OUSE_STYLE_PANEL,20);
    lv_obj_set_size(card,LV_PCT(94),LV_SIZE_CONTENT);lv_obj_set_style_max_height(card,LV_PCT(94),0);
    lv_obj_set_style_pad_all(card,ouse_ui_dp(16),0);lv_obj_set_style_pad_row(card,ouse_ui_dp(12),0);
    lv_obj_set_flex_flow(card,LV_FLEX_FLOW_COLUMN);lv_obj_add_flag(card,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(card,LV_DIR_VER);lv_obj_align(card,LV_ALIGN_BOTTOM_MID,0,-ouse_ui_dp(8));
    lv_obj_t *heading=ouse_widget_text(card,"网络详情",20);lv_obj_set_width(heading,LV_PCT(100));
    ui.details_ssid=ouse_widget_text(card,"",18);lv_obj_set_width(ui.details_ssid,LV_PCT(100));
    lv_obj_t *row=ouse_widget_row(card,NULL,"IP 地址","",NULL,NULL,&ui.details_ip);
    lv_obj_remove_flag(row,LV_OBJ_FLAG_CLICKABLE);lv_obj_delete(lv_obj_get_child(row,-1));
    row=ouse_widget_row(card,NULL,"信号强度","",NULL,NULL,&ui.details_signal);
    lv_obj_remove_flag(row,LV_OBJ_FLAG_CLICKABLE);lv_obj_delete(lv_obj_get_child(row,-1));
    row=ouse_widget_row(card,NULL,"时间同步","",NULL,NULL,&ui.details_time);
    lv_obj_remove_flag(row,LV_OBJ_FLAG_CLICKABLE);lv_obj_delete(lv_obj_get_child(row,-1));
    row=ouse_widget_box(card,OUSE_STYLE_PANEL,0);lv_obj_set_size(row,LV_PCT(100),LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(row,ouse_ui_dp(8),0);lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW_WRAP);
    ouse_widget_button(row,"断开连接",disconnect_event,NULL);ouse_widget_button(row,"忘记网络",forget_event,NULL);
    lv_obj_t *close=ouse_widget_button(card,"完成",cancel_event,NULL);lv_obj_set_style_align(close,LV_ALIGN_RIGHT_MID,0);
    lv_obj_add_flag(ui.details,LV_OBJ_FLAG_HIDDEN);
}
static void build_wifi(lv_obj_t *page)
{
    lv_obj_t *row=ouse_widget_row(page,LV_SYMBOL_WIFI,"Wi-Fi",NULL,wifi_event,NULL,NULL);
    lv_obj_delete(lv_obj_get_child(row,-1));ui.wifi_switch=ouse_widget_switch(row);
    lv_obj_add_event_cb(ui.wifi_switch,wifi_event,LV_EVENT_VALUE_CHANGED,NULL);
    ui.wifi=ouse_widget_text(page,"",14);lv_obj_set_width(ui.wifi,LV_PCT(100));
    ouse_theme_apply(ui.wifi,OUSE_STYLE_MUTED,0);
    ui.current_heading=ouse_widget_text(page,"当前网络",14);lv_obj_set_width(ui.current_heading,LV_PCT(100));
    lv_obj_set_style_margin_top(ui.current_heading,ouse_ui_dp(10),0);ouse_theme_apply(ui.current_heading,OUSE_STYLE_MUTED,0);
    ui.current=network_row(page,"","",-100,false,details_event,NULL,&ui.current_title,&ui.current_detail,&ui.current_signal);
    lv_label_set_text(lv_obj_get_child(ui.current,-1),LV_SYMBOL_SETTINGS);
    ui.available_heading=ouse_widget_box(page,OUSE_STYLE_APP,0);
    lv_obj_set_size(ui.available_heading,LV_PCT(100),LV_SIZE_CONTENT);lv_obj_set_style_margin_top(ui.available_heading,ouse_ui_dp(4),0);
    lv_obj_set_flex_flow(ui.available_heading,LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui.available_heading,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    lv_obj_t *label=ouse_widget_text(ui.available_heading,"可用网络",14);lv_obj_set_width(label,0);lv_obj_set_flex_grow(label,1);
    ouse_theme_apply(label,OUSE_STYLE_MUTED,0);
    ui.scan=lv_button_create(ui.available_heading);lv_obj_remove_style_all(ui.scan);
    lv_obj_set_size(ui.scan,ouse_ui_dp(40),ouse_ui_dp(40));lv_obj_set_style_radius(ui.scan,LV_RADIUS_CIRCLE,0);
    ouse_theme_apply(ui.scan,OUSE_STYLE_PRESSED,LV_STATE_PRESSED);lv_obj_set_style_opa(ui.scan,LV_OPA_40,LV_STATE_DISABLED);
    lv_obj_add_event_cb(ui.scan,scan_event,LV_EVENT_CLICKED,NULL);
    label=lv_label_create(ui.scan);lv_label_set_text(label,LV_SYMBOL_REFRESH);lv_obj_remove_flag(label,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(label,&lv_font_montserrat_24,0);ouse_theme_apply(label,OUSE_STYLE_ACCENT_TEXT,0);lv_obj_center(label);
    ui.network_group=ouse_widget_box(page,OUSE_STYLE_PANEL,16);
    lv_obj_set_size(ui.network_group,LV_PCT(100),LV_SIZE_CONTENT);lv_obj_set_flex_flow(ui.network_group,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(ui.network_group,ouse_ui_dp(4),0);lv_obj_set_style_pad_row(ui.network_group,0,0);
    ui.networks=ouse_widget_box(ui.network_group,OUSE_STYLE_PANEL,0);
    lv_obj_set_size(ui.networks,LV_PCT(100),LV_SIZE_CONTENT);lv_obj_set_flex_flow(ui.networks,LV_FLEX_FLOW_COLUMN);
    ui.empty=ouse_widget_text(ui.network_group,"未找到网络",14);lv_obj_set_width(ui.empty,LV_PCT(100));
    lv_obj_set_style_pad_all(ui.empty,ouse_ui_dp(12),0);ouse_theme_apply(ui.empty,OUSE_STYLE_MUTED,0);
    row=ouse_widget_row(ui.network_group,LV_SYMBOL_PLUS,"添加网络",NULL,network_event,NULL,NULL);
    lv_obj_delete(lv_obj_get_child(row,-1));lv_obj_set_style_radius(row,ouse_ui_dp(8),0);
    build_popup(page);build_details(page);
}
void ouse_device_ui_build(lv_obj_t *page,unsigned index)
{
    if(index==5) {
        ui.power=ouse_widget_text(page,"",20);lv_obj_set_width(ui.power,LV_PCT(100));
        ouse_widget_row(page,NULL,"自动熄屏","",timeout_event,NULL,&ui.timeout_value);
        ouse_widget_button(page,"立即熄屏",sleep_event,NULL);
    } else if(index==6) {
        ui.volume_value=ouse_widget_text(page,"",18);
        ui.volume=lv_slider_create(page);lv_obj_set_width(ui.volume,LV_PCT(100));ouse_widget_slider(ui.volume,24);lv_slider_set_range(ui.volume,0,100);
        lv_obj_add_event_cb(ui.volume,volume_event,LV_EVENT_VALUE_CHANGED,NULL);
        lv_obj_t *row=ouse_widget_row(page,LV_SYMBOL_MUTE,"静音",NULL,mute_event,NULL,NULL);lv_obj_delete(lv_obj_get_child(row,-1));
        ui.mute=ouse_widget_switch(row);
        lv_obj_add_event_cb(ui.mute,mute_event,LV_EVENT_VALUE_CHANGED,NULL);
    } else if(index==10){
        lv_obj_t *row=ui.usb_audio_row=ouse_widget_row(page,LV_SYMBOL_AUDIO,"USB 音频",NULL,usb_event,NULL,NULL);
        lv_obj_delete(lv_obj_get_child(row,-1));ui.usb_switch=ouse_widget_switch(row);
        lv_obj_add_event_cb(ui.usb_switch,usb_event,LV_EVENT_VALUE_CHANGED,NULL);
        row=ui.usb_network_row=ouse_widget_row(page,LV_SYMBOL_WIFI,"USB 网络共享",NULL,usb_network_event,NULL,NULL);
        lv_obj_delete(lv_obj_get_child(row,-1));ui.usb_network=ouse_widget_switch(row);
        lv_obj_add_event_cb(ui.usb_network,usb_network_event,LV_EVENT_VALUE_CHANGED,NULL);
        lv_obj_set_style_opa(ui.usb_audio_row,LV_OPA_40,LV_STATE_DISABLED);
        lv_obj_set_style_opa(ui.usb_network_row,LV_OPA_40,LV_STATE_DISABLED);
    } else build_wifi(page);
    ouse_device_ui_refresh();
}
static void checked(lv_obj_t *obj,bool on) {if(on)lv_obj_add_state(obj,LV_STATE_CHECKED);else lv_obj_remove_state(obj,LV_STATE_CHECKED);}
void ouse_device_ui_refresh(void)
{
    char text[256];
    if(ui.usb_switch){
        ouse_usb_state_t s;ouse_usb_get(&s);checked(ui.usb_switch,s.audio_enabled);checked(ui.usb_network,s.network_enabled);
        bool connected=s.available && s.enabled && s.mounted && !s.suspended && !ouse_tasks_quiescing();
        lv_obj_t *controls[]={ui.usb_audio_row,ui.usb_switch,ui.usb_network_row,ui.usb_network};
        for(unsigned i=0;i<4;++i){
            bool usable=connected && (i<2 || s.network_enabled || (s.uplink && !s.subnet_conflict));
            if(usable)lv_obj_remove_state(controls[i],LV_STATE_DISABLED);else lv_obj_add_state(controls[i],LV_STATE_DISABLED);
        }
    }
    if(ui.power) {
        ouse_power_state_t p={0};
        if(ouse_power_get(&p)!=ESP_OK || !p.valid)ouse_ui_text(ui.power,"电源状态不可用");
        else if(!p.present)ouse_ui_text(ui.power,p.external ? "外部供电，未连接电池" : "未连接电池");
        else {
            char percent[16];if(p.percent<0)strcpy(percent,"--");else snprintf(percent,sizeof(percent),"%d%%",p.percent);
            snprintf(text,sizeof(text),"%s: %s\n%s",ouse_ui_tr("电池"),percent,
                ouse_ui_tr(p.charging ? "正在充电" : p.full ? "已充满" : p.external ? "外部供电" : "电池供电"));
            ouse_ui_unbind_text(ui.power);lv_label_set_text(ui.power,text);
        }
        for(unsigned i=0;i<6;++i)if(ui.timeout_value && p.sleep_seconds==timeouts[i])ouse_ui_text(ui.timeout_value,timeout_labels[i]);
    }
    if(ui.volume) {
        ouse_audio_state_t a={0};ouse_audio_get(&a);
        lv_label_set_text_fmt(ui.volume_value,"%s  %u%%",ouse_ui_tr("音量"),a.volume);
        if(!lv_obj_has_state(ui.volume,LV_STATE_PRESSED))lv_slider_set_value(ui.volume,a.volume,LV_ANIM_OFF);
        checked(ui.mute,a.muted);
    }
    if(ui.wifi) {
        ouse_wifi_state_t w={0};ouse_wifi_get(&w);
        if(w.count>OUSE_WIFI_AP_MAX)w.count=OUSE_WIFI_AP_MAX;
        bool shown=lv_obj_is_visible(ui.wifi_switch);
        if(!shown || !w.enabled)ui.scan_visit=false;
        if(shown && ui.wifi_initialized && !ui.scan_visit && w.available && w.enabled && w.ready &&
           !w.scanning && !w.connecting && w.completed_id==w.request_id) {
            ui.scan_visit=true;check(ouse_wifi_scan());
        }
        uint32_t generation=ouse_ui_generation();
        if(ui.wifi_initialized && w.revision==ui.wifi_state.revision && generation==ui.generation)return;
        lv_textarea_set_placeholder_text(ui.ssid,ouse_ui_tr("网络名称"));
        lv_textarea_set_placeholder_text(ui.password,ouse_ui_tr("密码"));
        checked(ui.wifi_switch,w.enabled);
        if(w.available)lv_obj_remove_state(ui.wifi_switch,LV_STATE_DISABLED);else lv_obj_add_state(ui.wifi_switch,LV_STATE_DISABLED);
        const char *status=!w.available ? "无线服务不可用" : !w.enabled ? "已关闭" : w.scanning ? "正在搜索" :
                           w.connecting ? "正在连接" : w.error!=ESP_OK ? "连接失败，请重试" : w.connected ? "已连接" : "未连接";
        ouse_ui_text(ui.wifi,status);
        bool active=w.enabled && w.available,has_current=active && (w.connected || w.connecting) && w.ssid[0];
        set_visible(ui.wifi,!active || w.scanning || w.error!=ESP_OK || (!w.connected && !has_current));
        set_visible(ui.current_heading,has_current);set_visible(ui.current,has_current);
        set_visible(ui.available_heading,active);set_visible(ui.network_group,active);
        if(!active && ouse_device_ui_modal())ouse_device_ui_close_modal();
        if(w.scanning || w.connecting || !active)lv_obj_add_state(ui.scan,LV_STATE_DISABLED);else lv_obj_remove_state(ui.scan,LV_STATE_DISABLED);
        if(has_current) {
            raw_text(ui.current_title,w.ssid);
            snprintf(text,sizeof(text),"%s%s%s",ouse_ui_tr(w.connected ? "已连接" : "正在连接"),w.connected && w.ip[0] ? " · " : "",w.connected ? w.ip : "");
            raw_text(ui.current_detail,text);
            bool secured=false;
            for(unsigned i=0;i<w.count;++i)if(!strcmp(w.aps[i].ssid,w.ssid)){secured=w.aps[i].secured;break;}
            update_signal(ui.current_signal,w.rssi,secured);
            set_visible(lv_obj_get_child(ui.current,-1),w.connected);
        }
        if(!w.connected && ui.details)lv_obj_add_flag(ui.details,LV_OBJ_FLAG_HIDDEN);
        if(w.connected) {
            raw_text(ui.details_ssid,w.ssid);raw_text(ui.details_ip,w.ip);
            snprintf(text,sizeof(text),"%d dBm",w.rssi);raw_text(ui.details_signal,text);
            ouse_clock_state_t clock;ouse_clock_get(&clock);
            raw_text(ui.details_time,clock.syncing ? "正在同步" : clock.failed ? "同步失败，将重试" : clock.synced_this_boot ? "已同步" : "未同步");
        }
        // Periodic status reads leave the list and its hit targets intact. Only
        // list content, current-network identity or UI metrics can replace rows.
        bool rebuild=!ui.wifi_initialized || ui.ap_count!=w.count || memcmp(ui.aps,w.aps,sizeof(ui.aps)) ||
                     ui.wifi_state.connected!=w.connected || ui.wifi_state.connecting!=w.connecting ||
                     strcmp(ui.wifi_state.ssid,w.ssid) || ui.generation!=generation;
        if(rebuild) {
            lv_obj_clean(ui.networks);ui.ap_count=w.count;memcpy(ui.aps,w.aps,sizeof(ui.aps));
            for(unsigned i=0;i<ui.ap_count;++i) {
                if((w.connected || w.connecting) && !strcmp(ui.aps[i].ssid,w.ssid))continue;
                lv_obj_t *row=network_row(ui.networks,ui.aps[i].ssid,NULL,ui.aps[i].rssi,ui.aps[i].secured,
                                           network_event,(void *)(uintptr_t)(i+1),NULL,NULL,NULL);
                lv_obj_delete(lv_obj_get_child(row,-1));lv_obj_set_style_radius(row,ouse_ui_dp(8),0);
            }
        }
        unsigned visible=lv_obj_get_child_count(ui.networks);
        set_visible(ui.networks,visible>0);set_visible(ui.empty,!visible);
        ouse_ui_text(ui.empty,w.scanning ? "正在搜索" : "未找到网络");
        ui.wifi_state=w;ui.generation=generation;ui.wifi_initialized=true;
    }
}
void ouse_device_ui_close(void)
{
    ouse_device_ui_close_modal();
    memset(&ui,0,sizeof(ui));
}
