// SPDX-License-Identifier: MIT
#include "ouse_setup.h"
#include "ouse_core.h"
#include "ouse_identity.h"
#include "ouse_widgets.h"
#include "ouse_ime.h"
#include "ouse_devices.h"
#include "ouse_shell.h"
#include "ouse_dialog.h"
#include "ouse_device_ui.h"
#include <stdio.h>
#include <string.h>

enum {CLOSED,WELCOME,NAME,WIFI,OFFER,GUIDE,LOCK,NEW_PIN,CONFIRM_PIN,AUTH_CHANGE,AUTH_OFF,SETTINGS,RENAME,ERROR_PAGE,WIFI_PASSWORD,LANGUAGE,APPEARANCE,NAVIGATION};
static struct {
    bool enabled,locked,wizard;
    unsigned mode,next;
    uint32_t request,wifi_revision,retry_seconds;
    lv_obj_t *root,*body,*message,*digits_label,*name,*password,*networks,*wifi_status,*wifi_next;
    char selected_ssid[33];
    bool wifi_enable_queued,wifi_scan_queued;
    uint32_t wifi_retry_at;
    char digits[7],first[7],old[7];
    unsigned length;
    ouse_wifi_ap_t aps[OUSE_WIFI_AP_MAX];unsigned count;
} gate;
static void render(unsigned mode);
bool ouse_core_access_blocked(void){return gate.enabled && (gate.locked||gate.wizard||gate.root!=NULL);}
bool ouse_core_locked(void){return gate.enabled&&gate.locked;}
unsigned ouse_core_setup_step(void){return gate.enabled&&gate.wizard?gate.mode:0;}
static void clear_secrets(void){memset(gate.digits,0,sizeof(gate.digits));memset(gate.first,0,sizeof(gate.first));memset(gate.old,0,sizeof(gate.old));gate.length=0;}
static void message(const char *s){if(gate.message){lv_label_set_text(gate.message,ouse_ui_tr(s));if(*s)lv_obj_remove_flag(gate.message,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(gate.message,LV_OBJ_FLAG_HIDDEN);}}
static void close_gate(void)
{
    if(gate.root){ouse_ime_hide_root(gate.root);lv_obj_delete(gate.root);}gate.root=NULL;gate.mode=CLOSED;gate.request=0;clear_secrets();
}
static void paragraph(lv_obj_t *parent,const char *s,unsigned size)
{lv_obj_t *label=ouse_widget_text(parent,s,size);lv_obj_set_width(label,LV_PCT(100));lv_label_set_long_mode(label,LV_LABEL_LONG_WRAP);}
static void action(lv_obj_t *parent,const char *s,lv_event_cb_t cb,unsigned value)
{lv_obj_t *b=ouse_widget_button(parent,s,cb,(void *)(uintptr_t)value);lv_obj_set_width(b,LV_PCT(100));lv_obj_set_height(b,ouse_ui_dp(44));}
static void submit_result(esp_err_t e,unsigned next)
{if(e!=ESP_OK){gate.request=0;message(e==ESP_ERR_INVALID_ARG?"请检查输入内容":"暂时无法保存，请重试");}else{gate.next=next;message("正在保存…");}}
static void step_event(lv_event_t *e)
{
    if(gate.request){return;}unsigned step=(unsigned)(uintptr_t)lv_event_get_user_data(e);
    submit_result(ouse_identity_step(step,&gate.request),step==2?OFFER:step==3?GUIDE:CLOSED);
}
static void cancel_event(lv_event_t *e)
{(void)e;if(gate.request){return;}clear_secrets();if(gate.wizard)render(OFFER);else close_gate();}
static void start_event(lv_event_t *e){(void)e;render(LANGUAGE);}
static void preference_next(lv_event_t *e){render((unsigned)(uintptr_t)lv_event_get_user_data(e));}
static void language_event(lv_event_t *e)
{
    unsigned value=(unsigned)(uintptr_t)lv_event_get_user_data(e);
    if(ouse_core_set_ui(value,ouse_ui_font_percent())==ESP_OK)render(APPEARANCE);
}
static void appearance_event(lv_event_t *e)
{
    if(ouse_core_set_theme((ouse_theme_t)(uintptr_t)lv_event_get_user_data(e))==ESP_OK)render(NAVIGATION);
}
static void navigation_event(lv_event_t *e)
{
    unsigned value=(unsigned)(uintptr_t)lv_event_get_user_data(e);
    if(ouse_system_request_navigation((uint8_t)value)!=ESP_OK){message("暂时无法保存，请重试");return;}
    ouse_shell_set_navigation(value);render(NAME);
}
static void name_event(lv_event_t *e)
{
    (void)e;if(gate.request){return;}
    const char *name=lv_textarea_get_text(gate.name);
    if(!ouse_identity_valid_name(name)){message("请输入有效的设备名称");return;}
    submit_result(ouse_identity_name(name,&gate.request),gate.wizard?WIFI:CLOSED);
    if(gate.request)ouse_ime_hide();
}
static void scan_event(lv_event_t *e){(void)e;gate.wifi_enable_queued=gate.wifi_scan_queued=false;gate.wifi_retry_at=0;message("");}
static void wifi_back(lv_event_t *e){(void)e;render(WIFI);}
static void network_event(lv_event_t *e)
{
    unsigned i=(unsigned)(uintptr_t)lv_event_get_user_data(e);if(i>=gate.count)return;
    strcpy(gate.selected_ssid,gate.aps[i].ssid);
    if(gate.aps[i].secured)render(WIFI_PASSWORD);
    else message(ouse_wifi_connect(gate.selected_ssid,"")==ESP_OK?"正在连接…":"连接失败，请重试");
}
static void connect_event(lv_event_t *e)
{
    (void)e;ouse_ime_hide();esp_err_t r=ouse_wifi_connect(gate.selected_ssid,lv_textarea_get_text(gate.password));
    if(r==ESP_OK){render(WIFI);gate.wifi_scan_queued=true;message("正在连接…");}
    else message("连接失败，请检查密码");
}
static void pin_start(lv_event_t *e)
{
    if(gate.request){return;}unsigned mode=(unsigned)(uintptr_t)lv_event_get_user_data(e);clear_secrets();render(mode);
}
static void dots(void)
{char s[24]="";for(unsigned i=0;i<6;++i)strcat(s,i<gate.length?"* ":"_ ");lv_label_set_text(gate.digits_label,s);}
static void digit_event(lv_event_t *e)
{
    if(gate.request){return;}ouse_identity_state_t identity;ouse_identity_get(&identity);
    if(identity.retry_seconds && (gate.mode==LOCK||gate.mode==AUTH_CHANGE||gate.mode==AUTH_OFF)){message("尝试次数过多，请稍后重试");return;}
    unsigned digit=(unsigned)(uintptr_t)lv_event_get_user_data(e);
    if(digit==10){if(gate.length)gate.digits[--gate.length]=0;dots();return;}
    if(gate.length>=6){return;}gate.digits[gate.length++]=(char)('0'+digit);dots();if(gate.length!=6)return;
    if(gate.mode==NEW_PIN){strcpy(gate.first,gate.digits);render(CONFIRM_PIN);return;}
    if(gate.mode==CONFIRM_PIN){
        if(strcmp(gate.first,gate.digits)){memset(gate.first,0,sizeof(gate.first));render(NEW_PIN);message("两次密码不一致，请重新设置");return;}
        submit_result(ouse_identity_pin(gate.old,gate.digits,&gate.request),gate.wizard?GUIDE:SETTINGS);
    }else if(gate.mode==AUTH_OFF){submit_result(ouse_identity_pin(gate.digits,"",&gate.request),SETTINGS);}
    else {
        if(gate.mode==AUTH_CHANGE)strcpy(gate.old,gate.digits);
        submit_result(ouse_identity_verify(gate.digits,&gate.request),gate.mode==LOCK?CLOSED:NEW_PIN);
    }
    memset(gate.digits,0,sizeof(gate.digits));gate.length=0;dots();
}
static lv_obj_t *input(lv_obj_t *parent,const char *placeholder,bool password,unsigned maximum)
{
    lv_obj_t *t=lv_textarea_create(parent);lv_textarea_set_one_line(t,true);
    lv_textarea_set_max_length(t,maximum);lv_textarea_set_placeholder_text(t,ouse_ui_tr(placeholder));lv_textarea_set_password_mode(t,password);ouse_ui_use_font(t,16);
    ouse_theme_apply(t,OUSE_STYLE_PANEL,0);lv_obj_set_size(t,LV_PCT(100),ouse_ui_dp(48));lv_obj_set_style_radius(t,ouse_ui_dp(12),0);lv_obj_set_style_pad_all(t,ouse_ui_dp(12),0);
    (void)ouse_ime_attach(t,parent,OUSE_IME_RESIZE,password?OUSE_IME_PASSWORD:OUSE_IME_TEXT);return t;
}
static void render(unsigned mode)
{
    if(gate.root){ouse_ime_hide_root(gate.root);lv_obj_delete(gate.root);}gate.root=NULL;
    gate.mode=mode;gate.length=0;memset(gate.digits,0,sizeof(gate.digits));
    gate.name=gate.password=gate.networks=gate.wifi_status=gate.digits_label=gate.wifi_next=NULL;
    if(mode==CLOSED){close_gate();return;}
    ouse_identity_state_t identity;ouse_identity_get(&identity);
    gate.root=ouse_widget_box(lv_screen_active(),OUSE_STYLE_APP,0);lv_obj_set_size(gate.root,LV_PCT(100),LV_PCT(100));lv_obj_set_pos(gate.root,0,0);
    lv_obj_add_flag(gate.root,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(gate.root,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_EVENT_BUBBLE|LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_opa(gate.root,LV_OPA_COVER,0);lv_obj_set_style_pad_all(gate.root,ouse_ui_dp(ouse_core_landscape()?12:18),0);lv_obj_set_style_pad_row(gate.root,ouse_ui_dp(ouse_core_landscape()?6:10),0);lv_obj_set_flex_flow(gate.root,LV_FLEX_FLOW_COLUMN);
    const char *title=mode==LANGUAGE?"选择语言 / Language":mode==APPEARANCE?"选择主题":mode==NAVIGATION?"选择导航方式":mode==WELCOME?"欢迎使用 Ouse":mode==NAME||mode==RENAME?"为设备命名":mode==WIFI?"连接 Wi-Fi":mode==WIFI_PASSWORD?"输入网络密码":mode==GUIDE?"简单使用":mode==SETTINGS?"密钥":mode==LOCK?"输入锁屏密码":mode==NEW_PIN?"设置六位数字密码":mode==CONFIRM_PIN?"再次输入密码":mode==OFFER?"设置锁屏密码":"验证原密码";
    paragraph(gate.root,title,24);
    gate.body=ouse_widget_box(gate.root,OUSE_STYLE_APP,0);lv_obj_set_width(gate.body,LV_PCT(100));lv_obj_set_height(gate.body,0);lv_obj_set_flex_grow(gate.body,1);lv_obj_set_flex_flow(gate.body,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(gate.body,ouse_ui_dp(10),0);lv_obj_add_flag(gate.body,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(gate.body,LV_DIR_VER);
    gate.message=ouse_widget_text(gate.root,"",14);lv_obj_set_width(gate.message,LV_PCT(100));lv_obj_add_flag(gate.message,LV_OBJ_FLAG_HIDDEN);
    if(mode==WELCOME){action(gate.root,"开始使用",start_event,0);}
    else if(mode==LANGUAGE){
        action(gate.body,"简体中文",language_event,0);action(gate.body,"English",language_event,1);action(gate.root,"跳过",preference_next,APPEARANCE);
    }else if(mode==APPEARANCE){
        action(gate.body,"浅色",appearance_event,OUSE_THEME_LIGHT);action(gate.body,"深色",appearance_event,OUSE_THEME_DARK);action(gate.root,"跳过",preference_next,NAVIGATION);
    }else if(mode==NAVIGATION){
        action(gate.body,"全面屏手势",navigation_event,0);action(gate.body,"三键导航",navigation_event,1);action(gate.root,"跳过",preference_next,NAME);
    }else if(mode==NAME||mode==RENAME){paragraph(gate.body,"也用于蓝牙名称",14);gate.name=input(gate.body,"设备名称",false,24);lv_textarea_set_text(gate.name,identity.name);action(gate.root,"下一步",name_event,0);if(mode==RENAME)action(gate.root,"取消",cancel_event,0);}
    else if(mode==WIFI){
        gate.wifi_status=ouse_widget_text(gate.body,"正在搜索…",14);lv_obj_set_width(gate.wifi_status,LV_PCT(100));
        gate.networks=ouse_widget_box(gate.body,OUSE_STYLE_APP,0);lv_obj_set_size(gate.networks,LV_PCT(100),LV_SIZE_CONTENT);lv_obj_set_flex_flow(gate.networks,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(gate.networks,ouse_ui_dp(8),0);
        gate.wifi_revision=UINT32_MAX;gate.wifi_enable_queued=gate.wifi_scan_queued=false;gate.wifi_retry_at=0;
        lv_obj_t *footer=ouse_widget_box(gate.root,OUSE_STYLE_APP,0);lv_obj_set_size(footer,LV_PCT(100),LV_SIZE_CONTENT);lv_obj_set_flex_flow(footer,LV_FLEX_FLOW_ROW);lv_obj_set_style_pad_column(footer,ouse_ui_dp(10),0);
        lv_obj_t *refresh=ouse_widget_button(footer,"刷新",scan_event,NULL);lv_obj_set_width(refresh,0);lv_obj_set_flex_grow(refresh,1);lv_obj_set_height(refresh,ouse_ui_dp(44));
        lv_obj_t *next=ouse_widget_button(footer,"跳过",step_event,(void *)(uintptr_t)2);lv_obj_set_width(next,0);lv_obj_set_flex_grow(next,1);lv_obj_set_height(next,ouse_ui_dp(44));gate.wifi_next=lv_obj_get_child(next,0);
    }else if(mode==WIFI_PASSWORD){
        paragraph(gate.body,gate.selected_ssid,18);gate.password=input(gate.body,"Wi-Fi 密码",true,63);
        lv_obj_add_event_cb(gate.password,connect_event,LV_EVENT_READY,NULL);
        action(gate.root,"连接",connect_event,0);action(gate.root,"返回",wifi_back,0);
    }else if(mode==OFFER){action(gate.body,"设置密码",pin_start,NEW_PIN);action(gate.root,"稍后设置",step_event,3);}
    else if(mode==GUIDE){
        paragraph(gate.body,"左右滑动切页",18);paragraph(gate.body,"顶部下拉快捷控制",18);
        paragraph(gate.body,ouse_shell_navigation()?"底部按键：返回、桌面、最近应用":"侧边滑动返回\n底部上滑回到桌面",18);
        action(gate.root,"进入桌面",step_event,4);
    }else if(mode==SETTINGS){
        paragraph(gate.body,identity.pin_enabled?"锁屏密码已开启":"锁屏密码未开启",18);
        action(gate.body,identity.pin_enabled?"修改密码":"设置密码",pin_start,identity.pin_enabled?AUTH_CHANGE:NEW_PIN);
        if(identity.pin_enabled){action(gate.body,"关闭密码",pin_start,AUTH_OFF);}action(gate.root,"返回",cancel_event,0);
    }else if(mode==ERROR_PAGE){paragraph(gate.body,"设备配置无法读取。请重启后重试，原数据未自动清除。",18);}
    else {
        if(mode==LOCK&&!ouse_core_landscape())paragraph(gate.body,identity.name,18);
        lv_obj_remove_flag(gate.body,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_flex_align(gate.body,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
        gate.digits_label=ouse_widget_text(gate.body,"",28);lv_obj_set_width(gate.digits_label,LV_PCT(100));lv_obj_set_style_text_align(gate.digits_label,LV_TEXT_ALIGN_CENTER,0);dots();
        lv_obj_t *keys=ouse_widget_box(gate.root,OUSE_STYLE_APP,0);lv_obj_set_size(keys,LV_PCT(100),LV_SIZE_CONTENT);lv_obj_set_flex_flow(keys,LV_FLEX_FLOW_ROW_WRAP);lv_obj_set_style_pad_row(keys,ouse_ui_dp(8),0);lv_obj_set_style_pad_column(keys,ouse_ui_dp(8),0);lv_obj_set_flex_align(keys,LV_FLEX_ALIGN_SPACE_BETWEEN,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_START);
        for(unsigned i=0;i<12;++i){if(i==9){lv_obj_t *left=mode==LOCK?ouse_widget_box(keys,OUSE_STYLE_APP,0):ouse_widget_button(keys,"取消",cancel_event,NULL);lv_obj_set_size(left,LV_PCT(31),ouse_ui_dp(ouse_core_landscape()?40:52));continue;}unsigned digit=i==10?0:i==11?10:i+1;char label[8];if(digit==10)strcpy(label,LV_SYMBOL_BACKSPACE);else snprintf(label,sizeof(label),"%u",digit);
            lv_obj_t *b=ouse_widget_button(keys,label,digit_event,(void *)(uintptr_t)digit);lv_obj_set_size(b,LV_PCT(31),ouse_ui_dp(ouse_core_landscape()?40:52));}
    }
    if(mode==WIFI_PASSWORD)ouse_ime_focus(gate.password);
}
void ouse_setup_lock(void)
{
    if(!gate.enabled){return;}ouse_identity_state_t identity;ouse_identity_get(&identity);
    if(!identity.pin_enabled)return;
    gate.locked=true;gate.request=0;clear_secrets();ouse_ime_hide();ouse_device_ui_close_modal();ouse_dialog_dismiss();ouse_shell_foreground();render(LOCK);
}
void ouse_setup_settings(void){if(!gate.enabled||gate.locked||gate.wizard)return;clear_secrets();render(SETTINGS);}
void ouse_setup_rename(void){if(!gate.enabled||gate.locked||gate.wizard)return;clear_secrets();render(RENAME);}
static void tick(lv_timer_t *timer)
{
    (void)timer;if(!gate.enabled){return;}ouse_identity_state_t identity;ouse_identity_get(&identity);
    if(gate.root && !ouse_ime_active() && lv_obj_get_index(gate.root)!=(int32_t)lv_obj_get_child_count(lv_screen_active())-1)lv_obj_move_foreground(gate.root);
    if(gate.request && !identity.pending && identity.completed==gate.request){
        gate.request=0;
        if(identity.error!=ESP_OK){memset(gate.old,0,sizeof(gate.old));gate.length=0;memset(gate.digits,0,sizeof(gate.digits));if(gate.digits_label)dots();message(identity.retry_seconds?"尝试次数过多，请等待 30 秒":"密码不正确或保存失败，请重试");}
        else {
            unsigned next=gate.next;
            if(next==CLOSED){
                if(gate.mode==LOCK)gate.locked=false;
                if(identity.setup_done)gate.wizard=false;
                if(gate.wizard && identity.name[0]){clear_secrets();render(identity.setup_step<=1?WIFI:identity.setup_step==2&&!identity.pin_enabled?OFFER:GUIDE);}
                else close_gate();
            }
            else if(gate.mode==CONFIRM_PIN&&gate.wizard){clear_secrets();render(GUIDE);submit_result(ouse_identity_step(3,&gate.request),GUIDE);}
            else {if(next==SETTINGS)clear_secrets();render(next);}
        }
    }
    if(gate.mode==LOCK||gate.mode==AUTH_CHANGE||gate.mode==AUTH_OFF){
        if(identity.retry_seconds){char s[64];snprintf(s,sizeof(s),ouse_ui_locale()?"Try again in %lu seconds":"请等待 %lu 秒后重试",(unsigned long)identity.retry_seconds);message(s);}
        else if(gate.retry_seconds)message("可以重新输入密码");
        gate.retry_seconds=identity.retry_seconds;
    }
    if(gate.mode==WIFI){ouse_wifi_state_t wifi;if(ouse_wifi_get(&wifi)==ESP_OK){
        if((int32_t)(lv_tick_get()-gate.wifi_retry_at)>=0){
            if(!wifi.enabled && !gate.wifi_enable_queued){gate.wifi_enable_queued=ouse_wifi_set_enabled(true)==ESP_OK;gate.wifi_retry_at=lv_tick_get()+1000;}
            else if(wifi.enabled && wifi.ready && !wifi.scanning && !wifi.connecting && !gate.wifi_scan_queued){gate.wifi_scan_queued=ouse_wifi_scan()==ESP_OK;gate.wifi_retry_at=lv_tick_get()+1000;}
        }
        if(wifi.revision!=gate.wifi_revision){
            gate.wifi_revision=wifi.revision;char s[96];
            if(wifi.connected)snprintf(s,sizeof(s),ouse_ui_locale()?"Connected: %s":"已连接：%s",wifi.ssid);
            else snprintf(s,sizeof(s),"%s",wifi.connecting?"正在连接…":!wifi.enabled||!wifi.ready?"正在开启 Wi-Fi…":wifi.scanning||wifi.request_id!=wifi.completed_id?"正在搜索…":wifi.error!=ESP_OK?"连接失败，请重试":wifi.count?"选择网络":"未找到网络，点击刷新");
            lv_label_set_text(gate.wifi_status,ouse_ui_tr(s));ouse_ui_unbind_text(gate.wifi_next);lv_label_set_text(gate.wifi_next,ouse_ui_tr(wifi.connected?"下一步":"跳过"));
            if(wifi.connected)message("");
            lv_obj_clean(gate.networks);gate.count=wifi.count>OUSE_WIFI_AP_MAX?OUSE_WIFI_AP_MAX:wifi.count;memcpy(gate.aps,wifi.aps,sizeof(gate.aps));
            for(unsigned i=0;i<gate.count;++i)if(gate.aps[i].ssid[0])action(gate.networks,gate.aps[i].ssid,network_event,i);
        }
    }}
}
void ouse_setup_init(bool enabled)
{
    gate.enabled=enabled;if(!enabled)return;ouse_identity_state_t identity;ouse_identity_get(&identity);
    gate.wizard=!identity.setup_done;
    if(!identity.ready)render(ERROR_PAGE);
    else if(identity.pin_enabled){gate.locked=true;render(LOCK);}
    else if(gate.wizard)render(!identity.name[0]?WELCOME:identity.setup_step<=1?WIFI:identity.setup_step==2?OFFER:GUIDE);
    lv_timer_create(tick,200,NULL);
}
