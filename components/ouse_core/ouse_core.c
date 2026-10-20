#include "ouse_theme.h"
// SPDX-License-Identifier: MIT
#include "ouse_core.h"
#include "ouse_apps_internal.h"
#include "ouse_shell.h"
#include "ouse_widgets.h"
#include "ouse_devices.h"
#include "ouse_files.h"
#include "ouse_device_ui.h"
#include "ouse_dialog.h"
#include "ouse_screen.h"
#include "ouse_ime.h"
#include "ouse_notifications.h"
#include "ouse_health_ui.h"
#include "ouse_tasks.h"
#include "ouse_memory.h"
#include "ouse_memory_pressure.h"
#include "ouse_app_manager_ui.h"
#include "ouse_app_manager.h"
#include "ouse_health.h"
#include "ouse_permissions.h"
#include "ouse_storage.h"
#include "ouse_ble.h"
#include "ouse_setup.h"
#include "ouse_identity.h"
#include <stdatomic.h>
#include "src/misc/cache/instance/lv_image_cache.h"
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#if LVGL_VERSION_MAJOR != 9 || LVGL_VERSION_MINOR != 5
#error "Ouse Core is validated against LVGL 9.5"
#endif
#if !LV_FONT_MONTSERRAT_24
#error "Enable the built-in Montserrat 24 font for the default desktop"
#endif

LV_FONT_DECLARE(ouse_font_ui_16)

typedef struct {
    bool started;
    ouse_core_port_t port;
    uint8_t brightness;
    lv_obj_t *screen;
    lv_obj_t *home;
    lv_obj_t *app_host;
    lv_obj_t *grid,*display_page,*appearance_page,*language_page,*account_page;
    lv_obj_t *theme_value,*theme_switch,*display_value,*language_value;
    lv_obj_t *locale_marks[2],*device_pages[3];
    lv_obj_t *navigation_marks[2];
    lv_obj_t *sd_value,*sd_eject,*sd_recover;
    unsigned settings_page;
    size_t app_count;
    lv_timer_t *status_timer;
    ouse_app_info_t *icons;
    unsigned icon_capacity,dot_count;
    lv_obj_t *settings;
    lv_obj_t *health_page,*usb_page,*developer_page,*debug_switch;
    lv_obj_t *manager_page;
    lv_obj_t *bluetooth_page;
    bool manager_built;
    lv_obj_t **page_dots;
    unsigned launcher_page,launcher_pages;
    lv_obj_t *slider;
    lv_obj_t *brightness_text;
    lv_obj_t *account_names[2];
    lv_obj_t *info;
    unsigned pending_ui;
    bool brightness_failed;
} core_state_t;

static OUSE_PSRAM_BSS core_state_t core;
static void rebuild_launcher(void);
#include "ouse_launcher.inc"
#include "ouse_file_open.inc"
#include "ouse_keys.inc"
#include "ouse_boot.inc"
#include "ouse_launch.inc"
extern void ouse_navigation_grid(int,int,unsigned,int *,int *,int *,int *);

static lv_obj_t *text(lv_obj_t *parent, const char *value, int x, int y, int width)
{
    lv_obj_t *label = lv_label_create(parent);
    ouse_ui_text(label, value);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, width);
    lv_obj_set_pos(label, x, y);
    return label;
}

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}


static void app_changed(bool active)
{
    if(active)launcher_edit(false);
    if(active)launcher_cancel();
    if (active) {
        ouse_app_stats_t state={0};ouse_app_get_stats(&state);
        if(ouse_core_launch_active() && !strcmp(state.active_id,launch_animation.id)){
            lv_obj_remove_flag(core.home,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(core.app_host,LV_OBJ_FLAG_HIDDEN);
        }else {if(ouse_core_launch_active())launch_animation_end();lv_obj_add_flag(core.home,LV_OBJ_FLAG_HIDDEN);}
        ouse_shell_foreground();
    }else {
        if(ouse_core_launch_active())launch_animation_end();
        lv_obj_remove_flag(core.home,LV_OBJ_FLAG_HIDDEN);
    }
}

static void launch_app(lv_event_t *event)
{
    if(launcher.suppress_click)return;
    if(ouse_core_launch_active())return;
    if(launcher.editing){launcher_edit(false);return;}
    const char *id = lv_event_get_user_data(event);
    lv_obj_t *tile=lv_event_get_current_target_obj(event);
    (void)launch_animation_begin(id,lv_obj_get_child(tile,0));
    esp_err_t error = ouse_app_open(id);
    // Native open callbacks may construct a whole settings tree on the UI
    // task. No animation frame can render inside that callback. Start the
    // visible timeline when it returns, so loading cannot skip the expansion.
    if(error==ESP_OK && ouse_core_launch_active())launch_animation.started=lv_tick_get();
    if (error!=ESP_OK){launch_animation_end();LV_LOG_WARN("Application open failed");
        if(!ouse_dialog_active())ouse_dialog_show(ouse_ui_locale() ? "Unable to open app. Please retry." : "无法打开应用，请重试。",NULL,NULL);}
}

static void manager_settings_back(void);
static void bluetooth_refresh(void);
static void bluetooth_close(void);
static void bluetooth_pair_prompt(void);
static void select_settings_page(unsigned page)
{
    if(core.settings_page!=page)ouse_device_ui_close_modal();
    core.settings_page=page;
    if(page==12 && !core.manager_built){ouse_manager_ui_build(core.manager_page,manager_settings_back);core.manager_built=true;}
    if(page==12)ouse_manager_ui_refresh();
    lv_obj_t *pages[]={core.settings,core.display_page,core.appearance_page,core.language_page,core.info,core.device_pages[0],core.device_pages[1],core.device_pages[2],core.account_page,core.health_page,core.usb_page,core.developer_page,core.manager_page,core.bluetooth_page};
    for(unsigned i=0;i<sizeof(pages)/sizeof(pages[0]);++i)if(pages[i]) {
        if(i==page)lv_obj_remove_flag(pages[i],LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(pages[i],LV_OBJ_FLAG_HIDDEN);
    }
    if(page==13){ouse_ble_state_t state;ouse_ble_get(&state);if(state.enabled && state.ready)(void)ouse_ble_discoverable();}
}
static char settings_origin[48];
static void show_settings(lv_event_t *event) { (void)event;if(settings_origin[0]){char id[48];strcpy(id,settings_origin);settings_origin[0]=0;if(ouse_app_open(id)==ESP_OK)return;}select_settings_page(0); }
static void manager_settings_back(void){select_settings_page(0);}

static void theme_label(void)
{
    if(core.theme_value)ouse_ui_text(core.theme_value,ouse_theme_get()==OUSE_THEME_DARK ? "已开启" : "已关闭");
    if(core.theme_switch) {
        if(ouse_theme_get()==OUSE_THEME_DARK)lv_obj_add_state(core.theme_switch,LV_STATE_CHECKED);
        else lv_obj_remove_state(core.theme_switch,LV_STATE_CHECKED);
    }
}
static void rebuild_launcher(void);
static void preference_labels(void)
{
    theme_label();
    if(core.language_value)ouse_ui_text(core.language_value,ouse_ui_locale() ? "English" : "简体中文");
    for(unsigned i=0;i<2;++i)if(core.locale_marks[i])lv_label_set_text(core.locale_marks[i],ouse_ui_locale()==i ? LV_SYMBOL_OK : "");
    for(unsigned i=0;i<2;++i)if(core.navigation_marks[i])lv_label_set_text(core.navigation_marks[i],ouse_shell_navigation()==i ? LV_SYMBOL_OK : "");
}
static void retry_settings(void);
static void settings_error(const char *message) {ouse_dialog_show(message,"重试",retry_settings);}

esp_err_t ouse_core_set_ui(unsigned locale,unsigned percent)
{
    if (!core.started) return ESP_ERR_INVALID_STATE;
    esp_err_t error=ouse_ui_set_preferences(locale,percent);
    if (error!=ESP_OK) return error;
    preference_labels(); rebuild_launcher();
    error=core.port.save_ui ? core.port.save_ui(core.port.context,(uint8_t)locale,(uint8_t)percent) : ESP_OK;
    if(error!=ESP_OK)settings_error("设置未能保存，请重试");
    return error;
}
static void choose_language(lv_event_t *event)
{ (void)ouse_core_set_ui((unsigned)(uintptr_t)lv_event_get_user_data(event),ouse_ui_font_percent()); }

esp_err_t ouse_core_set_theme(ouse_theme_t theme)
{
    if (theme!=OUSE_THEME_LIGHT && theme!=OUSE_THEME_DARK) return ESP_ERR_INVALID_ARG;
    if (!core.started) return ESP_ERR_INVALID_STATE;
    if(ouse_core_launch_active())launch_animation_end();
    ouse_theme_set(theme); theme_label();
    esp_err_t error=core.port.save_theme ? core.port.save_theme(core.port.context,(uint8_t)theme,core.brightness) : ESP_OK;
    if(error!=ESP_OK)settings_error("设置未能保存，请重试");
    return error;
}
static void toggle_theme(lv_event_t *event)
{
    (void)event;
    (void)ouse_core_set_theme(ouse_theme_get()==OUSE_THEME_DARK ? OUSE_THEME_LIGHT : OUSE_THEME_DARK);
}
static void open_settings_page(lv_event_t *event)
{
    unsigned page=(unsigned)(uintptr_t)lv_event_get_user_data(event);
    select_settings_page(page);
}
static void shell_settings_page(unsigned page)
{
    if(ouse_app_open("ouse.settings")==ESP_OK)select_settings_page(page);
}
esp_err_t ouse_core_open_settings(unsigned target,const char *app_id)
{
    if(target<1 || target>3 || (target==1 && (!app_id || strncmp(app_id,"user.",5))))return ESP_ERR_INVALID_ARG;
    if(ouse_core_access_blocked())return ESP_ERR_INVALID_STATE;
    esp_err_t error=ouse_app_open("ouse.settings");if(error!=ESP_OK)return error;
    select_settings_page(target==1?12:target==2?13:7);
    if(target==1){settings_origin[0]=0;ouse_manager_ui_select(app_id);}
    else if(app_id)snprintf(settings_origin,sizeof(settings_origin),"%s",app_id);
    return ESP_OK;
}


static void refresh_status(lv_timer_t *timer)
{
    (void)timer;
    if(!core.settings)return;
    ouse_manager_ui_update();
    if(core.display_value){ouse_ui_unbind_text(core.display_value);lv_label_set_text_fmt(core.display_value,"%u%%",core.brightness);}
    ouse_device_ui_refresh();
    bluetooth_refresh();
    ouse_identity_state_t identity;ouse_identity_get(&identity);
    for(unsigned i=0;i<2;++i)if(core.account_names[i] && strcmp(lv_label_get_text(core.account_names[i]),identity.name[0]?identity.name:"Ouse"))lv_label_set_text(core.account_names[i],identity.name[0]?identity.name:"Ouse");
    ouse_health_ui_refresh();
    if(core.sd_value) {
        ouse_sd_state_t sd={0};ouse_sd_get(&sd);
        ouse_ui_unbind_text(core.sd_value);
        ouse_files_state_t card;ouse_files_get(&card);
        if(core.sd_eject){if(card.ready && !card.busy)lv_obj_remove_state(core.sd_eject,LV_STATE_DISABLED);else lv_obj_add_state(core.sd_eject,LV_STATE_DISABLED);}
        if(core.sd_recover){if(!card.busy && !card.recovering && !card.ejecting)lv_obj_remove_state(core.sd_recover,LV_STATE_DISABLED);else lv_obj_add_state(core.sd_recover,LV_STATE_DISABLED);}
        if(card.recovering)lv_label_set_text(core.sd_value,ouse_ui_locale() ? "Recovering" : "正在恢复");
        else if(card.ejected)lv_label_set_text(core.sd_value,ouse_ui_locale() ? "Ejected" : "已安全卸载");
        else if(card.ejecting)lv_label_set_text(core.sd_value,ouse_ui_locale() ? "Ejecting" : "正在卸载");
        else if(sd.probing)lv_label_set_text(core.sd_value,ouse_ui_locale() ? "Detecting" : "正在检测");
        else if(sd.detected){ouse_files_state_t files;ouse_files_get(&files);
            lv_label_set_text_fmt(core.sd_value,"%s · %llu MiB\n%s · %llu MiB",sd.name,(unsigned long long)(sd.capacity_bytes/(1024*1024)),
                files.ready ? (ouse_ui_locale() ? "Free" : "可用") : (ouse_ui_locale() ? "Unavailable" : "文件系统不可用"),(unsigned long long)(files.free_bytes/(1024*1024)));
        }
        else lv_label_set_text_fmt(core.sd_value,"%s: %s",ouse_ui_locale() ? "Not identified" : "未识别",esp_err_to_name(sd.error));
    }
}
static void retry_settings(void)
{
    esp_err_t e=core.port.save_brightness ? core.port.save_brightness(core.port.context,core.brightness) : ESP_OK;
    if(e==ESP_OK && core.port.save_ui)e=core.port.save_ui(core.port.context,ouse_ui_locale(),ouse_ui_font_percent());
    ouse_devices_retry_save();
    if(e!=ESP_OK)settings_error("设置未能保存，请重试");
}
static void save_brightness(lv_event_t *event)
{
    (void)event;
    if (!core.port.save_brightness || core.brightness_failed) return;
    esp_err_t error = core.port.save_brightness(core.port.context, core.brightness);
    if(error!=ESP_OK)settings_error("设置未能保存，请重试");
}


static esp_err_t apply_brightness(uint8_t requested)
{
    esp_err_t error=ouse_screen_brightness(requested);
    if (error==ESP_OK) {
        core.brightness_failed=false;
        if(core.brightness!=requested)ouse_ui_display_changed();
        core.brightness=requested;
    } else {
        core.brightness_failed=true;
        ouse_dialog_show("亮度设置失败，请重试",NULL,NULL);
    }
    if (core.slider) lv_slider_set_value(core.slider,core.brightness,LV_ANIM_OFF);
    if(core.display_value)lv_label_set_text_fmt(core.display_value,"%u%%",core.brightness);
    if (core.brightness_text) lv_label_set_text_fmt(core.brightness_text,"%u%%",(unsigned)core.brightness);
    return error;
}
uint8_t ouse_core_brightness(void) { return core.brightness; }
esp_err_t ouse_core_preview_brightness(uint8_t value)
{
    if (!core.started) return ESP_ERR_INVALID_STATE;
    if (value<10 || value>100) return ESP_ERR_INVALID_ARG;
    return apply_brightness(value);
}
esp_err_t ouse_core_set_brightness(uint8_t value)
{
    if (!core.started) return ESP_ERR_INVALID_STATE;
    if (value<10 || value>100) return ESP_ERR_INVALID_ARG;
    esp_err_t error=apply_brightness(value);
    if (error==ESP_OK && core.port.save_brightness) error=core.port.save_brightness(core.port.context,value);
    return error;
}
static void brightness_changed(lv_event_t *event)
{
    (void)apply_brightness((uint8_t)lv_slider_get_value(lv_event_get_target_obj(event)));
}
static uint8_t shell_brightness(void) { return core.brightness; }
static esp_err_t shell_save(void)
{
    return core.port.save_brightness ? core.port.save_brightness(core.port.context,core.brightness) : ESP_ERR_NOT_SUPPORTED;
}
static esp_err_t shell_settings_state(ouse_settings_state_t *state)
{
    return core.port.get_settings_state ? core.port.get_settings_state(core.port.context,state) : ESP_ERR_NOT_SUPPORTED;
}
static void shell_back(void)
{
    if(ouse_core_launch_active()){ouse_core_launch_cancel();return;}
    if(launcher.editing){launcher_cancel();launcher_edit(false);return;}
    if (ouse_ime_active()) { ouse_ime_hide(); return; }
    if (ouse_app_back()) return;
    ouse_app_stats_t state;
    if (ouse_app_get_stats(&state)!=ESP_OK) return;
    if(!strcmp(state.active_id,"ouse.settings") && core.settings_page==12 && ouse_manager_ui_back())return;
    if(!strcmp(state.active_id,"ouse.settings") && core.settings_page)show_settings(NULL);
    else (void)ouse_app_background();
}


static lv_obj_t *settings_column(lv_obj_t *root)
{
    lv_obj_t *page=ouse_widget_box(root,OUSE_STYLE_APP,0);
    lv_obj_set_size(page,LV_PCT(100),LV_PCT(100));
    // Blank space must hit the page so a drag can start between child rows.
    lv_obj_add_flag(page,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);lv_obj_set_scroll_dir(page,LV_DIR_VER);
    lv_obj_set_style_pad_all(page,ouse_ui_dp(12),0);
    lv_obj_set_style_pad_row(page,ouse_ui_dp(8),0);lv_obj_set_flex_flow(page,LV_FLEX_FLOW_COLUMN);
    return page;
}
static void page_heading(lv_obj_t *page,const char *title,bool back)
{
    if(back) {
        lv_obj_t *bar=ouse_widget_box(page,OUSE_STYLE_APP,0);
        lv_obj_set_size(bar,LV_PCT(100),LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(bar,ouse_ui_dp(48),0);
        lv_obj_set_style_pad_column(bar,ouse_ui_dp(8),0);
        lv_obj_set_flex_flow(bar,LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(bar,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
        lv_obj_t *button=lv_button_create(bar);lv_obj_remove_style_all(button);
        lv_obj_set_size(button,ouse_ui_dp(40),ouse_ui_dp(40));
        lv_obj_set_style_radius(button,LV_RADIUS_CIRCLE,0);
        ouse_theme_apply(button,OUSE_STYLE_PRESSED,LV_STATE_PRESSED);
        lv_obj_add_event_cb(button,show_settings,LV_EVENT_CLICKED,NULL);
        lv_obj_t *arrow=lv_label_create(button);lv_label_set_text(arrow,LV_SYMBOL_LEFT);
        lv_obj_remove_flag(arrow,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_font(arrow,&lv_font_montserrat_24,0);
        ouse_theme_apply(arrow,OUSE_STYLE_ACCENT_TEXT,0);lv_obj_center(arrow);
        lv_obj_t *label=ouse_widget_text(bar,title,22);
        lv_obj_set_width(label,0);lv_obj_set_flex_grow(label,1);
        return;
    }
    lv_obj_t *label=ouse_widget_text(page,title,28);lv_obj_set_width(label,LV_PCT(100));
}
static void section(lv_obj_t *page,const char *title)
{
    lv_obj_t *label=ouse_widget_text(page,title,13);lv_obj_set_width(label,LV_PCT(100));
    lv_obj_set_style_margin_top(label,ouse_ui_dp(8),0);ouse_theme_apply(label,OUSE_STYLE_MUTED,0);
}
static lv_obj_t *choice(lv_obj_t *page,const char *title,lv_event_cb_t callback,unsigned value)
{
    lv_obj_t *row=ouse_widget_row(page,NULL,title,NULL,callback,(void *)(uintptr_t)value,NULL);
    lv_obj_t *mark=lv_obj_get_child(row,-1);lv_label_set_text(mark,"");return mark;
}
static lv_obj_t *settings_card(lv_obj_t *parent)
{
    lv_obj_t *card=ouse_widget_box(parent,OUSE_STYLE_PANEL,20);
    lv_obj_set_width(card,LV_PCT(100));lv_obj_set_height(card,LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card,LV_FLEX_FLOW_COLUMN);
    return card;
}
static void local_avatar(lv_obj_t *parent)
{
    lv_obj_t *avatar=ouse_widget_box(parent,OUSE_STYLE_PANEL,0);
    lv_obj_set_size(avatar,ouse_ui_dp(48),ouse_ui_dp(48));
    lv_obj_set_style_radius(avatar,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(avatar,lv_color_hex(0x367bef),0);
    lv_obj_t *head=ouse_widget_box(avatar,OUSE_STYLE_PANEL,0);
    lv_obj_set_size(head,ouse_ui_dp(14),ouse_ui_dp(14));lv_obj_set_style_radius(head,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(head,lv_color_white(),0);lv_obj_align(head,LV_ALIGN_TOP_MID,0,ouse_ui_dp(8));
    lv_obj_t *body=ouse_widget_box(avatar,OUSE_STYLE_PANEL,0);
    lv_obj_set_size(body,ouse_ui_dp(28),ouse_ui_dp(16));lv_obj_set_style_radius(body,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(body,lv_color_white(),0);lv_obj_align(body,LV_ALIGN_BOTTOM_MID,0,-ouse_ui_dp(7));
}
static void account_header(lv_obj_t *parent,bool navigation)
{
    lv_obj_t *card=settings_card(parent);
    lv_obj_set_flex_flow(card,LV_FLEX_FLOW_ROW);lv_obj_set_flex_align(card,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(card,ouse_ui_dp(14),0);lv_obj_set_style_pad_column(card,ouse_ui_dp(12),0);
    if(navigation){
        lv_obj_add_flag(card,LV_OBJ_FLAG_CLICKABLE);
        ouse_theme_apply(card,OUSE_STYLE_PRESSED,LV_STATE_PRESSED);
        lv_obj_add_event_cb(card,open_settings_page,LV_EVENT_CLICKED,(void *)(uintptr_t)8);
    }
    local_avatar(card);
    lv_obj_t *copy=ouse_widget_box(card,OUSE_STYLE_PANEL,0);
    lv_obj_set_width(copy,0);lv_obj_set_flex_grow(copy,1);lv_obj_set_height(copy,LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(copy,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(copy,ouse_ui_dp(3),0);
    ouse_identity_state_t identity;ouse_identity_get(&identity);
    lv_obj_t *name=ouse_widget_text(copy,"",20);lv_label_set_text(name,identity.name[0]?identity.name:"Ouse");lv_obj_set_width(name,LV_PCT(100));
    core.account_names[navigation?0:1]=name;
    lv_obj_t *kind=ouse_widget_text(copy,"本地账号",13);lv_obj_set_width(kind,LV_PCT(100));ouse_theme_apply(kind,OUSE_STYLE_MUTED,0);
    if(navigation){
        lv_obj_t *arrow=lv_label_create(card);lv_label_set_text(arrow,LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_font(arrow,&lv_font_montserrat_24,0);ouse_theme_apply(arrow,OUSE_STYLE_MUTED,0);
    }
}
static lv_obj_t *specification(lv_obj_t *group,const char *title,const char *value)
{
    lv_obj_t *row=ouse_widget_box(group,OUSE_STYLE_PANEL,0);
    lv_obj_set_width(row,LV_PCT(100));lv_obj_set_height(row,LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row,ouse_ui_dp(12),0);lv_obj_set_style_pad_row(row,ouse_ui_dp(3),0);
    lv_obj_set_flex_flow(row,LV_FLEX_FLOW_COLUMN);
    lv_obj_t *label=ouse_widget_text(row,title,13);lv_obj_set_width(label,LV_PCT(100));ouse_theme_apply(label,OUSE_STYLE_MUTED,0);
    label=ouse_widget_text(row,value && *value ? value : "未知",16);lv_obj_set_width(label,LV_PCT(100));
    return label;
}
static void eject_confirmed(void)
{
    if(ouse_files_eject()!=ESP_OK)ouse_dialog_show("请先停止播放、录音和文件操作，再重试。",NULL,NULL);
}
static void eject_event(lv_event_t *event)
{(void)event;ouse_dialog_show("安全卸载 SD 卡？","卸载",eject_confirmed);}
static void recover_sd_event(lv_event_t *event)
{(void)event;if(ouse_files_recover()!=ESP_OK)ouse_dialog_show("请先停止播放、录音和文件操作，再重试。",NULL,NULL);}
static void hardware_page(lv_obj_t *parent)
{
    page_heading(parent,"关于设备",true);
    lv_obj_t *hero=settings_card(parent);
    lv_obj_set_style_pad_all(hero,ouse_ui_dp(16),0);lv_obj_set_style_pad_row(hero,ouse_ui_dp(10),0);
    lv_obj_set_flex_align(hero,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    ouse_widget_icon(hero,LV_SYMBOL_SETTINGS,48,0x367bef);
    lv_obj_t *name=ouse_widget_text(hero,core.port.board_name,20);
    lv_obj_set_width(name,LV_PCT(100));lv_obj_set_style_text_align(name,LV_TEXT_ALIGN_CENTER,0);
    const ouse_hardware_info_t *h=core.port.hardware;
    section(parent,"配置");lv_obj_t *group=settings_card(parent);
    specification(group,"处理器",h ? h->processor : NULL);
    specification(group,"运行内存",h ? h->memory : NULL);
    specification(group,"机身存储",h ? h->storage : NULL);
    core.sd_value=specification(group,"SD 卡","正在检测");
    core.sd_eject=ouse_widget_button(group,"安全卸载 SD 卡",eject_event,NULL);ouse_widget_button_expand(core.sd_eject);
    core.sd_recover=ouse_widget_button(group,"重新检测 SD 卡",recover_sd_event,NULL);ouse_widget_button_expand(core.sd_recover);
    section(parent,"硬件");group=settings_card(parent);
    specification(group,"屏幕",h ? h->display : NULL);
    specification(group,"触摸",h ? h->touch : NULL);
    specification(group,"麦克风",h ? h->microphone : NULL);
    specification(group,"扬声器",h ? h->speaker : NULL);
    specification(group,"无线连接",h ? h->wireless : NULL);
    specification(group,"摄像头",h ? h->camera : NULL);
    specification(group,"电源管理",h ? h->power : NULL);
    section(parent,"软件");group=settings_card(parent);
    specification(group,"系统版本","Ouse Core " OUSE_CORE_VERSION);
}
static void debug_changed(lv_event_t *event)
{
    bool enabled=lv_obj_has_state(lv_event_get_target_obj(event),LV_STATE_CHECKED);
    if(ouse_system_request_debug(enabled)!=ESP_OK){
        if(ouse_system_debug_enabled())lv_obj_add_state(core.debug_switch,LV_STATE_CHECKED);
        else lv_obj_remove_state(core.debug_switch,LV_STATE_CHECKED);
        ouse_dialog_show("操作失败，请重试",NULL,NULL);
    }
}
static void choose_navigation(lv_event_t *event)
{
    unsigned mode=(unsigned)(uintptr_t)lv_event_get_user_data(event);
    if(ouse_system_request_navigation((uint8_t)mode)!=ESP_OK){settings_error("设置未能保存，请重试");return;}
    ouse_shell_set_navigation(mode);
    preference_labels();
}
#include "ouse_bluetooth_ui.inc"
static void security_settings(lv_event_t *e){(void)e;ouse_setup_settings();}
static void rename_settings(lv_event_t *e){(void)e;ouse_setup_rename();}
static esp_err_t settings_open(lv_obj_t *root,void *context)
{
    (void)context;
    core.settings=settings_column(root);page_heading(core.settings,"设置",false);
    account_header(core.settings,true);
    ouse_widget_row(core.settings,LV_SYMBOL_EDIT,"设备名称",NULL,rename_settings,NULL,NULL);
    ouse_widget_row(core.settings,LV_SYMBOL_KEYBOARD,"密钥","锁屏密码",security_settings,NULL,NULL);
    section(core.settings,"显示与个性化");
    ouse_widget_row(core.settings,LV_SYMBOL_IMAGE,"显示","",open_settings_page,(void *)(uintptr_t)1,&core.display_value);
    ouse_widget_row(core.settings,LV_SYMBOL_TINT,"外观","明暗主题",open_settings_page,(void *)(uintptr_t)2,NULL);
    ouse_widget_row(core.settings,LV_SYMBOL_KEYBOARD,"语言","",open_settings_page,(void *)(uintptr_t)3,&core.language_value);
    section(core.settings,"设备");
    ouse_widget_row(core.settings,LV_SYMBOL_BATTERY_FULL,"电源",NULL,open_settings_page,(void *)(uintptr_t)5,NULL);
    ouse_widget_row(core.settings,LV_SYMBOL_VOLUME_MAX,"声音",NULL,open_settings_page,(void *)(uintptr_t)6,NULL);
    ouse_widget_row(core.settings,LV_SYMBOL_WIFI,"Wi-Fi",NULL,open_settings_page,(void *)(uintptr_t)7,NULL);
    ouse_widget_row(core.settings,LV_SYMBOL_BLUETOOTH,"蓝牙",NULL,open_settings_page,(void *)(uintptr_t)13,NULL);
    ouse_widget_row(core.settings,LV_SYMBOL_USB,"USB OTG",NULL,open_settings_page,(void *)(uintptr_t)10,NULL);
    section(core.settings,"系统");
    ouse_widget_row(core.settings,LV_SYMBOL_DIRECTORY,"应用管理",NULL,open_settings_page,(void *)(uintptr_t)12,NULL);
    ouse_widget_row(core.settings,LV_SYMBOL_FILE,"默认应用",NULL,file_settings,NULL,NULL);
    ouse_widget_row(core.settings,LV_SYMBOL_LOOP,"后台活动与服务",NULL,open_settings_page,(void *)(uintptr_t)9,NULL);
    ouse_widget_row(core.settings,LV_SYMBOL_LIST,"系统信息","设备与硬件",open_settings_page,(void *)(uintptr_t)4,NULL);
    ouse_widget_row(core.settings,LV_SYMBOL_SETTINGS,"开发者选项",NULL,open_settings_page,(void *)(uintptr_t)11,NULL);
    core.display_page=settings_column(root);page_heading(core.display_page,"显示",true);
    lv_obj_t *card=ouse_widget_box(core.display_page,OUSE_STYLE_PANEL,24);
    lv_obj_set_width(card,LV_PCT(100));lv_obj_set_height(card,LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card,ouse_ui_dp(16),0);lv_obj_set_flex_flow(card,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card,ouse_ui_dp(10),0);
    ouse_widget_text(card,"屏幕亮度",16);
    core.brightness_text=ouse_widget_text(card,"",24);lv_label_set_text_fmt(core.brightness_text,"%u%%",core.brightness);
    core.slider=lv_slider_create(card);lv_obj_set_width(core.slider,LV_PCT(100));ouse_widget_slider(core.slider,24);
    lv_slider_set_range(core.slider,10,100);lv_slider_set_value(core.slider,core.brightness,LV_ANIM_OFF);
    lv_obj_add_event_cb(core.slider,brightness_changed,LV_EVENT_VALUE_CHANGED,NULL);
    lv_obj_add_event_cb(core.slider,save_brightness,LV_EVENT_RELEASED,NULL);
    core.appearance_page=settings_column(root);page_heading(core.appearance_page,"外观",true);
    lv_obj_t *row=ouse_widget_row(core.appearance_page,LV_SYMBOL_TINT,"深色模式",NULL,toggle_theme,NULL,NULL);
    lv_obj_delete(lv_obj_get_child(row,-1));
    core.theme_switch=ouse_widget_switch(row);
    lv_obj_add_event_cb(core.theme_switch,toggle_theme,LV_EVENT_VALUE_CHANGED,NULL);
    section(core.appearance_page,"系统导航");
    core.navigation_marks[0]=choice(core.appearance_page,"全面屏手势",choose_navigation,0);
    core.navigation_marks[1]=choice(core.appearance_page,"三键导航",choose_navigation,1);
    core.language_page=settings_column(root);page_heading(core.language_page,"语言",true);
    section(core.language_page,"语言");
    core.locale_marks[0]=choice(core.language_page,"简体中文",choose_language,0);
    core.locale_marks[1]=choice(core.language_page,"English",choose_language,1);
    core.info=settings_column(root);hardware_page(core.info);
    core.account_page=settings_column(root);page_heading(core.account_page,"账号",true);account_header(core.account_page,false);
    const char *titles[]={"电源","声音","Wi-Fi"};
    for(unsigned i=0;i<3;++i){core.device_pages[i]=settings_column(root);page_heading(core.device_pages[i],titles[i],true);ouse_device_ui_build(core.device_pages[i],i+5);}
    core.health_page=settings_column(root);page_heading(core.health_page,"后台活动与服务",true);ouse_health_ui_build(core.health_page);
    core.usb_page=settings_column(root);page_heading(core.usb_page,"USB OTG",true);ouse_device_ui_build(core.usb_page,10);
    core.developer_page=settings_column(root);page_heading(core.developer_page,"开发者选项",true);
    lv_obj_t *debug_row=ouse_widget_row(core.developer_page,LV_SYMBOL_USB,"MCP 设备调试",NULL,NULL,NULL,NULL);
    lv_obj_delete(lv_obj_get_child(debug_row,-1));
    core.debug_switch=ouse_widget_switch(debug_row);
    if(ouse_system_debug_enabled())lv_obj_add_state(core.debug_switch,LV_STATE_CHECKED);
    lv_obj_add_event_cb(core.debug_switch,debug_changed,LV_EVENT_VALUE_CHANGED,NULL);
#if !CONFIG_OUSE_DEVICE_DEBUG
    lv_obj_add_state(core.debug_switch,LV_STATE_DISABLED);
#endif
    core.manager_page=settings_column(root);core.manager_built=false;
    core.bluetooth_page=settings_column(root);page_heading(core.bluetooth_page,"蓝牙",true);bluetooth_build(core.bluetooth_page);
    preference_labels();select_settings_page(0);
    core.status_timer=lv_timer_create(refresh_status,300,NULL);refresh_status(NULL);return ESP_OK;
}

static void settings_close(void *context)
{
    (void)context;
    if (core.status_timer) lv_timer_delete(core.status_timer);
    core.status_timer=NULL;core.theme_value=NULL;core.theme_switch=NULL;core.display_value=NULL;core.language_value=NULL;
    core.display_page=core.appearance_page=core.language_page=core.account_page=NULL;
    core.account_names[0]=core.account_names[1]=NULL;
    core.health_page=core.usb_page=NULL;ouse_health_ui_close();
    core.developer_page=core.debug_switch=NULL;
    bluetooth_close();core.bluetooth_page=NULL;
    ouse_manager_ui_close();core.manager_page=NULL;core.manager_built=false;
    memset(core.navigation_marks,0,sizeof(core.navigation_marks));
    core.sd_value=core.sd_eject=core.sd_recover=NULL;
    ouse_device_ui_close();memset(core.device_pages,0,sizeof(core.device_pages));memset(core.locale_marks,0,sizeof(core.locale_marks));
    core.settings = NULL; core.info = NULL; core.slider = NULL;
    core.brightness_text = NULL;
    core.brightness_failed = false;
}

static void settings_pause(void *context) {
    (void)context; if (core.status_timer) lv_timer_pause(core.status_timer);
    ouse_health_ui_pause();
    ouse_device_ui_close_modal();
    ouse_audio_state_t a;ouse_task_snapshot_t task;
    if(ouse_audio_get(&a)==ESP_OK && a.mode!=OUSE_AUDIO_IDLE && a.mode!=OUSE_AUDIO_ERROR &&
       ouse_task_get(NULL,a.request_id,&task) && !strcmp(task.owner.app_id,"ouse.system") && task.kind==OUSE_TASK_AUDIO)(void)ouse_audio_stop();
}
static void settings_resume(void *context) { (void)context; if (core.status_timer) lv_timer_resume(core.status_timer);if(core.settings_page==12)ouse_manager_ui_refresh();refresh_status(NULL); }

static void launcher_scrolled(lv_event_t *event)
{
    (void)event;
    int width=lv_obj_get_width(core.grid);if(width<=0 || !core.launcher_pages)return;
    unsigned page=(unsigned)((lv_obj_get_scroll_x(core.grid)+width/2)/width);
    if(page>=core.launcher_pages)page=core.launcher_pages-1;
    core.launcher_page=page;
    for(unsigned index=0;index<core.launcher_pages;++index)
        if(core.page_dots[index])lv_obj_set_style_bg_opa(core.page_dots[index],index==page ? LV_OPA_COVER : LV_OPA_30,0);
}
static void navigation_layout(int navigation_height)
{
    ouse_ime_set_bottom_inset(navigation_height);
    int height=lv_obj_get_height(core.screen),width=lv_obj_get_width(core.screen);
    lv_obj_set_width(core.home,width);lv_obj_set_width(core.app_host,width);lv_obj_set_width(core.grid,width-ouse_ui_dp(16));
    lv_obj_set_height(core.home,height-ouse_ui_dp(40)-navigation_height);
    lv_obj_set_height(core.grid,height-ouse_ui_dp(70)-navigation_height);
    ouse_apps_resize(height-ouse_ui_dp(40)-navigation_height);
    rebuild_launcher();
}
bool ouse_core_landscape(void){return core.started && lv_display_get_rotation(core.port.display)==LV_DISPLAY_ROTATION_270;}
esp_err_t ouse_core_set_orientation(bool landscape)
{
    if(!core.started || ouse_core_access_blocked())return ESP_ERR_INVALID_STATE;
    if(ouse_core_landscape()==landscape)return ESP_OK;
    if(ouse_core_launch_active())launch_animation_end();
    ouse_ime_hide();ouse_device_ui_close_modal();ouse_shell_foreground();
    for(lv_indev_t *input=lv_indev_get_next(NULL);input;input=lv_indev_get_next(input))if(lv_indev_get_display(input)==core.port.display)lv_indev_reset(input,NULL);
    esp_err_t error=core.port.set_orientation?core.port.set_orientation(core.port.context,landscape):ESP_OK;
    if(error!=ESP_OK)return error;
    lv_display_set_rotation(core.port.display,landscape?LV_DISPLAY_ROTATION_270:LV_DISPLAY_ROTATION_0);
    lv_obj_update_layout(core.screen);ouse_ui_display_changed();
    ouse_shell_resize();ouse_dialog_resize();lv_obj_invalidate(core.screen);
    return ESP_OK;
}
void ouse_core_launcher_state(unsigned *page,unsigned *pages)
{
    if(page)*page=core.launcher_page;
    if(pages)*pages=core.launcher_pages;
}
static void rebuild_launcher(void)
{
    if (!core.grid) return;
    if(ouse_core_launch_active())launch_animation_end();
    launcher_cancel();
    unsigned wanted_pages=(core.app_count+11)/12;
    if(core.app_count>core.icon_capacity){
        ouse_app_info_t *icons=ouse_psram_calloc(core.app_count,sizeof(*icons));
        if(!icons)return;
        lv_obj_clean(core.grid);free(core.icons);core.icons=icons;core.icon_capacity=core.app_count;
    }
    lv_obj_t **dots=ouse_psram_calloc(wanted_pages ? wanted_pages : 1,sizeof(*dots));
    if(!dots)return;
    for(unsigned index=0;index<core.dot_count;++index)if(core.page_dots[index])lv_obj_delete(core.page_dots[index]);
    free(core.page_dots);core.page_dots=dots;core.dot_count=wanted_pages;
    unsigned saved_page=core.launcher_page;
    lv_obj_clean(core.grid); lv_obj_update_layout(core.grid);
    int width=lv_obj_get_width(core.grid), height=lv_obj_get_height(core.grid);
    int cols,rows;launcher_shape(width,height,&cols,&rows);
    int cell_w=width/cols, name_h=2*ouse_ui_font(14)->line_height;
    int cell_h=height/rows;
    unsigned capacity=(unsigned)(rows*cols); lv_obj_t *page=NULL;
    for(unsigned index=0;index<core.app_count;++index)(void)ouse_app_get_info(index,&core.icons[index]);
    launcher_sort();
    if(!launcher_tiles(core.app_count))return;
    for(unsigned i=0;i<core.app_count;++i) {
        if(i%capacity==0) { page=panel(core.grid,(int)(i/capacity)*width,0,width,height); lv_obj_add_flag(page,LV_OBJ_FLAG_SNAPPABLE);lv_obj_add_event_cb(page,launcher_blank,LV_EVENT_CLICKED,NULL); }
        unsigned slot=i%capacity;
        lv_obj_t *tile=lv_button_create(page); lv_obj_remove_style_all(tile);
        launcher.tiles[i]=tile;
        lv_obj_add_flag(tile,LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(tile,launcher_touch,LV_EVENT_ALL,NULL);
        lv_obj_set_pos(tile,(int)(slot%cols)*cell_w,(int)(slot/cols)*cell_h); lv_obj_set_size(tile,cell_w,cell_h);
        lv_obj_set_style_opa(tile,LV_OPA_70,LV_STATE_PRESSED);
        lv_obj_add_event_cb(tile,launch_app,LV_EVENT_CLICKED,core.icons[i].id);
        int icon_size=40;
        lv_obj_t *icon=ouse_widget_app_icon(tile,&core.icons[i],icon_size);
        lv_obj_set_pos(icon,(cell_w-ouse_ui_dp(icon_size))/2,0);
        lv_obj_t *name=text(tile,"",ouse_ui_dp(2),ouse_ui_dp(icon_size+4),cell_w-ouse_ui_dp(4));
        ouse_widget_app_name(name,&core.icons[i]);
        ouse_ui_use_font(name,14); lv_obj_set_height(name,name_h);
        lv_label_set_long_mode(name,LV_LABEL_LONG_MODE_DOTS); lv_obj_set_style_text_align(name,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_remove_flag(name,LV_OBJ_FLAG_CLICKABLE);
        launcher_badge(i,cell_w);
    }
    core.launcher_pages=(core.app_count+capacity-1)/capacity;
    core.launcher_page=saved_page;
    if(core.launcher_page>=core.launcher_pages)core.launcher_page=core.launcher_pages ? core.launcher_pages-1 : 0;
    if(core.launcher_pages>1)for(unsigned index=0;index<core.launcher_pages;++index){
        lv_obj_t *dot=panel(core.home,lv_obj_get_width(core.home)/2+(int)(index*12)-(int)(core.launcher_pages*12)/2,lv_obj_get_height(core.home)-ouse_ui_dp(12),ouse_ui_dp(5),ouse_ui_dp(5));
        core.page_dots[index]=dot;ouse_theme_apply(dot,OUSE_STYLE_PILL,0);lv_obj_set_style_radius(dot,LV_RADIUS_CIRCLE,0);
    }
    lv_obj_scroll_to_x(core.grid,(int)core.launcher_page*width,LV_ANIM_OFF);
    launcher_scrolled(NULL);
}

static ouse_permission_prompt_t permission_prompt;
static uint32_t permission_shown;
static void permission_choice(unsigned choice)
{
    ouse_app_stats_t stats={0};ouse_app_get_stats(&stats);
    if(ouse_permissions_prompt_current(permission_prompt.instance) && !strcmp(stats.active_id,permission_prompt.app_id)){
        if(ouse_permissions_set(permission_prompt.app_id,permission_prompt.permission,choice==0 ? OUSE_PERMISSION_ALLOW : OUSE_PERMISSION_DENY)!=ESP_OK)
            ouse_dialog_show("设置未能保存，请重试",NULL,NULL);
    }
    permission_prompt.instance=0;
}
static void permission_dialog(void)
{
    ouse_app_stats_t stats={0};ouse_app_get_stats(&stats);
    if(permission_prompt.instance){
        if(!ouse_permissions_prompt_current(permission_prompt.instance) || strcmp(stats.active_id,permission_prompt.app_id) || lv_tick_elaps(permission_shown)>15000){ouse_dialog_dismiss();permission_prompt.instance=0;}
        else if(!ouse_dialog_active())permission_prompt.instance=0;
        return;
    }
    if(ouse_dialog_active() || !ouse_permissions_next(&permission_prompt))return;
    if(strcmp(stats.active_id,permission_prompt.app_id)){permission_prompt.instance=0;return;}
    ouse_app_info_t info;char title[160];const char *names[]={"麦克风","摄像头","删除共享文件","蓝牙","键盘与鼠标控制"};
    const char *name=ouse_app_find_info(stats.active_id,&info)==ESP_OK ? info.name : stats.active_id;
    snprintf(title,sizeof(title),"%s\n%s",name,names[permission_prompt.permission]);
    static const char *choices[]={"允许","拒绝"};permission_shown=lv_tick_get();
    ouse_dialog_select(title,choices,2,1,permission_choice);
}
static void monitor_errors(lv_timer_t *timer)
{
    (void)timer;
    if(ouse_core_access_blocked())return;
    permission_dialog();
    bluetooth_pair_prompt();
    static bool settings_failed;
    static uint32_t saved_error,audio_error,wifi_error;
    ouse_settings_state_t settings;
    if(core.port.get_settings_state && core.port.get_settings_state(core.port.context,&settings)==ESP_OK) {
        if(settings.pending || settings.last_error==ESP_OK)settings_failed=false;
        else if(!settings_failed && !ouse_dialog_active()){settings_failed=true;settings_error("设置未能保存，请重试");}
    }
    ouse_device_save_state_t save;ouse_devices_save_state(&save);
    if(save.error!=ESP_OK && save.revision!=saved_error && !ouse_dialog_active()){saved_error=save.revision;settings_error("设置未能保存，请重试");}
    ouse_audio_state_t audio;
    if(ouse_audio_get(&audio)==ESP_OK && audio.error!=ESP_OK && audio.completed_id!=audio_error && !ouse_dialog_active()) {
        audio_error=audio.completed_id;ouse_dialog_show("无法录放音，请重试",NULL,NULL);
    }
    ouse_wifi_state_t wifi;
    if(ouse_wifi_get(&wifi)==ESP_OK && wifi.error!=ESP_OK && wifi.completed_id!=wifi_error && !ouse_dialog_active()) {
        wifi_error=wifi.completed_id;ouse_dialog_show("Wi-Fi 操作失败，请检查网络或重试",NULL,NULL);
    }
}
esp_err_t ouse_core_sleep_display(void) {return core.started ? ouse_screen_sleep() : ESP_ERR_INVALID_STATE;}
esp_err_t ouse_core_wake_display(void) {return core.started ? ouse_screen_wake() : ESP_ERR_INVALID_STATE;}
esp_err_t ouse_core_start(const ouse_core_config_t *config)
{
    if (core.started) return ESP_ERR_INVALID_STATE;
    if (!config || !config->port.display || !config->port.touch ||
        !config->port.lock || !config->port.unlock || !config->port.set_brightness ||
        !config->port.board_name || config->initial_brightness < 10 ||
        config->initial_brightness > 100) return ESP_ERR_INVALID_ARG;
    if (config->initial_theme!=OUSE_THEME_LIGHT && config->initial_theme!=OUSE_THEME_DARK) return ESP_ERR_INVALID_ARG;
    if (config->app_count && !config->apps) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < config->app_count; ++i) {
        esp_err_t error = ouse_apps_validate(&config->apps[i]);
        if (error != ESP_OK) return error;
        if (!strcmp(config->apps[i].id, "ouse.settings")) return ESP_ERR_INVALID_ARG;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(config->apps[i].id, config->apps[j].id)) return ESP_ERR_INVALID_ARG;
    }
    const ouse_core_port_t *port = &config->port;
    if (!port->lock(port->context, 2000)) return ESP_ERR_TIMEOUT;
    const int width = lv_display_get_horizontal_resolution(port->display);
    const int height = lv_display_get_vertical_resolution(port->display);
    if (width < 320 || height < 480) {
        port->unlock(port->context);
        return ESP_ERR_NOT_SUPPORTED;
    }
    const esp_err_t brightness_error = port->set_brightness(port->context, config->initial_brightness);
    if (brightness_error != ESP_OK) {
        port->unlock(port->context);
        return brightness_error;
    }
    esp_err_t ui_error=ouse_ui_init(config->density_dpi,config->initial_locale,config->initial_font_percent);
    if (ui_error!=ESP_OK) { port->unlock(port->context); return ui_error; }
    if(ouse_ui_to_dp(width)<320 || ouse_ui_to_dp(height)<480) {
        port->unlock(port->context); return ESP_ERR_NOT_SUPPORTED;
    }
    core.port = *port; core.app_count=config->app_count+1;
    esp_err_t notification_error=ouse_notifications_init();
    esp_err_t permissions_error=ouse_permissions_init();
    if(permissions_error!=ESP_OK)return permissions_error;
    if(notification_error!=ESP_OK){port->unlock(port->context);return notification_error;}
    ouse_theme_set(config->initial_theme);
    core.brightness = config->initial_brightness;
    lv_display_set_default(port->display);
    core.screen = lv_obj_create(NULL);
    lv_obj_remove_flag(core.screen, LV_OBJ_FLAG_SCROLLABLE);
    ouse_theme_apply(core.screen, OUSE_STYLE_DESKTOP, 0);
    lv_obj_set_style_bg_opa(core.screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(core.screen, 0, 0);
    lv_obj_set_style_border_width(core.screen, 0, 0);
    ouse_ui_use_font(core.screen,16);
    core.home = panel(core.screen, 0, ouse_ui_dp(40), width, height - ouse_ui_dp(64));
    core.grid=panel(core.home,ouse_ui_dp(8),ouse_ui_dp(10),width-ouse_ui_dp(16),height-ouse_ui_dp(94));
    lv_obj_add_event_cb(core.grid,launcher_scrolled,LV_EVENT_SCROLL,NULL);
    lv_obj_add_flag(core.grid,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_SCROLL_ONE); lv_obj_set_scroll_dir(core.grid,LV_DIR_HOR);
    lv_obj_remove_flag(core.grid,LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_snap_x(core.grid,LV_SCROLL_SNAP_CENTER); lv_obj_set_scrollbar_mode(core.grid,LV_SCROLLBAR_MODE_OFF);
    core.app_host = panel(core.screen, 0, ouse_ui_dp(40), width, height - ouse_ui_dp(64));
    ouse_app_definition_t settings = {.id = "ouse.settings", .name = "设置", .icon = LV_SYMBOL_SETTINGS,
        .open = settings_open, .close = settings_close, .pause = settings_pause, .resume = settings_resume};
    // All definitions and duplicate ids were checked before acquiring the UI lock.
    esp_err_t apps_error=ouse_apps_init(core.app_host, app_changed);
    if(apps_error!=ESP_OK){lv_obj_delete(core.screen);port->unlock(port->context);return apps_error;}
    (void)ouse_apps_register(&settings);
    for (size_t i = 0; i < config->app_count; ++i) (void)ouse_apps_register(&config->apps[i]);
    ouse_apps_seal();
    rebuild_launcher();
    ouse_shell_config_t shell = {.screen=core.screen,.touch=core.port.touch,.brightness=shell_brightness,
        .set_brightness=apply_brightness,.save_brightness=shell_save,.settings_state=shell_settings_state,.back=shell_back,.open_settings=shell_settings_page,.navigation_changed=navigation_layout};
    ouse_screen_init(port,core.brightness);
    ouse_shell_init(&shell);
    ouse_dialog_init(core.screen);
    lv_timer_create(monitor_errors,250,NULL);
    lv_screen_load(core.screen);
    core.started = true;
    lv_timer_create(launcher_tick,50,NULL);
    lv_timer_create(file_preferences_tick,250,NULL);
    if(core.port.read_key)lv_timer_create(side_keys_tick,20,NULL);
    if(config->boot_animation)boot_animation_start();
    ouse_setup_init(config->device_setup);
    port->unlock(port->context);
    return ESP_OK;
}

bool ouse_core_is_started(void) { return core.started; }
const char *ouse_core_version(void) { return OUSE_CORE_VERSION; }

typedef struct { ouse_ui_callback_t callback; void *context; } ui_call_t;
static void dispatch_ui(void *argument)
{
    ui_call_t call = *(ui_call_t *)argument;
    lv_free(argument);
    --core.pending_ui; // adapter's LVGL task already holds the UI lock
    call.callback(call.context);
}

esp_err_t ouse_core_post_ui(ouse_ui_callback_t callback, void *context)
{
    if (!callback) return ESP_ERR_INVALID_ARG;
    if (!core.started) return ESP_ERR_INVALID_STATE;
    if (!core.port.lock(core.port.context, 2000)) return ESP_ERR_TIMEOUT;
    esp_err_t error = ESP_ERR_NO_MEM;
    if (core.pending_ui < 8) {
        ui_call_t *call = lv_malloc(sizeof(*call));
        if (call) {
            *call = (ui_call_t){.callback = callback, .context = context};
            if (lv_async_call(dispatch_ui, call) == LV_RESULT_OK) {
                ++core.pending_ui; error = ESP_OK;
            } else lv_free(call);
        }
    }
    core.port.unlock(core.port.context);
    return error;
}

void ouse_core_app_size(int32_t *width,int32_t *height)
{ *width=lv_obj_get_width(core.app_host);*height=lv_obj_get_height(core.app_host); }
esp_err_t ouse_core_user_check(const char *id) { return ouse_apps_user_check(id); }
static void refresh_apps(void)
{
    ouse_app_stats_t stats;ouse_app_get_stats(&stats);core.app_count=stats.registered;rebuild_launcher();
}
static const lv_image_dsc_t *registered_icon(const char *id)
{
    ouse_app_stats_t stats;ouse_app_get_stats(&stats);
    for(unsigned i=0;i<stats.registered;++i){
        ouse_app_info_t info;
        if(ouse_app_get_info(i,&info)==ESP_OK && !strcmp(info.id,id))return info.icon_image;
    }
    return NULL;
}
esp_err_t ouse_core_user_apply(const ouse_app_definition_t *definition)
{
    if(!definition)return ESP_ERR_INVALID_ARG;
    esp_err_t error=ouse_apps_validate(definition);if(error!=ESP_OK)return error;
    error=ouse_apps_user_check(definition->id);if(error!=ESP_OK)return error;
    const lv_image_dsc_t *old_icon=registered_icon(definition->id);
    ouse_manager_ui_release_icons();
    // Recents owns borrowed preview pointers: discard them before closing an app.
    ouse_shell_foreground();
    error=ouse_apps_user_apply(definition);
    if(error==ESP_OK){
        ouse_notifications_remove_app(definition->id);
        refresh_apps();
        // Old recents, app UI and launcher images have all been deleted. Drop
        // decoder/cache references under the UI lock before the worker frees
        // the previous package's borrowed descriptor and pixels.
        if(old_icon)lv_image_cache_drop(old_icon);
    }
    return error;
}
esp_err_t ouse_core_user_remove(const char *id)
{
    // Removal never needs a free registration slot. A previous attempt can
    // have closed the app before a storage purge failed; preserve NOT_FOUND
    // so that the installer can retry that purge even if the registry is full.
    ouse_app_info_t existing;
    esp_err_t found=ouse_app_find_info(id,&existing);if(found!=ESP_OK)return found;
    esp_err_t error=ouse_apps_user_check(id);if(error!=ESP_OK)return error;
    const lv_image_dsc_t *old_icon=registered_icon(id);
    ouse_manager_ui_release_icons();
    ouse_shell_foreground();error=ouse_apps_user_remove(id);
    if(error==ESP_OK){ouse_notifications_remove_app(id);launcher_forget(id);refresh_apps();if(old_icon)lv_image_cache_drop(old_icon);}
    return error;
}
void ouse_core_launcher_restored(void){launcher.restored=true;}
esp_err_t ouse_core_user_maintenance(const char *id,bool enabled)
{
    if(enabled)ouse_shell_foreground();
    esp_err_t error=ouse_apps_user_maintenance(id,enabled);
    if(error==ESP_OK && enabled)ouse_notifications_remove_app(id);
    return error;
}
