// SPDX-License-Identifier: MIT
#include "ouse_shell.h"
#include "ouse_theme.h"
extern bool ouse_navigation_back(int,int,int,int,int,int);
extern void ouse_navigation_clock(char *);
#include <string.h>
#include <stdlib.h>

static struct {
    ouse_shell_config_t config;
    lv_obj_t *quick, *slider, *percent, *message, *theme_button, *clock;
    lv_obj_t *recents, *strip, *cards[OUSE_APP_CAPACITY];
    ouse_app_info_t items[OUSE_APP_CAPACITY];
    lv_image_dsc_t images[OUSE_APP_CAPACITY];
    unsigned count;
    int width, height;
    lv_point_t start;
    char swipe_id[48];
    bool save_failed, brightness_failed;
} shell;

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int width, int height, uint32_t color)
{
    lv_obj_t *obj=lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj,x,y); lv_obj_set_size(obj,width,height);
    ouse_theme_apply(obj, color==0xAABCD1 ? OUSE_STYLE_PILL : color==0x101B2B ? OUSE_STYLE_DESKTOP : OUSE_STYLE_PANEL, 0);
    lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}
static lv_obj_t *text(lv_obj_t *parent, const char *value, int x, int y, int width)
{
    lv_obj_t *obj=lv_label_create(parent);
    lv_label_set_text(obj,value); lv_obj_set_pos(obj,x,y); lv_obj_set_width(obj,width);
    lv_label_set_long_mode(obj,LV_LABEL_LONG_WRAP);
    return obj;
}
static lv_obj_t *button(lv_obj_t *parent, const char *value, int x, int y, int w, int h, lv_event_cb_t callback, void *data)
{
    lv_obj_t *obj=lv_button_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj,x,y); lv_obj_set_size(obj,w,h);
    lv_obj_set_style_radius(obj,12,0);
    ouse_theme_apply(obj,OUSE_STYLE_BUTTON,0);
    ouse_theme_apply(obj,OUSE_STYLE_PRESSED,LV_STATE_PRESSED);
    lv_obj_add_event_cb(obj,callback,LV_EVENT_CLICKED,data);
    lv_obj_t *caption=lv_label_create(obj); lv_label_set_text(caption,value); lv_obj_center(caption);
    return obj;
}
static void clear_cards(void)
{
    if (!shell.strip) return;
    lv_obj_clean(shell.strip); // release image decoders before their source memory
    for (unsigned i=0;i<shell.count;++i) {
        lv_free((void *)shell.images[i].data);
        memset(&shell.images[i],0,sizeof(shell.images[i]));
        shell.cards[i]=NULL;
    }
    shell.count=0;
}
void ouse_shell_foreground(void)
{
    if (!shell.recents) return;
    lv_obj_add_flag(shell.recents,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(shell.quick,LV_OBJ_FLAG_HIDDEN);
    clear_cards();
}
static void dismiss_recent(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(shell.recents,LV_OBJ_FLAG_HIDDEN); clear_cards();
}
static void show_recents(void);
static void resume_card(lv_event_t *event)
{
    char id[48]; strcpy(id,lv_event_get_user_data(event));
    dismiss_recent(NULL);
    (void)ouse_app_open(id);
}
static void remove_card(const char *source)
{
    char id[48]; strcpy(id,source);
    clear_cards();
    (void)ouse_app_close_id(id);
    show_recents();
}
static void close_card(lv_event_t *event) { remove_card(lv_event_get_user_data(event)); }
static void drag_card(lv_event_t *event)
{
    lv_point_t point; lv_indev_get_point(shell.config.touch,&point);
    int dx=point.x-shell.start.x, dy=point.y-shell.start.y;
    if (dy<0 && abs(dy)>abs(dx)) lv_obj_set_y(lv_event_get_target_obj(event),12+(dy < -220 ? -220 : dy));
}
static void show_recents(void)
{
    clear_cards();
    (void)ouse_app_background();
    lv_obj_add_flag(shell.quick,LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(shell.recents,LV_OBJ_FLAG_HIDDEN);
    for (unsigned i=0;i<OUSE_APP_CAPACITY;++i) {
        if (ouse_app_get_recent(i,&shell.items[i])!=ESP_OK) break;
        ++shell.count;
        lv_obj_t *card=box(shell.strip,48+(int)i*244,12,224,296,0x23364E);
        shell.cards[i]=card;
        lv_obj_set_style_radius(card,18,0);
        lv_obj_add_flag(card,LV_OBJ_FLAG_SNAPPABLE);
        lv_obj_add_event_cb(card,resume_card,LV_EVENT_CLICKED,shell.items[i].id);
        lv_obj_add_event_cb(card,drag_card,LV_EVENT_PRESSING,NULL);
        text(card,shell.items[i].name,14,10,160);
        lv_obj_t *close=button(card,LV_SYMBOL_CLOSE,182,6,32,28,close_card,shell.items[i].id);
        lv_obj_set_style_text_font(close,&lv_font_montserrat_16,0);
        const lv_image_dsc_t *preview=ouse_app_get_preview(shell.items[i].id);
        if (preview) {
            shell.images[i]=*preview;
            uint8_t *copy=lv_malloc(preview->data_size);
            if (copy) {
                memcpy(copy,preview->data,preview->data_size); shell.images[i].data=copy;
                lv_obj_t *image=lv_image_create(card);
                lv_image_set_src(image,&shell.images[i]);
                lv_obj_set_pos(image,12,42); lv_obj_set_size(image,200,242);
                lv_image_set_inner_align(image,LV_IMAGE_ALIGN_STRETCH);
                lv_obj_remove_flag(image,LV_OBJ_FLAG_CLICKABLE);
            } else shell.images[i].data=NULL;
        } else text(card,"点击恢复应用",18,130,190);
    }
    if (!shell.count) text(shell.strip,"暂无最近应用",70,130,200);
    lv_obj_update_layout(shell.strip);
    lv_obj_scroll_to_x(shell.strip,0,LV_ANIM_OFF);
}
static void recent_button(lv_event_t *event) { (void)event; show_recents(); }
static void home_button(lv_event_t *event)
{
    (void)event; ouse_shell_foreground(); (void)ouse_app_background();
}
static void dismiss_quick(lv_event_t *event) { (void)event; lv_obj_add_flag(shell.quick,LV_OBJ_FLAG_HIDDEN); }
static void save_quick(lv_event_t *event)
{
    (void)event;
    if (shell.brightness_failed) return;
    shell.save_failed=shell.config.save_brightness()!=ESP_OK;
    if (shell.save_failed) lv_label_set_text(shell.message,"保存失败，请重试");
}
static void change_quick(lv_event_t *event)
{
    (void)event;
    uint8_t value=(uint8_t)lv_slider_get_value(shell.slider);
    if (shell.config.set_brightness(value)!=ESP_OK) {
        shell.brightness_failed=true;
        lv_slider_set_value(shell.slider,shell.config.brightness(),LV_ANIM_OFF);
        lv_label_set_text(shell.message,"亮度设置失败，请重试");
    } else { shell.brightness_failed=false; shell.save_failed=false; }
    lv_label_set_text_fmt(shell.percent,"%u%%",shell.config.brightness());
}
static void show_quick(void)
{
    shell.save_failed=false; shell.brightness_failed=false;
    lv_slider_set_value(shell.slider,shell.config.brightness(),LV_ANIM_OFF);
    lv_label_set_text_fmt(shell.percent,"%u%%",shell.config.brightness());
    lv_obj_remove_flag(shell.quick,LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(shell.quick);
}
static void toggle_theme(lv_event_t *event)
{
    (void)event;
    shell.save_failed=ouse_core_set_theme(ouse_theme_get()==OUSE_THEME_DARK ? OUSE_THEME_LIGHT : OUSE_THEME_DARK)!=ESP_OK;
    if (shell.save_failed) lv_label_set_text(shell.message,"主题保存失败，请重试");
}
static void refresh_quick(lv_timer_t *timer)
{
    (void)timer;
    char time[8]; ouse_navigation_clock(time); lv_label_set_text(shell.clock,time);
    lv_label_set_text(lv_obj_get_child(shell.theme_button,0),ouse_theme_get()==OUSE_THEME_DARK ? "浅色模式" : "深色模式");
    if (lv_obj_has_flag(shell.quick,LV_OBJ_FLAG_HIDDEN)) return;
    if (shell.save_failed || shell.brightness_failed) return;
    ouse_settings_state_t state;
    if (shell.config.settings_state(&state)!=ESP_OK) lv_label_set_text(shell.message,"保存状态读取失败");
    else if (state.pending) lv_label_set_text(shell.message,"正在保存亮度");
    else if (state.last_error!=ESP_OK) lv_label_set_text(shell.message,"保存失败，请重试");
    else if (state.saved_brightness==shell.config.brightness()) lv_label_set_text(shell.message,"已保存，重启保留");
    else lv_label_set_text(shell.message,"松手保存，或点击保存");
}
static void back(void)
{
    if (!lv_obj_has_flag(shell.quick,LV_OBJ_FLAG_HIDDEN)) dismiss_quick(NULL);
    else if (!lv_obj_has_flag(shell.recents,LV_OBJ_FLAG_HIDDEN)) dismiss_recent(NULL);
    else shell.config.back();
}
static void gesture(lv_event_t *event)
{
    lv_point_t point; lv_indev_get_point(shell.config.touch,&point);
    if (lv_event_get_code(event)==LV_EVENT_PRESSED) {
        shell.start=point; shell.swipe_id[0]=0;
        if (lv_obj_is_visible(shell.recents) && !lv_obj_is_visible(shell.quick)) {
            for (unsigned i=0;i<shell.count;++i) {
                lv_area_t area; lv_obj_get_coords(shell.cards[i],&area);
                if (point.x>=area.x1 && point.x<=area.x2 && point.y>=area.y1 && point.y<=area.y2)
                    strcpy(shell.swipe_id,shell.items[i].id);
            }
        }
        return;
    }
    int dx=point.x-shell.start.x, dy=point.y-shell.start.y;
    bool quick=lv_obj_is_visible(shell.quick), recent=lv_obj_is_visible(shell.recents);
    enum {NONE, QUICK, RECENT, BACK, DISMISS_QUICK, CLOSE_CARD} action=NONE;
    if (shell.start.y<=36 && dy>=48 && abs(dy)>abs(dx)) action=QUICK;
    else if (quick && dy<=-48 && abs(dy)>abs(dx)) action=DISMISS_QUICK;
    else if (recent && shell.swipe_id[0] && dy<=-56 && abs(dy)>abs(dx)) action=CLOSE_CARD;
    else if (shell.start.y>=shell.height-28 && dy<=-48 && abs(dy)>abs(dx)) action=RECENT;
    else if (shell.start.y>36 && shell.start.y<shell.height-28 && abs(dx)>=48 && abs(dx)>abs(dy) &&
             ((shell.start.x<=24 && dx>0) || (shell.start.x>=shell.width-24 && dx<0)) &&
             ouse_navigation_back(shell.start.x,shell.start.y,point.x,point.y,shell.width,shell.height)) action=BACK;
    if (action!=NONE) {
        // Cancel the pending object click before navigation can delete its target.
        lv_indev_reset(shell.config.touch,NULL);
        if (action==QUICK) show_quick();
        else if (action==DISMISS_QUICK) dismiss_quick(NULL);
        else if (action==RECENT) show_recents();
        else if (action==BACK) back();
        else if (action==CLOSE_CARD) remove_card(shell.swipe_id);
    } else if (recent) {
        for (unsigned i=0;i<shell.count;++i) lv_obj_set_y(shell.cards[i],12);
    }
}

void ouse_shell_init(const ouse_shell_config_t *config)
{
    shell.config=*config;
    shell.width=lv_display_get_horizontal_resolution(lv_obj_get_display(config->screen));
    shell.height=lv_display_get_vertical_resolution(lv_obj_get_display(config->screen));
    lv_obj_t *status=box(config->screen,0,0,shell.width,36,0x101B2B);
    shell.clock=text(status,"--:--",16,8,90);
    lv_obj_set_style_text_font(shell.clock,&lv_font_montserrat_16,0);
    lv_obj_t *battery=text(status,LV_SYMBOL_BATTERY_EMPTY " --",shell.width-72,8,60);
    lv_obj_set_style_text_font(battery,&lv_font_montserrat_16,0);
    ouse_theme_apply(battery,OUSE_STYLE_MUTED,0);
    shell.recents=box(config->screen,0,40,shell.width,shell.height-64,0x101B2B);
    text(shell.recents,"最近应用",20,10,170);
    button(shell.recents,"桌面",shell.width-80,6,64,32,home_button,NULL);
    shell.strip=box(shell.recents,0,52,shell.width,shell.height-168,0x101B2B);
    lv_obj_add_flag(shell.strip,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(shell.strip,LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(shell.strip,LV_SCROLL_SNAP_CENTER);

    lv_obj_add_flag(shell.recents,LV_OBJ_FLAG_HIDDEN);
    shell.quick=box(config->screen,0,0,shell.width,shell.height-24,0x17283D);
    text(shell.quick,"快捷设置",22,28,180);
    text(shell.quick,"屏幕亮度",22,106,180);
    shell.percent=text(shell.quick,"",shell.width-82,106,64);
    shell.slider=lv_slider_create(shell.quick);
    lv_obj_set_pos(shell.slider,32,166); lv_obj_set_size(shell.slider,shell.width-64,12);
    lv_slider_set_range(shell.slider,10,100);
    ouse_theme_apply(shell.slider,OUSE_STYLE_TRACK,LV_PART_MAIN);
    lv_obj_set_style_bg_opa(shell.slider,LV_OPA_COVER,LV_PART_MAIN);
    ouse_theme_apply(shell.slider,OUSE_STYLE_ACCENT,LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(shell.slider,LV_OPA_COVER,LV_PART_INDICATOR);
    ouse_theme_apply(shell.slider,OUSE_STYLE_ACCENT,LV_PART_KNOB);
    lv_obj_set_style_bg_opa(shell.slider,LV_OPA_COVER,LV_PART_KNOB);
    lv_obj_set_style_radius(shell.slider,LV_RADIUS_CIRCLE,LV_PART_KNOB);
    lv_obj_set_style_pad_all(shell.slider,8,LV_PART_KNOB);
    lv_obj_add_event_cb(shell.slider,change_quick,LV_EVENT_VALUE_CHANGED,NULL);
    lv_obj_add_event_cb(shell.slider,save_quick,LV_EVENT_RELEASED,NULL);
    button(shell.quick,"保存",22,222,90,40,save_quick,NULL);
    shell.message=text(shell.quick,"",22,282,shell.width-44);
    shell.theme_button=button(shell.quick,"深色模式",shell.width-160,222,138,40,toggle_theme,NULL);
    button(shell.quick,"收起",22,shell.height-90,shell.width-44,44,dismiss_quick,NULL);
    lv_obj_add_flag(shell.quick,LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *nav=box(config->screen,0,shell.height-24,shell.width,24,0x101B2B);
    lv_obj_t *pill=box(nav,shell.width/2-42,12,84,4,0xAABCD1);
    lv_obj_set_style_radius(pill,2,0); lv_obj_remove_flag(pill,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(nav,recent_button,LV_EVENT_CLICKED,NULL);
    lv_indev_add_event_cb(config->touch,gesture,LV_EVENT_PRESSED,NULL);
    lv_indev_add_event_cb(config->touch,gesture,LV_EVENT_RELEASED,NULL);
    lv_timer_create(refresh_quick,300,NULL);
}
