// SPDX-License-Identifier: MIT
#include "ouse_shell.h"
#include "ouse_widgets.h"
#include "ouse_devices.h"
#include "ouse_ble.h"
#include "ouse_usb.h"
#include "ouse_dialog.h"
#include "ouse_device_ui.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "ouse_ime.h"
#include "ouse_notifications.h"
#include "ouse_memory.h"
#include "ouse_apps_internal.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
extern void ouse_navigation_clock(char *);
extern void ouse_navigation_date(char *,size_t,bool);

typedef enum { G_NONE, G_TOP, G_CLOSE, G_BACK, G_BOTTOM, G_CARD, G_SWITCH, G_LOCKED } gesture_kind_t;
typedef struct {lv_obj_t *button,*icon,*title,*status;} quick_tile_t;
static OUSE_PSRAM_BSS struct {
    ouse_shell_config_t config;
    lv_obj_t *quick_layer,*quick,*quick_body,*quick_clock,*date,*slider;
    lv_obj_t *sheet,*notification_panel,*notification_list,*notification_clear,*notification_clock;
    ouse_notification_t notification_items[OUSE_NOTIFICATION_CAPACITY];
    unsigned notification_count;
    uint32_t notification_generation,notification_ui_generation,notification_age_minute;
    bool notifications_selected;
    lv_obj_t *wifi_icon,*bluetooth_icon,*notification_dot,*banner,*banner_title,*banner_body;
    bool bluetooth_connected;
    uint32_t banner_seen,notification_read,banner_serial,banner_until;
    int wifi_level;
    uint32_t icon_generation;
    struct {char app_id[48];lv_image_dsc_t image;} notification_icons[OUSE_NOTIFICATION_CAPACITY];
    unsigned notification_icon_count;
    lv_obj_t *gear,*light,*clock,*battery,*status,*nav,*volume_box,*volume_slider,*volume_icon;
    quick_tile_t wifi_tile,bluetooth_tile,power_tile,sound_tile,theme_tile,usb_tile;
    lv_obj_t *recents,*strip,*clear,*cards[OUSE_RECENTS_CAPACITY],*card_titles[OUSE_RECENTS_CAPACITY];
    lv_obj_t *shield,*back_indicator,*back_arrow,*flight,*flight_image,*pill;
    lv_obj_t *bottom_guard,*quick_handle,*banner_dismiss,*game_hint;
    uint32_t game_hint_at;
    lv_obj_t *navigation_buttons[3],*navigation_icons[3];
    unsigned navigation;
    lv_point_t navigation_start;
    bool navigation_cancelled,fullscreen;
    lv_image_dsc_t images[OUSE_RECENTS_CAPACITY],flight_source;
    ouse_app_info_t items[OUSE_RECENTS_CAPACITY];
    lv_timer_t *gesture_timer;
    unsigned count;
    int width,height,quick_height,card_height;
    bool quick_target,quick_animating,save_failed,brightness_failed;
    bool card_animating,card_closing;
    int card_anim_index,card_anim_start;
    uint32_t generation,recent_generation;
    struct {
        gesture_kind_t kind;
        lv_point_t start,last,anchor;
        uint32_t last_move,last_tick;
        int sheet_start,card,back_progress;
        bool claimed,left,home_started,from_recents;
        char origin[48];
    } g;
    int flight_x,flight_y,flight_w,flight_h;
} shell;
#define D(value) ouse_ui_dp(value)
static void finish_gesture(bool cancel);
static void show_recents(void);
static void settle_quick(bool open);
static void flight_clear(void);
static void rebuild_notifications(void);
static void layout_quick(void);
static void orientation_clicked(lv_event_t *event){(void)event;if(ouse_core_set_orientation(!ouse_core_landscape())!=ESP_OK)ouse_dialog_show("屏幕旋转失败，请重试",NULL,NULL);}
static void refresh(lv_timer_t *timer);
static int clamp(int value,int lo,int hi) { return value<lo ? lo : value>hi ? hi : value; }
static bool visible(lv_obj_t *obj) { return obj && !lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN); }
static void hide(lv_obj_t *obj) { lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN); }
static void show(lv_obj_t *obj) { lv_obj_remove_flag(obj,LV_OBJ_FLAG_HIDDEN); }
static void rect(lv_obj_t *obj,int x,int y,int w,int h)
{ lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,h); }
static void label_changed(lv_obj_t *label,const char *value)
{ if(strcmp(lv_label_get_text(label),value))lv_label_set_text(label,value); }
static void background_changed(lv_obj_t *object,lv_color_t color)
{ if(!lv_color_eq(lv_obj_get_style_bg_color(object,0),color))lv_obj_set_style_bg_color(object,color,0); }
static void text_color_changed(lv_obj_t *object,lv_color_t color)
{ if(!lv_color_eq(lv_obj_get_style_text_color(object,0),color))lv_obj_set_style_text_color(object,color,0); }
static bool contains(lv_obj_t *obj,lv_point_t p)
{
    if(!obj || !lv_obj_is_visible(obj))return false;
    lv_area_t a;lv_obj_get_coords(obj,&a);
    return p.x>=a.x1 && p.x<=a.x2 && p.y>=a.y1 && p.y<=a.y2;
}
static void status_colors(bool overlay)
{
    lv_opa_t opacity=overlay ? (lv_opa_t)96 : LV_OPA_COVER;
    if(lv_obj_get_style_bg_opa(shell.status,0)!=opacity)lv_obj_set_style_bg_opa(shell.status,opacity,0);
    ouse_app_stats_t apps;bool in_app=ouse_app_get_stats(&apps)==ESP_OK && apps.active_id[0];
    lv_color_t color=lv_color_hex(in_app ? ouse_theme_colors().background : ouse_theme_colors().desktop);
    background_changed(shell.status,color);
    if(shell.nav)background_changed(shell.nav,color);
}
static void overlay_status(bool overlay)
{
    status_colors(overlay);
    if(ouse_core_access_blocked())return;
    lv_obj_move_foreground(shell.status);
    if(shell.nav && !visible(shell.quick_layer)) {
        if(shell.bottom_guard && !shell.navigation)lv_obj_move_foreground(shell.bottom_guard);
        lv_obj_move_foreground(shell.nav);
    }
}
static void clear_cards(void)
{
    shell.card_animating=false;shell.card_anim_index=-1;
    lv_obj_clean(shell.strip); // close decoders before freeing their image data
    for(unsigned i=0;i<shell.count;++i) {
        memset(&shell.images[i],0,sizeof(shell.images[i]));shell.cards[i]=shell.card_titles[i]=NULL;
    }
    shell.count=0;
}
static void move_quick(void *unused,int32_t y)
{
    (void)unused;
    y=clamp(y,-shell.quick_height,0);lv_obj_set_y(shell.sheet,y);
    lv_obj_set_style_bg_opa(shell.quick_layer,(lv_opa_t)(110*(y+shell.quick_height)/shell.quick_height),0);
}
static void quick_done(lv_anim_t *animation)
{
    (void)animation;shell.quick_animating=false;
    if(!shell.quick_target)hide(shell.quick_layer);
}
static void settle_quick(bool open)
{
    if(open)ouse_ime_hide();
    lv_anim_delete(shell.sheet,move_quick);
    shell.quick_target=open;shell.quick_animating=true;show(shell.quick_layer);
    lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,shell.sheet);
    lv_anim_set_exec_cb(&a,move_quick);lv_anim_set_values(&a,lv_obj_get_y(shell.sheet),open ? 0 : -shell.quick_height);
    lv_anim_set_duration(&a,180);lv_anim_set_path_cb(&a,lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a,quick_done);
    if(!lv_anim_start(&a)) {move_quick(NULL,open ? 0 : -shell.quick_height);quick_done(NULL);}
}
static void back_motion(void *unused,int32_t progress)
{
    (void)unused;shell.g.back_progress=progress;
    if(progress<=0){hide(shell.back_indicator);return;}
    show(shell.back_indicator);
    int w=D(36)+D(16)*progress/1024;
    int x=shell.g.left ? -D(16)+D(20)*progress/1024 : shell.width-w+D(16)-D(20)*progress/1024;
    rect(shell.back_indicator,x,clamp(shell.g.start.y-D(34),D(44),shell.height-D(100)),w,D(68));
    lv_obj_set_style_opa(shell.back_indicator,(lv_opa_t)(100+155*progress/1024),0);
    lv_obj_center(shell.back_arrow);
}
static void rebound_back(void)
{
    lv_anim_delete(shell.back_indicator,back_motion);
    lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,shell.back_indicator);
    lv_anim_set_exec_cb(&a,back_motion);lv_anim_set_values(&a,shell.g.back_progress,0);
    lv_anim_set_duration(&a,140);lv_anim_set_path_cb(&a,lv_anim_path_ease_out);
    if(!lv_anim_start(&a))back_motion(NULL,0);
}
static void flight_clear(void)
{
    ouse_apps_swipe_offset(0);
    lv_anim_delete(shell.flight,NULL);hide(shell.flight);
    lv_image_set_src(shell.flight_image,NULL);
    lv_free((void *)shell.flight_source.data);memset(&shell.flight_source,0,sizeof(shell.flight_source));
    overlay_status(visible(shell.recents));
}
static void pill_motion(void *unused,int32_t offset)
{ (void)unused;if(shell.pill)lv_obj_set_style_translate_y(shell.pill,offset,0); }
static void settle_pill(bool recent)
{
    if(shell.navigation)return;
    lv_anim_delete(shell.pill,pill_motion);
    lv_anim_t animation;lv_anim_init(&animation);lv_anim_set_var(&animation,shell.pill);
    lv_anim_set_exec_cb(&animation,pill_motion);
    lv_anim_set_values(&animation,lv_obj_get_style_translate_y(shell.pill,0),recent ? -D(6) : 0);
    lv_anim_set_duration(&animation,100);lv_anim_set_path_cb(&animation,lv_anim_path_ease_out);lv_anim_start(&animation);
}
static void flight_finish(lv_anim_t *animation)
{ (void)animation;(void)ouse_apps_background_fast();flight_clear();settle_pill(false); }
static void flight_settle(void *unused,int32_t p)
{
    (void)unused;
    int offset=shell.flight_y+(-shell.height-shell.flight_y)*p/1024;
    ouse_apps_swipe_offset(offset);
}
static void flight_home(void)
{
    if(!shell.g.origin[0]){settle_pill(false);return;}
    if(!ouse_apps_confirm_exit()){flight_clear();settle_pill(false);return;}
    shell.flight_y=clamp(shell.g.last.y-shell.g.start.y,-shell.height,0);
    lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,shell.flight);lv_anim_set_exec_cb(&a,flight_settle);
    lv_anim_set_values(&a,0,1024);lv_anim_set_duration(&a,140);lv_anim_set_path_cb(&a,lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a,flight_finish);
    if(!lv_anim_start(&a))flight_finish(NULL);
}
static void begin_home(void)
{
    flight_clear();
    shell.g.home_started=true;
    ouse_app_stats_t state;
    if(ouse_app_get_stats(&state)==ESP_OK)strcpy(shell.g.origin,state.active_id);
    overlay_status(true);
}
static void follow_home(int dy)
{
    if(!shell.g.home_started)begin_home();
    pill_motion(NULL,-clamp(-dy/8,0,D(12)));
    if(shell.g.origin[0])ouse_apps_swipe_offset(clamp(dy,-shell.height,0));
}
static void claim(void)
{
    if(shell.g.claimed)return;
    shell.g.claimed=true;show(shell.shield);lv_obj_move_foreground(shell.shield);
    // Clear the original widget/scroll target. Further pointer reads land on
    // the press-locked shield, while the short-lived UI timer keeps tracking.
    lv_indev_reset(shell.config.touch,NULL);
}
void ouse_shell_foreground(void)
{
    if(!shell.recents)return;
    lv_timer_pause(shell.gesture_timer);lv_anim_delete(shell.sheet,move_quick);
    lv_anim_delete(shell.back_indicator,back_motion);
    hide(shell.quick_layer);hide(shell.recents);hide(shell.shield);hide(shell.back_indicator);
    shell.quick_animating=false;shell.quick_target=false;shell.g.kind=G_NONE;shell.g.claimed=false;
    flight_clear();clear_cards();overlay_status(false);settle_pill(false);
}
static void dismiss_recent(lv_event_t *event)
{
    (void)event;hide(shell.recents);clear_cards();overlay_status(false);settle_pill(false);
}
static void resume_card(lv_event_t *event)
{
    char id[48];strcpy(id,lv_event_get_user_data(event));
    dismiss_recent(NULL);(void)ouse_app_open(id);
}
static void close_all(lv_event_t *event)
{
    (void)event;ouse_app_info_t entries[OUSE_RECENTS_CAPACITY];unsigned count=0;
    while(count<OUSE_RECENTS_CAPACITY && ouse_app_get_recent(count,&entries[count])==ESP_OK)++count;
    dismiss_recent(NULL);
    for(unsigned i=0;i<count;++i)(void)ouse_app_close_id(entries[i].id);
    (void)ouse_app_background();
}
static void recent_blank(lv_event_t *event)
{
    (void)event;lv_point_t p;lv_indev_get_point(shell.config.touch,&p);
    for(unsigned i=0;i<shell.count;++i)if(contains(shell.cards[i],p))return;
    dismiss_recent(NULL);
}
static void show_recents(void)
{
    flight_clear();clear_cards();(void)ouse_app_background();settle_pill(true);
    shell.recent_generation=ouse_ui_generation();
    hide(shell.quick_layer);show(shell.recents);lv_obj_move_foreground(shell.recents);overlay_status(true);
    for(unsigned i=0;i<OUSE_RECENTS_CAPACITY;++i) {
        if(ouse_app_get_recent(i,&shell.items[i])!=ESP_OK)break;
        ++shell.count;
        lv_obj_t *card=ouse_widget_box(shell.strip,OUSE_STYLE_PANEL,24);
        lv_obj_add_flag(card,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SNAPPABLE);
        lv_obj_set_style_clip_corner(card,true,0);
        rect(card,(shell.width-D(224))/2+(int)i*D(244),D(50),D(224),shell.card_height);shell.cards[i]=card;
        lv_obj_add_event_cb(card,resume_card,LV_EVENT_CLICKED,shell.items[i].id);
        lv_obj_t *icon=ouse_widget_app_icon(card,&shell.items[i],28);
        lv_obj_set_pos(icon,D(12),D(10));
        lv_obj_t *title=ouse_widget_text(card,"",14);
        ouse_widget_app_name(title,&shell.items[i]);
        shell.card_titles[i]=title;
        rect(title,D(50),D(12),D(160),ouse_ui_font(14)->line_height);
        lv_label_set_long_mode(title,LV_LABEL_LONG_MODE_DOTS);
        const lv_image_dsc_t *source=ouse_app_get_preview(shell.items[i].id);
        if(source && source->data_size<=4u*1024*1024) {
            {
                shell.images[i]=*source;
                lv_obj_t *image=lv_image_create(card);lv_image_set_src(image,&shell.images[i]);
                rect(image,D(10),D(48),D(204),shell.card_height-D(58));
                lv_image_set_inner_align(image,LV_IMAGE_ALIGN_CONTAIN);lv_obj_remove_flag(image,LV_OBJ_FLAG_CLICKABLE);
            }
        }
        if(!shell.images[i].data) {
            lv_obj_t *empty=ouse_widget_text(card,"点击恢复应用",14);
            lv_obj_set_width(empty,D(188));lv_obj_set_style_text_align(empty,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(empty);
        }
    }
    if(!shell.count) {
        lv_obj_t *empty=ouse_widget_text(shell.strip,"暂无最近应用",16);
        lv_obj_set_width(empty,shell.width-D(48));lv_obj_set_style_text_align(empty,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_align(empty,LV_ALIGN_CENTER,0,0);hide(shell.clear);
    } else {
        show(shell.clear);
        // Leave enough trailing content for the final card to snap to center.
        lv_obj_t *space=ouse_widget_box(shell.strip,OUSE_STYLE_DESKTOP,0);
        lv_obj_remove_flag(space,LV_OBJ_FLAG_SNAPPABLE);
        rect(space,(shell.width+D(224))/2+(int)(shell.count-1)*D(244),0,(shell.width-D(224))/2,1);
    }
    lv_obj_update_layout(shell.strip);lv_obj_scroll_to_x(shell.strip,0,LV_ANIM_OFF);
}
static void card_motion(void *object,int32_t p)
{
    int end=shell.card_closing ? -shell.card_height-D(60) : D(50);
    int y=shell.card_anim_start+(end-shell.card_anim_start)*p/1024;
    lv_obj_set_y(object,y);
    lv_obj_set_style_opa(object,(lv_opa_t)clamp(255+(y-D(50))*180/shell.card_height,0,255),0);
}
static void card_done(lv_anim_t *animation)
{
    (void)animation;bool close=shell.card_closing;
    char id[48];strcpy(id,shell.items[shell.card_anim_index].id);
    shell.card_animating=false;hide(shell.shield);
    if(close) {clear_cards();(void)ouse_app_close_id(id);show_recents();}
}
static void settle_card(unsigned index,bool close)
{
    lv_obj_t *card=shell.cards[index];
    lv_obj_update_layout(card);
    shell.card_anim_index=(int)index;shell.card_anim_start=lv_obj_get_y(card);
    shell.card_closing=close;shell.card_animating=true;
    show(shell.shield);lv_obj_move_foreground(shell.shield);
    lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,card);lv_anim_set_exec_cb(&a,card_motion);
    lv_anim_set_values(&a,0,1024);lv_anim_set_duration(&a,close ? 160 : 140);
    lv_anim_set_path_cb(&a,lv_anim_path_ease_out);lv_anim_set_completed_cb(&a,card_done);
    if(!lv_anim_start(&a)) {card_motion(card,1024);card_done(NULL);}
}
static void quick_settings(lv_event_t *event)
{
    unsigned page=(unsigned)(uintptr_t)lv_event_get_user_data(event);
    ouse_shell_foreground();shell.config.open_settings(page);
}
static void theme_clicked(lv_event_t *event)
{
    (void)event;shell.save_failed=ouse_core_set_theme(ouse_theme_get()==OUSE_THEME_DARK ? OUSE_THEME_LIGHT : OUSE_THEME_DARK)!=ESP_OK;
    refresh(NULL);
}
static void bluetooth_clicked(lv_event_t *event)
{
    if(lv_event_get_code(event)==LV_EVENT_LONG_PRESSED){ouse_shell_foreground();shell.config.open_settings(13);return;}
    ouse_ble_state_t state;ouse_ble_get(&state);(void)ouse_ble_enable(!state.enabled);refresh(NULL);
}
static void usb_clicked(lv_event_t *event)
{
    if(lv_event_get_code(event)==LV_EVENT_LONG_PRESSED){ouse_shell_foreground();shell.config.open_settings(10);return;}
    ouse_usb_state_t state;ouse_usb_get(&state);if(state.available)(void)ouse_usb_set_enabled(!state.enabled);refresh(NULL);
}
static void bluetooth_draw(lv_event_t *event)
{
    lv_area_t bounds;lv_obj_get_coords(lv_event_get_target_obj(event),&bounds);
    const int points[][2]={{2,4},{15,14},{8,19},{8,1},{15,6},{2,16}};
    lv_draw_line_dsc_t line;lv_draw_line_dsc_init(&line);line.width=D(2);line.color=lv_color_hex(ouse_theme_colors().text);line.opa=shell.bluetooth_connected ? LV_OPA_COVER : LV_OPA_50;
    for(unsigned index=0;index<5;++index){line.p1=(lv_point_precise_t){bounds.x1+D(points[index][0]),bounds.y1+D(points[index][1])};line.p2=(lv_point_precise_t){bounds.x1+D(points[index+1][0]),bounds.y1+D(points[index+1][1])};lv_draw_line(lv_event_get_layer(event),&line);}
}
static void save_quick(lv_event_t *event)
{
    (void)event;if(shell.brightness_failed)return;
    shell.save_failed=shell.config.save_brightness()!=ESP_OK;
    if(shell.save_failed)ouse_dialog_show("设置未能保存，请重试",NULL,NULL);
}
static void brightness_changed(lv_event_t *event)
{
    uint8_t value=(uint8_t)lv_slider_get_value(lv_event_get_target_obj(event));
    shell.brightness_failed=shell.config.set_brightness(value)!=ESP_OK;
    shell.save_failed=false;
    if(shell.brightness_failed){lv_slider_set_value(shell.slider,shell.config.brightness(),LV_ANIM_OFF);ouse_dialog_show("亮度设置失败，请重试",NULL,NULL);}
}
static void volume_changed(lv_event_t *event)
{
    unsigned value=(unsigned)lv_slider_get_value(lv_event_get_target_obj(event));
    esp_err_t error=ouse_audio_set_volume(value);
    if(error==ESP_OK && value)error=ouse_audio_set_muted(false);
    if(error!=ESP_OK)ouse_dialog_show("声音设置失败，请重试",NULL,NULL);
}
static void sound_clicked(lv_event_t *event)
{
    (void)event;ouse_audio_state_t audio;ouse_audio_get(&audio);
    if(ouse_audio_set_muted(!audio.muted)!=ESP_OK)ouse_dialog_show("声音设置失败，请重试",NULL,NULL);
    refresh(NULL);
}
static void notification_clicked(lv_event_t *event)
{
    hide(shell.banner);
    uint32_t serial=(uint32_t)(uintptr_t)lv_event_get_user_data(event),token=0;
    char app_id[OUSE_NOTIFICATION_APP_ID_SIZE];
    esp_err_t error=ouse_notifications_activate(serial,app_id,sizeof(app_id),&token);
    if(error!=ESP_OK)return;
    ouse_app_info_t info;
    if(ouse_app_find_info(app_id,&info)!=ESP_OK) {
        ouse_notifications_remove_app(app_id);rebuild_notifications();return;
    }
    // Queuing precedes app startup: app.js may subscribe immediately, or later
    // from a startup promise. The worker acknowledges only successful delivery.
    ouse_shell_foreground();
    error=ouse_app_open(app_id);
    if(error!=ESP_OK) {
        ouse_notifications_discard_open(app_id,token);
        ouse_dialog_show(ouse_ui_locale() ? "Unable to open app" : "无法打开应用",NULL,NULL);
    }
}
static void banner_close(lv_event_t *event){(void)event;hide(shell.banner);}
static void wifi_draw(lv_event_t *event)
{
    lv_layer_t *layer=lv_event_get_layer(event);lv_area_t bounds;lv_obj_get_coords(shell.wifi_icon,&bounds);
    lv_color_t color=lv_color_hex(ouse_theme_colors().text);
    for(unsigned index=0;index<3;++index){
        lv_draw_arc_dsc_t arc;lv_draw_arc_dsc_init(&arc);arc.color=color;arc.width=D(2);arc.rounded=1;
        arc.center=(lv_point_t){bounds.x1+D(10),bounds.y1+D(17)};arc.radius=D(3+index*4);arc.start_angle=225;arc.end_angle=315;
        arc.opa=shell.wifi_level>(int)index ? LV_OPA_COVER : LV_OPA_30;lv_draw_arc(layer,&arc);
    }
    if(!shell.wifi_level){lv_draw_line_dsc_t line;lv_draw_line_dsc_init(&line);line.color=color;line.width=D(2);line.opa=LV_OPA_COVER;line.p1=(lv_point_precise_t){bounds.x1+D(3),bounds.y1+D(4)};line.p2=(lv_point_precise_t){bounds.x1+D(17),bounds.y1+D(18)};lv_draw_line(layer,&line);}
}
static void refresh_notification_banner(bool asleep)
{
    if(ouse_core_access_blocked()){hide(shell.banner);return;}
    ouse_notification_t latest={0};bool found=ouse_notifications_latest(&latest);
    ouse_app_stats_t active={0};
    bool suppress=found && ((latest.ongoing && !strcmp(latest.id,"ouse.media")) || (ouse_app_get_stats(&active)==ESP_OK && !strcmp(active.active_id,latest.app_id)));
    if(suppress){shell.banner_seen=latest.serial;hide(shell.banner);}
    if(visible(shell.quick_layer) && shell.notifications_selected && found){shell.notification_read=latest.serial;shell.banner_seen=latest.serial;}
    if(found && latest.serial>shell.notification_read)show(shell.notification_dot);else hide(shell.notification_dot);
    if(asleep || visible(shell.quick_layer) || visible(shell.recents) || ouse_dialog_active())hide(shell.banner);
    else if(found && !suppress && latest.serial>shell.banner_seen){
        shell.banner_seen=latest.serial;shell.banner_serial=latest.serial;shell.banner_until=lv_tick_get()+4500;
        if(!latest.ongoing)(void)ouse_audio_test_tone();
        lv_label_set_text(shell.banner_title,latest.title);lv_label_set_text(shell.banner_body,latest.body);
        lv_obj_remove_event_cb(shell.banner,notification_clicked);lv_obj_add_event_cb(shell.banner,notification_clicked,LV_EVENT_CLICKED,(void *)(uintptr_t)latest.serial);
        lv_obj_set_y(shell.banner,D(42));show(shell.banner);
        lv_anim_t animation;lv_anim_init(&animation);lv_anim_set_var(&animation,shell.banner);lv_anim_set_values(&animation,-D(90),D(42));lv_anim_set_duration(&animation,180);lv_anim_set_exec_cb(&animation,(lv_anim_exec_xcb_t)lv_obj_set_y);lv_anim_set_path_cb(&animation,lv_anim_path_ease_out);lv_anim_start(&animation);
    }
    if(visible(shell.banner) && ((!found || latest.serial!=shell.banner_serial) || (int32_t)(lv_tick_get()-shell.banner_until)>=0))hide(shell.banner);
    if(visible(shell.banner))lv_obj_move_foreground(shell.banner);
}
static void notification_dismissed(lv_event_t *event)
{
    (void)ouse_notifications_dismiss((uint32_t)(uintptr_t)lv_event_get_user_data(event));
    // Refresh after this input event, so deleting the card cannot invalidate
    // the button's current event dispatch.
    shell.notification_generation=UINT32_MAX;
}
static void notifications_clear(lv_event_t *event)
{ (void)event;ouse_notifications_clear_all();shell.notification_generation=UINT32_MAX; }
static void release_notification_cards(void)
{
    lv_obj_clean(shell.notification_list);
    for(unsigned i=0;i<shell.notification_icon_count;++i) {
        lv_image_cache_drop(&shell.notification_icons[i].image);
        lv_free((void *)shell.notification_icons[i].image.data);
        memset(&shell.notification_icons[i],0,sizeof(shell.notification_icons[i]));
    }
    shell.notification_icon_count=0;shell.notification_count=0;
}
static const lv_image_dsc_t *notification_icon_copy(const ouse_app_info_t *info)
{
    if(!info->icon_image||!info->icon_image->data||!info->icon_image->data_size||
       info->icon_image->data_size>96u*96u*4u)return NULL;
    for(unsigned i=0;i<shell.notification_icon_count;++i)
        if(!strcmp(shell.notification_icons[i].app_id,info->id))return &shell.notification_icons[i].image;
    if(shell.notification_icon_count==OUSE_NOTIFICATION_CAPACITY)return NULL;
    uint8_t *pixels=lv_malloc(info->icon_image->data_size);if(!pixels)return NULL;
    memcpy(pixels,info->icon_image->data,info->icon_image->data_size);
    unsigned index=shell.notification_icon_count++;
    strcpy(shell.notification_icons[index].app_id,info->id);
    shell.notification_icons[index].image=*info->icon_image;shell.notification_icons[index].image.data=pixels;
    return &shell.notification_icons[index].image;
}
static void rebuild_notifications(void)
{
    int scroll=lv_obj_get_scroll_y(shell.notification_list);
    // Images are owned snapshots; no package-owned image pointer can survive
    // application replacement/uninstall. LVGL decoders close before freeing.
    release_notification_cards();
    // Read revision first: a concurrent publish after the snapshot must still
    // cause the next refresh, rather than being mistaken for rendered data.
    shell.notification_generation=ouse_notifications_generation();
    shell.notification_count=ouse_notifications_list(NULL,shell.notification_items,OUSE_NOTIFICATION_CAPACITY);
    shell.notification_ui_generation=ouse_ui_generation();
    shell.notification_age_minute=(uint32_t)((uint64_t)esp_timer_get_time()/60000000);
    int header=ouse_ui_font(32)->line_height+ouse_ui_font(16)->line_height+D(40);
    rect(shell.notification_clock,D(20),D(14),shell.width-D(40),ouse_ui_font(32)->line_height);
    lv_obj_t *caption=lv_obj_get_child(shell.notification_panel,1);
    rect(caption,D(22),D(24)+ouse_ui_font(32)->line_height,shell.width-D(44),ouse_ui_font(16)->line_height);
    rect(shell.notification_list,D(12),header,shell.width-D(24),shell.height-header-D(78));
    lv_obj_align(shell.notification_clear,LV_ALIGN_BOTTOM_RIGHT,-D(16),-D(28));
    unsigned ordinary=0;
    for(unsigned i=0;i<shell.notification_count;++i) {
        const ouse_notification_t *record=&shell.notification_items[i];
        ouse_app_info_t info;
        if(ouse_app_find_info(record->app_id,&info)!=ESP_OK)continue;
        lv_obj_t *card=ouse_widget_box(shell.notification_list,OUSE_STYLE_PANEL,20);
        lv_obj_set_width(card,LV_PCT(100));lv_obj_set_height(card,LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(card,D(14),0);lv_obj_set_style_pad_row(card,D(6),0);
        lv_obj_set_flex_flow(card,LV_FLEX_FLOW_COLUMN);lv_obj_add_flag(card,LV_OBJ_FLAG_CLICKABLE);
        ouse_theme_apply(card,OUSE_STYLE_PRESSED,LV_STATE_PRESSED);
        lv_obj_add_event_cb(card,notification_clicked,LV_EVENT_CLICKED,(void *)(uintptr_t)record->serial);
        lv_obj_t *row=ouse_widget_box(card,OUSE_STYLE_PANEL,0);
        lv_obj_set_style_bg_opa(row,LV_OPA_TRANSP,0);
        lv_obj_set_width(row,LV_PCT(100));lv_obj_set_height(row,LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row,D(7),0);
        lv_obj_set_flex_align(row,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
        info.icon_image=notification_icon_copy(&info);
        lv_obj_t *icon=ouse_widget_app_icon(row,&info,22);lv_obj_set_style_radius(icon,D(6),0);
        lv_obj_t *name=ouse_widget_text(row,"",12);ouse_widget_app_name(name,&info);
        lv_obj_set_width(name,0);lv_obj_set_flex_grow(name,1);lv_label_set_long_mode(name,LV_LABEL_LONG_MODE_DOTS);
        ouse_theme_apply(name,OUSE_STYLE_MUTED,0);
        uint64_t age=((uint64_t)esp_timer_get_time()/1000-record->updated_ms)/60000;
        char time[32];
        if(!age)snprintf(time,sizeof(time),"%s",ouse_ui_locale() ? "Now" : "现在");
        else if(age<60)snprintf(time,sizeof(time),ouse_ui_locale() ? "%llum" : "%llu 分钟",(unsigned long long)age);
        else snprintf(time,sizeof(time),ouse_ui_locale() ? "%lluh" : "%llu 小时",(unsigned long long)(age/60));
        lv_obj_t *when=ouse_widget_text(row,"",12);ouse_ui_unbind_text(when);lv_label_set_text(when,time);
        ouse_theme_apply(when,OUSE_STYLE_MUTED,0);
        if(!record->ongoing) {
            ++ordinary;
            lv_obj_t *dismiss=ouse_widget_button(row,LV_SYMBOL_CLOSE,notification_dismissed,(void *)(uintptr_t)record->serial);
            lv_obj_set_style_bg_opa(dismiss,LV_OPA_TRANSP,0);
            lv_obj_set_style_min_width(dismiss,D(30),0);lv_obj_set_style_min_height(dismiss,D(30),0);
            lv_obj_set_style_pad_all(dismiss,D(4),0);
            lv_obj_t *glyph=lv_obj_get_child(dismiss,0);ouse_ui_use_font(glyph,0);lv_obj_set_style_text_font(glyph,&lv_font_montserrat_16,0);
        }
        lv_obj_t *title=ouse_widget_text(card,"",16);ouse_ui_unbind_text(title);lv_label_set_text(title,record->title);
        lv_obj_set_width(title,LV_PCT(100));
        if(record->body[0]) {
            lv_obj_t *body=ouse_widget_text(card,"",14);ouse_ui_unbind_text(body);lv_label_set_text(body,record->body);
            lv_obj_set_width(body,LV_PCT(100));ouse_theme_apply(body,OUSE_STYLE_MUTED,0);
        }
    }
    if(ordinary)show(shell.notification_clear);else hide(shell.notification_clear);
    if(!shell.notification_count) {
        lv_obj_t *empty=ouse_widget_text(shell.notification_list,"",16);
        ouse_ui_text_pair(empty,"暂无通知","No notifications");lv_obj_set_width(empty,LV_PCT(100));
        lv_obj_set_style_text_align(empty,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_margin_top(empty,D(56),0);
        ouse_theme_apply(empty,OUSE_STYLE_MUTED,0);
    }
    lv_obj_update_layout(shell.notification_list);lv_obj_scroll_to_y(shell.notification_list,scroll,LV_ANIM_OFF);
}
static void select_sheet(bool notifications)
{
    lv_obj_set_x(shell.quick,0);lv_obj_set_x(shell.notification_panel,0);
    shell.notifications_selected=notifications;
    if(notifications) {hide(shell.quick);show(shell.notification_panel);rebuild_notifications();}
    else {show(shell.quick);hide(shell.notification_panel);layout_quick();shell.generation=ouse_ui_generation();}
}
static lv_obj_t *quick_symbol(lv_obj_t *parent,const char *symbol)
{
    lv_obj_t *glyph=lv_label_create(parent);lv_obj_remove_flag(glyph,LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(glyph,symbol);lv_obj_set_style_text_font(glyph,&lv_font_montserrat_24,0);
    lv_obj_set_style_transform_scale_x(glyph,D(256),0);lv_obj_set_style_transform_scale_y(glyph,D(256),0);
    return glyph;
}
static lv_obj_t *quick_icon_button(lv_obj_t *parent,const char *symbol,lv_event_cb_t callback,void *data)
{
    lv_obj_t *button=ouse_widget_box(parent,OUSE_STYLE_PANEL,22);
    lv_obj_add_flag(button,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_size(button,D(44),D(44));
    ouse_theme_apply(button,OUSE_STYLE_PRESSED,LV_STATE_PRESSED);
    lv_obj_add_event_cb(button,callback,LV_EVENT_CLICKED,data);
    lv_obj_t *glyph=quick_symbol(button,symbol);lv_obj_center(glyph);return button;
}
static void quick_tile_create(quick_tile_t *tile,lv_obj_t *parent,const char *symbol,const char *zh,const char *en,unsigned page,lv_event_cb_t callback)
{
    tile->button=ouse_widget_box(parent,OUSE_STYLE_PANEL,20);
    lv_obj_add_flag(tile->button,LV_OBJ_FLAG_CLICKABLE);
    ouse_theme_apply(tile->button,OUSE_STYLE_PRESSED,LV_STATE_PRESSED);
    lv_obj_add_event_cb(tile->button,callback ? callback : quick_settings,LV_EVENT_CLICKED,(void *)(uintptr_t)page);
    tile->icon=quick_symbol(tile->button,symbol);
    tile->title=ouse_widget_text(tile->button,"",14);ouse_ui_text_pair(tile->title,zh,en);
    tile->status=ouse_widget_text(tile->button,"",11);
    lv_label_set_long_mode(tile->title,LV_LABEL_LONG_MODE_DOTS);lv_label_set_long_mode(tile->status,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_opa(tile->status,LV_OPA_80,0);
}
static void quick_control_color(lv_obj_t *object,bool active)
{
    ouse_theme_colors_t colors=ouse_theme_colors();
    background_changed(object,lv_color_hex(active ? colors.accent : colors.surface));
    text_color_changed(object,lv_color_hex(active ? (ouse_theme_get()==OUSE_THEME_DARK ? 0x122D3B : 0xffffff) : colors.text));
}
static void quick_status_pair(lv_obj_t *label,const char *zh,const char *en)
{
    if(strcmp(lv_label_get_text(label),ouse_ui_locale() ? en : zh))ouse_ui_text_pair(label,zh,en);
}
static void quick_sun(lv_obj_t *parent)
{
    // A small native sun avoids depending on an unavailable symbol glyph.
    lv_obj_t *sun=ouse_widget_box(parent,OUSE_STYLE_DESKTOP,0);lv_obj_set_style_bg_opa(sun,LV_OPA_TRANSP,0);
    rect(sun,D(16),D(16),D(24),D(24));
    lv_obj_t *center=ouse_widget_box(sun,OUSE_STYLE_PILL,4);rect(center,D(8),D(8),D(8),D(8));
    static const int rays[][4]={{11,0,2,4},{11,20,2,4},{0,11,4,2},{20,11,4,2},{3,3,3,3},{18,3,3,3},{3,18,3,3},{18,18,3,3}};
    for(unsigned i=0;i<8;++i){lv_obj_t *ray=ouse_widget_box(sun,OUSE_STYLE_PILL,1);rect(ray,D(rays[i][0]),D(rays[i][1]),D(rays[i][2]),D(rays[i][3]));}
}
static void quick_tile_layout(quick_tile_t *tile,int x,int y,int width,int height)
{
    rect(tile->button,x,y,width,height);
    lv_obj_align(tile->icon,LV_ALIGN_LEFT_MID,D(12),0);
    int title=ouse_ui_font(14)->line_height,status=ouse_ui_font(11)->line_height;
    int top=(height-title-status-D(2))/2;
    rect(tile->title,D(48),top,width-D(60),title);
    rect(tile->status,D(48),top+title+D(2),width-D(60),status);
}
static void quick_capsule_layout(lv_obj_t *capsule,lv_obj_t *slider,int y,int width,int height)
{
    rect(capsule,0,y,width,height);
    rect(slider,D(56),(height-D(32))/2,width-D(72),D(32));
}
static void layout_quick(void)
{
    int clock_height=ouse_ui_font(20)->line_height,date_height=ouse_ui_font(11)->line_height;
    int header=clock_height+date_height+D(12);if(header<D(64))header=D(64);
    int top=(header-clock_height-date_height)/2;
    rect(shell.quick_clock,D(20),top,shell.width-D(92),clock_height);
    rect(shell.date,D(20),top+clock_height,shell.width-D(92),date_height);
    rect(shell.gear,shell.width-D(60),(header-D(44))/2,D(44),D(44));
    int width=shell.width-D(32),tile_height=ouse_ui_font(14)->line_height+ouse_ui_font(11)->line_height+D(14);
    if(tile_height<D(64))tile_height=D(64);
    rect(shell.quick_body,D(16),header+D(8),width,shell.height-header-D(46));
    if(shell.width>shell.height){
        int tile_width=(width-D(24))/3;
        quick_tile_t *tiles[]={&shell.wifi_tile,&shell.bluetooth_tile,&shell.sound_tile,&shell.power_tile,&shell.theme_tile,&shell.usb_tile};
        for(unsigned i=0;i<6;++i)quick_tile_layout(tiles[i],(i%3)*(tile_width+D(12)),(i/3)*(tile_height+D(12)),tile_width,tile_height);
        quick_capsule_layout(shell.volume_box,shell.volume_slider,2*(tile_height+D(12)),width,D(56));
        lv_obj_align(shell.volume_icon,LV_ALIGN_LEFT_MID,D(16),0);
        quick_capsule_layout(shell.light,shell.slider,2*(tile_height+D(12))+D(68),width,D(56));return;
    }
    int tile_width=(width-D(12))/2;
    quick_tile_layout(&shell.wifi_tile,0,0,tile_width,tile_height);
    quick_tile_layout(&shell.bluetooth_tile,tile_width+D(12),0,width-tile_width-D(12),tile_height);
    quick_tile_layout(&shell.sound_tile,0,tile_height+D(12),tile_width,tile_height);
    quick_tile_layout(&shell.power_tile,tile_width+D(12),tile_height+D(12),width-tile_width-D(12),tile_height);
    quick_tile_layout(&shell.theme_tile,0,2*(tile_height+D(12)),tile_width,tile_height);
    quick_tile_layout(&shell.usb_tile,tile_width+D(12),2*(tile_height+D(12)),width-tile_width-D(12),tile_height);
    quick_capsule_layout(shell.volume_box,shell.volume_slider,3*(tile_height+D(12)),width,D(56));
    lv_obj_align(shell.volume_icon,LV_ALIGN_LEFT_MID,D(16),0);
    quick_capsule_layout(shell.light,shell.slider,3*(tile_height+D(12))+D(68),width,D(56));
}
static void refresh(lv_timer_t *timer)
{
    if(visible(shell.game_hint) && (uint32_t)(lv_tick_get()-shell.game_hint_at)>=2000)hide(shell.game_hint);
    (void)timer;char time[8],date[64],battery[32];ouse_navigation_clock(time);
    if(!ouse_core_access_blocked() && shell.navigation && !shell.g.claimed && !shell.card_animating)lv_obj_move_foreground(shell.nav);
    lv_color_t navigation_color=lv_color_hex(ouse_theme_colors().text);
    for(unsigned index=0;index<3;++index)if(shell.navigation_icons[index]) {
        lv_obj_t *icon=shell.navigation_icons[index];
        if(index==0){if(!lv_color_eq(lv_obj_get_style_line_color(icon,0),navigation_color))lv_obj_set_style_line_color(icon,navigation_color,0);}
        else if(!lv_color_eq(lv_obj_get_style_border_color(icon,0),navigation_color))lv_obj_set_style_border_color(icon,navigation_color,0);
    }
    // Keep foreground chrome consistent even for apps without theme callbacks.
    if(!visible(shell.recents) && !visible(shell.flight) && !visible(shell.quick_layer))status_colors(false);
    label_changed(shell.clock,time);
    ouse_power_state_t power={0};ouse_power_get(&power);
    const char *symbol=power.charging ? LV_SYMBOL_CHARGE : power.percent>80 ? LV_SYMBOL_BATTERY_FULL : power.percent>40 ? LV_SYMBOL_BATTERY_2 : power.percent>15 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
    if(power.valid && power.percent>=0)snprintf(battery,sizeof(battery),"%s %d%%",symbol,power.percent);
    else snprintf(battery,sizeof(battery),"%s --",LV_SYMBOL_BATTERY_EMPTY);
    label_changed(shell.battery,battery);
    ouse_wifi_state_t wifi={0};ouse_wifi_get(&wifi);int level=!wifi.connected ? 0 : wifi.rssi>=-60 ? 3 : wifi.rssi>=-75 ? 2 : 1;
    bool colors_changed=shell.icon_generation!=ouse_ui_generation();
    if(level!=shell.wifi_level || colors_changed){shell.wifi_level=level;lv_obj_invalidate(shell.wifi_icon);}
    ouse_ble_state_t bluetooth;ouse_ble_get(&bluetooth);
    if(shell.bluetooth_connected!=bluetooth.connected || colors_changed)lv_obj_invalidate(shell.bluetooth_icon);
    shell.bluetooth_connected=bluetooth.connected;shell.icon_generation=ouse_ui_generation();
    if(bluetooth.enabled)show(shell.bluetooth_icon);else hide(shell.bluetooth_icon);
    refresh_notification_banner(power.asleep);
    // Quick settings can change text size while overview stays open beneath
    // it. Text bindings update the font; resize its explicit line box too.
    if(visible(shell.recents) && shell.recent_generation!=ouse_ui_generation()) {
        shell.recent_generation=ouse_ui_generation();
        for(unsigned i=0;i<shell.count;++i)if(shell.card_titles[i])
            lv_obj_set_height(shell.card_titles[i],ouse_ui_font(14)->line_height);
    }
    if(!visible(shell.quick_layer)) {
        // Release visual copies after event delivery has completed. Only the
        // compact native notification records remain while the panel is closed.
        if(lv_obj_get_child_count(shell.notification_list) &&
           lv_indev_get_state(shell.config.touch)==LV_INDEV_STATE_RELEASED)release_notification_cards();
        return;
    }
    if(shell.notifications_selected) {
        label_changed(shell.notification_clock,time);
        if((shell.notification_generation!=ouse_notifications_generation() ||
            shell.notification_ui_generation!=ouse_ui_generation() ||
            shell.notification_age_minute!=(uint32_t)((uint64_t)esp_timer_get_time()/60000000)) &&
            lv_indev_get_state(shell.config.touch)==LV_INDEV_STATE_RELEASED)rebuild_notifications();
        return;
    }
    label_changed(shell.quick_clock,time);ouse_navigation_date(date,sizeof(date),!ouse_ui_locale());
    ouse_ui_text(shell.date,date);
    if(shell.generation!=ouse_ui_generation()) {shell.generation=ouse_ui_generation();layout_quick();}
    quick_control_color(shell.theme_tile.button,ouse_theme_get()==OUSE_THEME_DARK);
    ouse_usb_state_t usb;ouse_usb_get(&usb);quick_control_color(shell.usb_tile.button,usb.available && usb.enabled);
    if(!usb.available)quick_status_pair(shell.usb_tile.status,"不可用","Unavailable");
    else if(!usb.enabled)quick_status_pair(shell.usb_tile.status,"已关闭","Off");
    else if(usb.mounted)quick_status_pair(shell.usb_tile.status,"已连接","Connected");
    else quick_status_pair(shell.usb_tile.status,"未连接","Disconnected");
    quick_control_color(shell.bluetooth_tile.button,bluetooth.enabled);
    if(!bluetooth.enabled)quick_status_pair(shell.bluetooth_tile.status,"已关闭","Off");
    else if(bluetooth.connected)quick_status_pair(shell.bluetooth_tile.status,"已连接","Connected");
    else quick_status_pair(shell.bluetooth_tile.status,"未连接","Disconnected");
    quick_status_pair(shell.theme_tile.status,ouse_theme_get()==OUSE_THEME_DARK ? "深色" : "浅色",ouse_theme_get()==OUSE_THEME_DARK ? "Dark" : "Light");
    if(!lv_obj_has_state(shell.slider,LV_STATE_PRESSED))lv_slider_set_value(shell.slider,shell.config.brightness(),LV_ANIM_OFF);
    ouse_wifi_state_t w={0};ouse_wifi_get(&w);
    quick_control_color(shell.wifi_tile.button,w.connected);
    if(w.connected){ouse_ui_unbind_text(shell.wifi_tile.status);label_changed(shell.wifi_tile.status,w.ssid);}
    else if(w.connecting)quick_status_pair(shell.wifi_tile.status,"连接中","Connecting");
    else if(w.enabled)quick_status_pair(shell.wifi_tile.status,"未连接","Disconnected");
    else quick_status_pair(shell.wifi_tile.status,"已关闭","Off");
    quick_control_color(shell.power_tile.button,ouse_core_landscape());
    label_changed(shell.power_tile.icon,LV_SYMBOL_REFRESH);
    quick_status_pair(shell.power_tile.status,ouse_core_landscape()?"横屏":"竖屏",ouse_core_landscape()?"Landscape":"Portrait");
    ouse_audio_state_t audio={0};ouse_audio_get(&audio);
    if(!lv_obj_has_state(shell.volume_slider,LV_STATE_PRESSED))lv_slider_set_value(shell.volume_slider,audio.volume,LV_ANIM_OFF);
    label_changed(shell.volume_icon,audio.muted || !audio.volume ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);
    quick_control_color(shell.sound_tile.button,!audio.muted&&audio.volume>0);
    quick_status_pair(shell.sound_tile.status,audio.muted ? "静音" : audio.volume ? "已开启" : "已关闭",audio.muted ? "Muted" : audio.volume ? "On" : "Off");
    label_changed(shell.sound_tile.icon,audio.muted ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);

}
static void back_action(void)
{
    if(ouse_ime_active())ouse_ime_hide();
    else if(ouse_dialog_active())ouse_dialog_dismiss();
    else if(ouse_device_ui_modal())ouse_device_ui_close_modal();
    else if(visible(shell.quick_layer))settle_quick(false);
    else if(visible(shell.recents))dismiss_recent(NULL);
    else shell.config.back();
}
static void home_tap(lv_event_t *event)
{
    if(event) {
        lv_point_t point;lv_indev_get_point(shell.config.touch,&point);
        if(shell.navigation || abs(point.x-shell.g.start.x)+abs(point.y-shell.g.start.y)>D(8))return;
    }
    if(ouse_dialog_active()||ouse_device_ui_modal())return;
    if(!ouse_apps_confirm_exit())return;
    ouse_shell_foreground();(void)ouse_app_background();
}
static void navigation_clicked(lv_event_t *event)
{
    if(ouse_core_access_blocked())return;
    lv_event_code_t code=lv_event_get_code(event);
    if(code==LV_EVENT_PRESSED){lv_indev_get_point(shell.config.touch,&shell.navigation_start);shell.navigation_cancelled=false;return;}
    if(code==LV_EVENT_PRESSING || code==LV_EVENT_RELEASED) {
        lv_point_t point;lv_indev_get_point(shell.config.touch,&point);
        if(abs(point.x-shell.navigation_start.x)+abs(point.y-shell.navigation_start.y)>D(10))shell.navigation_cancelled=true;
        return;
    }
    if(code!=LV_EVENT_CLICKED || shell.navigation_cancelled)return;
    unsigned action=(unsigned)(uintptr_t)lv_event_get_user_data(event);
    if(action==0){back_action();return;}
    if(ouse_ime_active())ouse_ime_hide();
    if(ouse_dialog_active())ouse_dialog_dismiss();
    if(ouse_device_ui_modal())ouse_device_ui_close_modal();
    if(action==1)home_tap(NULL);
    else if(visible(shell.recents))dismiss_recent(NULL);
    else {ouse_shell_foreground();show_recents();}
}
static void draw_navigation_back(lv_event_t *event)
{
    lv_area_t bounds;lv_obj_get_coords(lv_event_get_target_obj(event),&bounds);
    lv_point_precise_t points[]={
        {bounds.x1+D(14),bounds.y1},{bounds.x1,bounds.y1+D(8)},
        {bounds.x1+D(14),bounds.y1+D(16)},{bounds.x1+D(14),bounds.y1}
    };
    lv_draw_line_dsc_t line;lv_draw_line_dsc_init(&line);
    line.color=lv_color_hex(ouse_theme_colors().text);line.width=D(2);
    for(unsigned index=0;index<3;++index){line.p1=points[index];line.p2=points[index+1];lv_draw_line(lv_event_get_layer(event),&line);}
}
void ouse_shell_set_fullscreen(bool enabled)
{
    shell.fullscreen=enabled;
    if(!shell.status || !shell.nav)return;
    if(enabled){hide(shell.status);hide(shell.nav);hide(shell.bottom_guard);}
    else{show(shell.status);show(shell.nav);if(!shell.navigation)show(shell.bottom_guard);}
}
unsigned ouse_shell_navigation(void) { return shell.navigation; }
void ouse_shell_set_navigation(unsigned mode)
{
    if(mode>1 || !shell.nav)return;
    ouse_shell_foreground();
    shell.navigation=mode;
    lv_anim_delete(shell.pill,pill_motion);pill_motion(NULL,0);
    int height=D(mode ? 40 : 24);
    rect(shell.nav,0,shell.height-height,shell.width,height);
    lv_obj_set_height(shell.recents,shell.height-height);
    if(mode)hide(shell.pill);else show(shell.pill);
    if(mode)hide(shell.bottom_guard);else show(shell.bottom_guard);
    rect(shell.pill,shell.width/2-D(32),D(12),D(64),D(4));
    for(unsigned index=0;index<3;++index){rect(shell.navigation_buttons[index],(int)index*shell.width/3,0,shell.width/3,D(40));lv_obj_center(shell.navigation_icons[index]);if(mode)show(shell.navigation_buttons[index]);else hide(shell.navigation_buttons[index]);}
    if(shell.config.navigation_changed)shell.config.navigation_changed(height);
}
static void gesture_step(lv_timer_t *timer)
{
    (void)timer;
    if(shell.g.kind==G_NONE){lv_timer_pause(shell.gesture_timer);return;}
    if(lv_indev_get_state(shell.config.touch)==LV_INDEV_STATE_RELEASED){finish_gesture(false);return;}
    lv_point_t p;lv_indev_get_point(shell.config.touch,&p);
    int dx=p.x-shell.g.start.x,dy=p.y-shell.g.start.y;
    uint32_t now=lv_tick_get();
    if(abs(p.x-shell.g.anchor.x)+abs(p.y-shell.g.anchor.y)>D(10)) {
        shell.g.anchor=p;shell.g.last_move=now;
    }
    shell.g.last=p;shell.g.last_tick=now;
    if(shell.g.kind==G_TOP || shell.g.kind==G_CLOSE || shell.g.kind==G_SWITCH) {
        bool horizontal=shell.g.kind!=G_TOP && abs(dx)>D(12) && abs(dx)>abs(dy)+D(6);
        bool toward_other=shell.notifications_selected ? dx<0 : dx>0;
        if(!shell.g.claimed && horizontal && toward_other) {
            shell.g.kind=G_SWITCH;
            if(shell.notifications_selected){layout_quick();show(shell.quick);}
            else {rebuild_notifications();show(shell.notification_panel);}
            bool previous=shell.notifications_selected;shell.notifications_selected=!previous;
            refresh(NULL);shell.notifications_selected=previous;
            claim();
        }
        if(shell.g.kind==G_SWITCH) {
            if(shell.g.claimed) {
                int x=clamp(dx,-shell.width,shell.width);
                lv_obj_set_x(shell.notification_panel,shell.notifications_selected ? x : x-shell.width);
                lv_obj_set_x(shell.quick,shell.notifications_selected ? x+shell.width : x);
            } else if(abs(dy)>abs(dx)+D(8)){shell.g.kind=G_NONE;lv_timer_pause(shell.gesture_timer);}
            return;
        }
        bool correct=shell.g.kind==G_TOP ? dy>D(6) : dy<-D(6);
        if(!shell.g.claimed && correct && abs(dy)>abs(dx)) {
            ouse_ime_hide();
            show(shell.quick_layer);lv_obj_move_foreground(shell.quick_layer);
            refresh(NULL);
            lv_anim_delete(shell.sheet,move_quick);shell.quick_animating=false;claim();
        }
        if(shell.g.claimed)move_quick(NULL,shell.g.sheet_start+dy);
    } else if(shell.g.kind==G_BACK) {
        int inward=shell.g.left ? dx : -dx;
        if(!shell.g.claimed && inward>D(6) && inward>abs(dy))claim();
        if(shell.g.claimed)back_motion(NULL,clamp(inward*1024/D(72),0,1024));
    } else if(shell.g.kind==G_BOTTOM) {
        if(!shell.g.claimed && -dy>D(8) && -dy>abs(dx)) {
            if(!shell.g.from_recents)follow_home(dy);
            claim();
            // Taking the app preview can cost a frame. That time is not a
            // stationary finger hold and must not open overview on a fast swipe.
            shell.g.last_move=lv_tick_get();
        }
        if(shell.g.claimed) {
            if(shell.g.from_recents){pill_motion(NULL,-clamp(-dy/8+ D(6),D(6),D(12)));return;}
            follow_home(dy);
            // Show overview while still pressed after a brief pause. The
            // movement tolerance above accommodates normal finger jitter.
            if(-dy>=D(32) && lv_tick_elaps(shell.g.last_move)>=90) {
                show_recents();shell.g.kind=G_LOCKED;show(shell.shield);lv_obj_move_foreground(shell.shield);
            }
        }
    } else if(shell.g.kind==G_CARD && shell.g.card>=0) {
        if(!shell.g.claimed && abs(dx)>abs(dy)+D(8)) {shell.g.kind=G_NONE;lv_timer_pause(shell.gesture_timer);return;}
        if(!shell.g.claimed && -dy>D(8) && -dy>abs(dx))claim();
        if(shell.g.claimed) {
            lv_obj_t *card=shell.cards[shell.g.card];
            lv_obj_set_y(card,D(50)+clamp(dy,-shell.card_height,0));
            lv_obj_set_style_opa(card,(lv_opa_t)clamp(255+dy*180/shell.card_height,60,255),0);
        }
    }
}
static void finish_gesture(bool cancel)
{
    gesture_kind_t kind=shell.g.kind;bool claimed=shell.g.claimed;
    lv_point_t p;lv_indev_get_point(shell.config.touch,&p);
    int dx=p.x-shell.g.start.x,dy=p.y-shell.g.start.y;
    lv_timer_pause(shell.gesture_timer);
    shell.g.kind=G_NONE;shell.g.claimed=false;
    if(claimed)lv_indev_reset(shell.config.touch,NULL);
    hide(shell.shield);
    if(!claimed)return;
    if(kind==G_SWITCH) {
        bool change=!cancel && abs(dx)>=D(64) && abs(dx)>abs(dy);
        select_sheet(change ? !shell.notifications_selected : shell.notifications_selected);refresh(NULL);
    } else if(kind==G_TOP || kind==G_CLOSE) {
        bool open=kind==G_TOP ? !cancel && dy>=D(80) : cancel || dy>-D(64);
        settle_quick(open);
    } else if(kind==G_BACK) {
        bool commit=!cancel && (shell.g.left ? dx : -dx)>=D(48) && abs(dx)>=abs(dy);
        rebound_back();if(commit)back_action();
    } else if(kind==G_BOTTOM) {
        if(shell.g.from_recents){if(!cancel && -dy>=D(32))dismiss_recent(NULL);else settle_pill(true);}
        else if(!cancel && -dy>=D(32))flight_home();
        else {
            char id[48];strcpy(id,shell.g.origin);flight_clear();
            if(id[0])(void)ouse_app_open(id);
            settle_pill(false);
        }
    } else if(kind==G_CARD && shell.g.card>=0 && (unsigned)shell.g.card<shell.count) {
        settle_card((unsigned)shell.g.card,!cancel && dy<=-D(64));
    }
}
static void pointer_event(lv_event_t *event)
{
    if(ouse_core_access_blocked())return;
    if(shell.card_animating)return;
    lv_event_code_t code=lv_event_get_code(event);
    if(code==LV_EVENT_RELEASED){finish_gesture(false);return;}
    if(code!=LV_EVENT_PRESSED)return;
    if(ouse_core_launch_active()){
        lv_point_t point;lv_indev_get_point(shell.config.touch,&point);
        if(point.x<D(16) || point.x>=shell.width-D(16) || point.y>=shell.height-D(40)){
            ouse_core_launch_cancel();lv_indev_reset(shell.config.touch,NULL);
        }
        return;
    }
    if(shell.g.claimed)finish_gesture(true);
    lv_anim_delete(shell.back_indicator,back_motion);hide(shell.back_indicator);
    memset(&shell.g,0,sizeof(shell.g));shell.g.card=-1;
    lv_indev_get_point(shell.config.touch,&shell.g.start);shell.g.last=shell.g.anchor=shell.g.start;
    shell.g.last_move=shell.g.last_tick=lv_tick_get();
    lv_point_t p=shell.g.start;
    if(shell.navigation && p.y>=shell.height-D(40))return;
    bool quick=visible(shell.quick_layer),recent=visible(shell.recents);
    if((ouse_dialog_active()||ouse_device_ui_modal()) && !(p.x<=D(24)||p.x>=shell.width-D(24)))return;
    lv_obj_t *target=lv_indev_get_active_obj();
    bool control=false;
    for(lv_obj_t *obj=target;obj;obj=lv_obj_get_parent(obj))if(lv_obj_check_type(obj,&lv_slider_class))control=true;
    if(!quick && p.y<=D(shell.fullscreen?8:36)) {
        // The starting side selects the panel for the entire drag, even if
        // the finger crosses the middle of the screen afterwards.
        select_sheet(p.x<shell.width/2);
        shell.g.kind=G_TOP;shell.g.sheet_start=-shell.quick_height;move_quick(NULL,-shell.quick_height);
    } else if(quick && !control && (p.y<D(88) || p.y>=shell.height-D(38))) {
        shell.g.kind=G_CLOSE;shell.g.sheet_start=lv_obj_get_y(shell.sheet);
    } else if(quick && !control) {
        shell.g.kind=G_SWITCH;
    } else if((!shell.navigation || shell.fullscreen) && !quick && p.y>=shell.height-D(shell.fullscreen?8:36)){shell.g.kind=G_BOTTOM;shell.g.from_recents=recent;}
    else if(!shell.fullscreen && !shell.navigation && !control && p.y>D(40) && p.y<shell.height-D(28) && (p.x<=D(24) || p.x>=shell.width-D(24))) {
        shell.g.kind=G_BACK;shell.g.left=p.x<shell.width/2;
        lv_label_set_text(shell.back_arrow,shell.g.left ? LV_SYMBOL_LEFT : LV_SYMBOL_RIGHT);
        lv_anim_delete(shell.back_indicator,back_motion);
    } else if(recent) {
        for(unsigned i=0;i<shell.count;++i)if(contains(shell.cards[i],p)){shell.g.kind=G_CARD;shell.g.card=(int)i;break;}
    }
    if(shell.g.kind!=G_NONE)lv_timer_resume(shell.gesture_timer);
}
void ouse_shell_get_state(ouse_shell_state_t *out)
{
    *out=(ouse_shell_state_t){.fullscreen=shell.fullscreen,.quick=visible(shell.quick_layer)&&!shell.notifications_selected,
        .notifications=visible(shell.quick_layer)&&shell.notifications_selected,
        .notification_cards=shell.notification_count,.recents=visible(shell.recents),.tracking=shell.g.claimed,
        .quick_y=lv_obj_get_y(shell.sheet),.back_progress=shell.g.back_progress,.cards=shell.count,
        .recent_scroll_x=lv_obj_get_scroll_x(shell.strip),
        .snapshot_bytes=shell.flight_source.data_size,.bar_offset=lv_obj_get_style_translate_y(shell.pill,0)};
    for(unsigned i=0;i<shell.count;++i)out->snapshot_bytes+=shell.images[i].data_size;
}
void ouse_shell_init(const ouse_shell_config_t *config)
{
    shell.config=*config;shell.width=lv_obj_get_width(config->screen);shell.height=lv_obj_get_height(config->screen);
    shell.quick_height=shell.height;shell.g.card=-1;
    lv_obj_t *status=ouse_widget_box(config->screen,OUSE_STYLE_DESKTOP,0);shell.status=status;rect(status,0,0,shell.width,D(40));
    shell.clock=ouse_widget_text(status,"--:--",14);lv_obj_set_pos(shell.clock,D(16),D(8));
    lv_obj_t *battery=lv_label_create(status);shell.battery=battery;lv_label_set_text(battery,LV_SYMBOL_BATTERY_EMPTY " --");
    lv_obj_set_style_text_font(battery,&lv_font_montserrat_16,0);lv_obj_set_pos(battery,shell.width-D(66),D(8));
    lv_obj_set_style_transform_scale_x(battery,D(256),0);lv_obj_set_style_transform_scale_y(battery,D(256),0);
    ouse_theme_apply(battery,OUSE_STYLE_MUTED,0);
    shell.wifi_level=-1;shell.wifi_icon=ouse_widget_box(status,OUSE_STYLE_DESKTOP,0);rect(shell.wifi_icon,shell.width-D(92),D(7),D(20),D(20));lv_obj_set_style_bg_opa(shell.wifi_icon,LV_OPA_TRANSP,0);lv_obj_add_event_cb(shell.wifi_icon,wifi_draw,LV_EVENT_DRAW_MAIN,NULL);
    shell.bluetooth_icon=ouse_widget_box(status,OUSE_STYLE_DESKTOP,0);rect(shell.bluetooth_icon,shell.width-D(116),D(7),D(20),D(20));lv_obj_set_style_bg_opa(shell.bluetooth_icon,LV_OPA_TRANSP,0);lv_obj_add_event_cb(shell.bluetooth_icon,bluetooth_draw,LV_EVENT_DRAW_MAIN,NULL);
    shell.notification_dot=ouse_widget_box(status,OUSE_STYLE_PILL,4);rect(shell.notification_dot,D(68),D(14),D(6),D(6));lv_obj_set_style_bg_color(shell.notification_dot,lv_color_hex(0x3988ff),0);hide(shell.notification_dot);
    shell.banner=ouse_widget_box(config->screen,OUSE_STYLE_PANEL,18);rect(shell.banner,D(8),D(42),shell.width-D(16),D(84));lv_obj_add_flag(shell.banner,LV_OBJ_FLAG_CLICKABLE);
    shell.banner_title=ouse_widget_text(shell.banner,"",18);rect(shell.banner_title,D(12),D(10),shell.width-D(80),D(26));lv_label_set_long_mode(shell.banner_title,LV_LABEL_LONG_MODE_DOTS);
    shell.banner_body=ouse_widget_text(shell.banner,"",16);rect(shell.banner_body,D(12),D(42),shell.width-D(48),D(26));lv_label_set_long_mode(shell.banner_body,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t *banner_dismiss=shell.banner_dismiss=ouse_widget_button(shell.banner,LV_SYMBOL_CLOSE,banner_close,NULL);rect(banner_dismiss,shell.width-D(60),D(6),D(34),D(32));hide(shell.banner);
    shell.recents=ouse_widget_box(config->screen,OUSE_STYLE_DESKTOP,0);rect(shell.recents,0,0,shell.width,shell.height-D(24));
    lv_obj_add_flag(shell.recents,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(shell.recents,recent_blank,LV_EVENT_CLICKED,NULL);
    shell.strip=ouse_widget_box(shell.recents,OUSE_STYLE_DESKTOP,0);rect(shell.strip,0,0,shell.width,shell.height-D(108));
    // Use actual fullscreen drawing bounds: overflow alone still loses child
    // pixels at an ancestor/opacity layer's clip during card dismissal.
    lv_obj_add_flag(shell.strip,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);lv_obj_set_scroll_dir(shell.strip,LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(shell.strip,LV_SCROLL_SNAP_CENTER);lv_obj_set_scrollbar_mode(shell.strip,LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(shell.strip,recent_blank,LV_EVENT_CLICKED,NULL);shell.card_height=shell.height-D(170);
    shell.clear=ouse_widget_button(shell.recents,"全部清除",close_all,NULL);
    rect(shell.clear,D(64),shell.height-D(88),shell.width-D(128),D(48));hide(shell.recents);
    shell.quick_layer=ouse_widget_box(config->screen,OUSE_STYLE_DESKTOP,0);rect(shell.quick_layer,0,0,shell.width,shell.height);
    lv_obj_set_style_bg_color(shell.quick_layer,lv_color_black(),0);lv_obj_add_flag(shell.quick_layer,LV_OBJ_FLAG_CLICKABLE);
    shell.sheet=ouse_widget_box(shell.quick_layer,OUSE_STYLE_DESKTOP,0);rect(shell.sheet,0,0,shell.width,shell.height);
    lv_obj_add_flag(shell.sheet,LV_OBJ_FLAG_CLICKABLE);
    shell.quick=ouse_widget_box(shell.sheet,OUSE_STYLE_DESKTOP,0);rect(shell.quick,0,0,shell.width,shell.height);
    lv_obj_add_flag(shell.quick,LV_OBJ_FLAG_CLICKABLE);
    shell.quick_clock=ouse_widget_text(shell.quick,"--:--",20);shell.date=ouse_widget_text(shell.quick,"未校时",11);
    lv_label_set_long_mode(shell.date,LV_LABEL_LONG_MODE_DOTS);
    shell.gear=quick_icon_button(shell.quick,LV_SYMBOL_SETTINGS,quick_settings,(void *)(uintptr_t)0);
    shell.quick_body=ouse_widget_box(shell.quick,OUSE_STYLE_DESKTOP,0);
    lv_obj_add_flag(shell.quick_body,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);lv_obj_set_scroll_dir(shell.quick_body,LV_DIR_VER);
    lv_obj_set_scrollbar_mode(shell.quick_body,LV_SCROLLBAR_MODE_AUTO);
    quick_tile_create(&shell.wifi_tile,shell.quick_body,LV_SYMBOL_WIFI,"Wi-Fi","Wi-Fi",7,NULL);
    quick_tile_create(&shell.bluetooth_tile,shell.quick_body,LV_SYMBOL_BLUETOOTH,"蓝牙","Bluetooth",13,bluetooth_clicked);
    lv_obj_remove_event_cb(shell.bluetooth_tile.button,bluetooth_clicked);
    lv_obj_add_event_cb(shell.bluetooth_tile.button,bluetooth_clicked,LV_EVENT_SHORT_CLICKED,NULL);
    lv_obj_add_event_cb(shell.bluetooth_tile.button,bluetooth_clicked,LV_EVENT_LONG_PRESSED,NULL);
    quick_tile_create(&shell.power_tile,shell.quick_body,LV_SYMBOL_REFRESH,"屏幕方向","Orientation",0,orientation_clicked);
    quick_tile_create(&shell.sound_tile,shell.quick_body,LV_SYMBOL_VOLUME_MAX,"声音","Sound",6,sound_clicked);
    quick_tile_create(&shell.theme_tile,shell.quick_body,LV_SYMBOL_TINT,"主题","Theme",0,theme_clicked);
    quick_tile_create(&shell.usb_tile,shell.quick_body,LV_SYMBOL_USB,"USB OTG","USB OTG",10,usb_clicked);
    lv_obj_remove_event_cb(shell.usb_tile.button,usb_clicked);
    lv_obj_add_event_cb(shell.usb_tile.button,usb_clicked,LV_EVENT_SHORT_CLICKED,NULL);
    lv_obj_add_event_cb(shell.usb_tile.button,usb_clicked,LV_EVENT_LONG_PRESSED,NULL);
    shell.volume_box=ouse_widget_box(shell.quick_body,OUSE_STYLE_PANEL,28);
    shell.volume_icon=quick_symbol(shell.volume_box,LV_SYMBOL_VOLUME_MAX);
    shell.volume_slider=lv_slider_create(shell.volume_box);ouse_widget_slider(shell.volume_slider,32);
    lv_slider_set_range(shell.volume_slider,0,100);lv_obj_add_event_cb(shell.volume_slider,volume_changed,LV_EVENT_VALUE_CHANGED,NULL);
    shell.light=ouse_widget_box(shell.quick_body,OUSE_STYLE_PANEL,28);
    quick_sun(shell.light);
    shell.slider=lv_slider_create(shell.light);ouse_widget_slider(shell.slider,32);
    lv_slider_set_range(shell.slider,10,100);lv_obj_add_event_cb(shell.slider,brightness_changed,LV_EVENT_VALUE_CHANGED,NULL);lv_obj_add_event_cb(shell.slider,save_quick,LV_EVENT_RELEASED,NULL);
    lv_obj_t *handle=shell.quick_handle=ouse_widget_box(shell.quick,OUSE_STYLE_PILL,2);rect(handle,shell.width/2-D(28),shell.height-D(18),D(56),D(4));
    shell.notification_panel=ouse_widget_box(shell.sheet,OUSE_STYLE_DESKTOP,0);
    rect(shell.notification_panel,0,0,shell.width,shell.height);
    lv_obj_add_flag(shell.notification_panel,LV_OBJ_FLAG_CLICKABLE);
    shell.notification_clock=ouse_widget_text(shell.notification_panel,"--:--",32);
    lv_obj_t *notification_title=ouse_widget_text(shell.notification_panel,"",16);
    ouse_ui_text_pair(notification_title,"通知","Notifications");
    shell.notification_list=ouse_widget_box(shell.notification_panel,OUSE_STYLE_DESKTOP,0);
    lv_obj_add_flag(shell.notification_list,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(shell.notification_list,LV_DIR_VER);lv_obj_set_scrollbar_mode(shell.notification_list,LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(shell.notification_list,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(shell.notification_list,D(10),0);
    shell.notification_clear=ouse_widget_button(shell.notification_panel,"",notifications_clear,NULL);
    ouse_ui_text_pair(lv_obj_get_child(shell.notification_clear,0),"全部清除","Clear all");
    handle=ouse_widget_box(shell.notification_panel,OUSE_STYLE_PILL,2);
    rect(handle,shell.width/2-D(28),shell.height-D(18),D(56),D(4));
    hide(shell.notification_panel);
    layout_quick();move_quick(NULL,-shell.quick_height);hide(shell.quick_layer);
    shell.bottom_guard=ouse_widget_box(config->screen,OUSE_STYLE_DESKTOP,0);
    rect(shell.bottom_guard,0,shell.height-D(36),shell.width,D(36));
    lv_obj_set_style_bg_opa(shell.bottom_guard,LV_OPA_TRANSP,0);
    lv_obj_add_flag(shell.bottom_guard,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_t *nav=ouse_widget_box(config->screen,OUSE_STYLE_DESKTOP,0);shell.nav=nav;rect(nav,0,shell.height-D(24),shell.width,D(24));
    lv_obj_add_flag(nav,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(nav,home_tap,LV_EVENT_CLICKED,NULL);
    lv_obj_t *pill=ouse_widget_box(nav,OUSE_STYLE_PILL,2);shell.pill=pill;rect(pill,shell.width/2-D(32),D(12),D(64),D(4));
    for(unsigned index=0;index<3;++index) {
        lv_obj_t *button=ouse_widget_box(nav,OUSE_STYLE_DESKTOP,0);shell.navigation_buttons[index]=button;
        rect(button,(int)index*shell.width/3,0,shell.width/3,D(40));
        lv_obj_set_style_bg_opa(button,LV_OPA_TRANSP,0);
        lv_obj_add_flag(button,LV_OBJ_FLAG_CLICKABLE);
        ouse_theme_apply(button,OUSE_STYLE_PRESSED,LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(button,LV_OPA_COVER,LV_STATE_PRESSED);
        lv_obj_add_event_cb(button,navigation_clicked,LV_EVENT_ALL,(void *)(uintptr_t)index);
        lv_obj_t *icon;
        if(index==0) {
            icon=ouse_widget_box(button,OUSE_STYLE_DESKTOP,0);
            lv_obj_set_size(icon,D(16),D(18));lv_obj_set_style_bg_opa(icon,LV_OPA_TRANSP,0);
            lv_obj_add_event_cb(icon,draw_navigation_back,LV_EVENT_DRAW_MAIN,NULL);
        } else {
            icon=ouse_widget_box(button,OUSE_STYLE_DESKTOP,index==1 ? 100 : 2);
            lv_obj_set_size(icon,D(16),D(16));lv_obj_set_style_bg_opa(icon,LV_OPA_TRANSP,0);
            lv_obj_set_style_border_width(icon,D(2),0);
        }
        shell.navigation_icons[index]=icon;lv_obj_center(icon);
        lv_obj_remove_flag(icon,LV_OBJ_FLAG_CLICKABLE);hide(button);
    }
    shell.flight=ouse_widget_box(config->screen,OUSE_STYLE_APP,24);lv_obj_set_style_clip_corner(shell.flight,true,0);
    shell.flight_image=lv_image_create(shell.flight);lv_obj_set_size(shell.flight_image,LV_PCT(100),LV_PCT(100));
    lv_image_set_inner_align(shell.flight_image,LV_IMAGE_ALIGN_STRETCH);lv_obj_remove_flag(shell.flight_image,LV_OBJ_FLAG_CLICKABLE);hide(shell.flight);
    shell.back_indicator=ouse_widget_box(config->screen,OUSE_STYLE_PANEL,34);
    shell.back_arrow=lv_label_create(shell.back_indicator);lv_label_set_text(shell.back_arrow,LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(shell.back_arrow,&lv_font_montserrat_24,0);hide(shell.back_indicator);
    shell.shield=ouse_widget_box(config->screen,OUSE_STYLE_DESKTOP,0);rect(shell.shield,0,0,shell.width,shell.height);
    lv_obj_set_style_bg_opa(shell.shield,LV_OPA_TRANSP,0);lv_obj_add_flag(shell.shield,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_PRESS_LOCK);hide(shell.shield);
    shell.gesture_timer=lv_timer_create(gesture_step,16,NULL);lv_timer_pause(shell.gesture_timer);
    lv_indev_add_event_cb(config->touch,pointer_event,LV_EVENT_PRESSED,NULL);lv_indev_add_event_cb(config->touch,pointer_event,LV_EVENT_RELEASED,NULL);
    shell.game_hint=lv_label_create(config->screen);
    lv_obj_remove_flag(shell.game_hint,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(shell.game_hint,lv_color_hex(0x172b27),0);lv_obj_set_style_bg_opa(shell.game_hint,230,0);
    lv_obj_set_style_text_color(shell.game_hint,lv_color_white(),0);lv_obj_set_style_pad_all(shell.game_hint,D(10),0);lv_obj_set_style_radius(shell.game_hint,D(12),0);hide(shell.game_hint);
    lv_timer_create(refresh,250,NULL);
    ouse_settings_state_t settings={0};
    if(config->settings_state)config->settings_state(&settings);
    ouse_shell_set_navigation(settings.requested_navigation);
}

void ouse_shell_resize(void)
{
    ouse_shell_foreground();shell.width=lv_obj_get_width(shell.config.screen);shell.height=lv_obj_get_height(shell.config.screen);shell.quick_height=shell.height;shell.card_height=shell.height-D(170);
    rect(shell.status,0,0,shell.width,D(40));lv_obj_set_x(shell.battery,shell.width-D(66));lv_obj_set_x(shell.wifi_icon,shell.width-D(92));lv_obj_set_x(shell.bluetooth_icon,shell.width-D(116));
    rect(shell.banner,D(8),D(42),shell.width-D(16),D(84));lv_obj_set_width(shell.banner_title,shell.width-D(80));lv_obj_set_width(shell.banner_body,shell.width-D(48));lv_obj_set_x(shell.banner_dismiss,shell.width-D(60));
    rect(shell.recents,0,0,shell.width,shell.height-D(24));rect(shell.strip,0,0,shell.width,shell.height-D(108));rect(shell.clear,D(64),shell.height-D(88),shell.width-D(128),D(48));clear_cards();
    rect(shell.quick_layer,0,0,shell.width,shell.height);rect(shell.sheet,0,-shell.height,shell.width,shell.height);rect(shell.quick,0,0,shell.width,shell.height);rect(shell.notification_panel,0,0,shell.width,shell.height);
    rect(shell.quick_handle,shell.width/2-D(28),shell.height-D(18),D(56),D(4));rect(shell.shield,0,0,shell.width,shell.height);rect(shell.bottom_guard,0,shell.height-D(24),shell.width,D(24));
    layout_quick();rebuild_notifications();ouse_shell_set_navigation(shell.navigation);refresh(NULL);
}
void ouse_shell_game_hint(bool show_hint)
{
    if(!shell.game_hint)return;
    if(!show_hint){hide(shell.game_hint);return;}
    ouse_ui_text(shell.game_hint,ouse_ui_locale()?"Exit again to leave":"请再次退出");
    lv_obj_set_width(shell.game_hint,LV_SIZE_CONTENT);lv_obj_align(shell.game_hint,LV_ALIGN_BOTTOM_MID,0,-D(32));
    shell.game_hint_at=lv_tick_get();lv_obj_move_foreground(shell.game_hint);show(shell.game_hint);
}
