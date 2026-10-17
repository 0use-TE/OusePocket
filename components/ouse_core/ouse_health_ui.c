// SPDX-License-Identifier: MIT
#include "ouse_health_ui.h"
#include "ouse_health.h"
#include "ouse_widgets.h"
#include "ouse_dialog.h"
#include <stdio.h>
#include <string.h>

typedef struct {lv_obj_t *row,*name,*detail,*badge,*button,*glyph;} activity_row_t;
typedef struct {lv_obj_t *tile,*name,*status,*glyph;} service_tile_t;
static lv_obj_t *page,*summary,*summary_icon,*totals,*activity_count,*empty,*activity_list,*service_grid,*resume_button,*restart_button;
static activity_row_t rows[OUSE_HEALTH_ACTIVITIES];
static service_tile_t tiles[OUSE_HEALTH_SERVICES];
static ouse_activity_t activities[OUSE_HEALTH_ACTIVITIES];
static const char *local(const char *zh,const char *en){return ouse_ui_locale()==1 ? en : zh;}
static void text(lv_obj_t *label,const char *value){if(strcmp(lv_label_get_text(label),value))lv_label_set_text(label,value);}
static void hidden(lv_obj_t *obj,bool hide){if(hide)lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(obj,LV_OBJ_FLAG_HIDDEN);}
static void disabled(lv_obj_t *obj,bool disable){if(disable)lv_obj_add_state(obj,LV_STATE_DISABLED);else lv_obj_remove_state(obj,LV_STATE_DISABLED);}
static void color(lv_obj_t *obj,uint32_t value){lv_obj_set_style_text_color(obj,lv_color_hex(value),0);}
static lv_obj_t *box(lv_obj_t *parent,lv_flex_flow_t flow,int padding)
{
    lv_obj_t *obj=ouse_widget_box(parent,OUSE_STYLE_PANEL,14);
    lv_obj_set_width(obj,LV_PCT(100));lv_obj_set_height(obj,LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(obj,flow);lv_obj_set_style_pad_all(obj,ouse_ui_dp(padding),0);
    // Layout-only containers must not paint over the gaps between cards.
    if(!padding)lv_obj_set_style_bg_opa(obj,LV_OPA_TRANSP,0);
    lv_obj_set_style_pad_gap(obj,ouse_ui_dp(8),0);return obj;
}
static lv_obj_t *glyph(lv_obj_t *parent,const char *symbol)
{
    lv_obj_t *obj=lv_label_create(parent);lv_label_set_text(obj,symbol);
    lv_obj_set_style_text_font(obj,&lv_font_montserrat_24,0);lv_obj_remove_flag(obj,LV_OBJ_FLAG_CLICKABLE);return obj;
}
static void heading(lv_obj_t *parent,const char *zh,const char *en)
{
    lv_obj_t *obj=ouse_widget_text(parent,"",14);ouse_ui_text_pair(obj,zh,en);
    ouse_theme_apply(obj,OUSE_STYLE_MUTED,0);
}
static void restart(void){if(ouse_control_request(OUSE_CONTROL_RESTART,NULL,0,0)!=ESP_OK)ouse_dialog_show("操作未能提交，请稍后重试",NULL,NULL);}
static void restart_event(lv_event_t *event){(void)event;ouse_dialog_show("结束应用与原生任务，保存完成后重启？","重启",restart);}
static void resume_event(lv_event_t *event){(void)event;(void)ouse_control_request(OUSE_CONTROL_RESUME,NULL,0,0);}
static void status_event(lv_event_t *event)
{
    (void)event;ouse_health_snapshot_t h;ouse_health_get(&h);char buffer[192];
    snprintf(buffer,sizeof(buffer),"%s%s%s",local("系统状态：","System phase: "),h.phase,h.blocker[0] ? "\n" : "");
    size_t used=strlen(buffer);snprintf(buffer+used,sizeof(buffer)-used,"%s",h.blocker);
    ouse_dialog_show(buffer,NULL,NULL);
}
static void service_event(lv_event_t *event)
{
    unsigned i=(unsigned)(uintptr_t)lv_event_get_user_data(event);ouse_health_snapshot_t h;ouse_health_get(&h);
    if(i>=h.service_count || i>=OUSE_HEALTH_SERVICES)return;
    const ouse_service_health_t *s=&h.services[i];char buffer[192];
    snprintf(buffer,sizeof(buffer),"%s\n%s\nerror: 0x%x\nactive: %u\noldest: %llu ms",s->name,
        ouse_service_status_name(s->status),(unsigned)s->error,s->active,(unsigned long long)s->oldest_ms);
    ouse_dialog_show(buffer,NULL,NULL);
}
static void stop_event(lv_event_t *event)
{
    unsigned index=(unsigned)(uintptr_t)lv_event_get_user_data(event);if(index>=OUSE_HEALTH_ACTIVITIES)return;
    const ouse_activity_t *a=&activities[index];
    if(!a->cancellable || a->stopping)return;
    if(ouse_control_request(OUSE_CONTROL_STOP,a->app_id,a->task_id,a->instance_id)!=ESP_OK)ouse_dialog_show("操作未能提交，请稍后重试",NULL,NULL);
}
static void activity_build(unsigned i)
{
    activity_row_t *r=&rows[i];r->row=box(activity_list,LV_FLEX_FLOW_COLUMN,12);
    lv_obj_t *top=box(r->row,LV_FLEX_FLOW_ROW,0);
    lv_obj_set_flex_align(top,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    r->glyph=glyph(top,LV_SYMBOL_LIST);
    lv_obj_t *copy=box(top,LV_FLEX_FLOW_COLUMN,0);lv_obj_set_width(copy,0);lv_obj_set_flex_grow(copy,1);
    lv_obj_set_style_pad_gap(copy,ouse_ui_dp(3),0);
    r->name=ouse_widget_text(copy,"",16);lv_obj_set_width(r->name,LV_PCT(100));
    r->badge=ouse_widget_text(copy,"",12);lv_obj_set_width(r->badge,LV_PCT(100));
    r->button=ouse_widget_button(top,"",stop_event,(void *)(uintptr_t)i);
    lv_obj_set_style_pad_hor(r->button,ouse_ui_dp(10),0);lv_obj_set_style_opa(r->button,LV_OPA_40,LV_STATE_DISABLED);
    r->detail=ouse_widget_text(r->row,"",13);lv_obj_set_width(r->detail,LV_PCT(100));
    ouse_theme_apply(r->detail,OUSE_STYLE_MUTED,0);
}
static const char *operation(const ouse_activity_t *a)
{
    if(a->javascript)return a->reason;
    if(strstr(a->reason,"usbDuplex"))return local("电脑音频与麦克风","Computer audio & microphone");
    if(strstr(a->reason,"usbSharing"))return local("Wi-Fi 网络共享","Wi-Fi sharing");
    if(strstr(a->reason,"record"))return local("录音","Recording");
    if(strstr(a->reason,"play"))return local("音频播放","Audio playback");
    if(strstr(a->reason,"alarm"))return local("闹铃响铃","Alarm ringing");
    return a->reason;
}
static void service_build(unsigned i)
{
    service_tile_t *t=&tiles[i];t->tile=box(service_grid,LV_FLEX_FLOW_COLUMN,10);
    lv_obj_add_flag(t->tile,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(t->tile,service_event,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
    lv_obj_set_width(t->tile,LV_PCT(48));lv_obj_set_style_min_height(t->tile,ouse_ui_dp(82),0);
    lv_obj_t *top=box(t->tile,LV_FLEX_FLOW_ROW,0);
    lv_obj_set_flex_align(top,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    t->glyph=glyph(top,LV_SYMBOL_SETTINGS);
    t->name=ouse_widget_text(top,"",14);lv_obj_set_width(t->name,0);lv_obj_set_flex_grow(t->name,1);
    t->status=ouse_widget_text(t->tile,"",13);lv_obj_set_width(t->status,LV_PCT(100));
}
void ouse_health_ui_build(lv_obj_t *parent)
{
    page=parent;
    lv_obj_t *hero=box(parent,LV_FLEX_FLOW_ROW,14);
    lv_obj_add_flag(hero,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(hero,status_event,LV_EVENT_CLICKED,NULL);
    lv_obj_set_flex_align(hero,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    summary_icon=glyph(hero,LV_SYMBOL_OK);
    lv_obj_t *copy=box(hero,LV_FLEX_FLOW_COLUMN,0);lv_obj_set_width(copy,0);lv_obj_set_flex_grow(copy,1);
    summary=ouse_widget_text(copy,"",18);lv_obj_set_width(summary,LV_PCT(100));
    totals=ouse_widget_text(copy,"",13);lv_obj_set_width(totals,LV_PCT(100));ouse_theme_apply(totals,OUSE_STYLE_MUTED,0);
    lv_obj_t *title=box(parent,LV_FLEX_FLOW_ROW,0);heading(title,"后台活动","Background activity");
    activity_count=ouse_widget_text(title,"",14);ouse_theme_apply(activity_count,OUSE_STYLE_ACCENT_TEXT,0);
    activity_list=box(parent,LV_FLEX_FLOW_COLUMN,0);
    empty=box(parent,LV_FLEX_FLOW_ROW,14);glyph(empty,LV_SYMBOL_OK);heading(empty,"暂无后台活动","No background activity");
    heading(parent,"系统服务","System services");service_grid=box(parent,LV_FLEX_FLOW_ROW_WRAP,0);
    heading(parent,"系统操作","System actions");
    restart_button=ouse_widget_button(parent,"",restart_event,NULL);ouse_widget_button_expand(restart_button);
    ouse_ui_text_pair(lv_obj_get_child(restart_button,0),"安全重启","Safe restart");
    resume_button=ouse_widget_button(parent,"",resume_event,NULL);ouse_widget_button_expand(resume_button);
    ouse_ui_text_pair(lv_obj_get_child(resume_button,0),"恢复使用","Resume system");hidden(resume_button,true);
}
void ouse_health_ui_pause(void)
{
    if(activity_list)lv_obj_clean(activity_list);
    if(service_grid)lv_obj_clean(service_grid);
    memset(rows,0,sizeof(rows));memset(tiles,0,sizeof(tiles));memset(activities,0,sizeof(activities));
}
void ouse_health_ui_refresh(void)
{
    if(!summary)return;
    // The summary scrolls out of the viewport. That is not a page transition:
    // deleting the cards then collapses content height and resets scroll_y.
    if(!lv_obj_is_visible(page)){if(tiles[0].tile || rows[0].row)ouse_health_ui_pause();return;}
    // Do not fight touch scrolling/inertia with live content/layout changes.
    if(lv_obj_is_scrolling(page))return;
    // Freeze identities until click delivery, including other action controls.
    if(lv_obj_has_state(restart_button,LV_STATE_PRESSED) || lv_obj_has_state(resume_button,LV_STATE_PRESSED))return;
    for(unsigned i=0;i<OUSE_HEALTH_ACTIVITIES;++i)if(rows[i].button && lv_obj_has_state(rows[i].button,LV_STATE_PRESSED))return;
    lv_obj_t *anchor=NULL;lv_area_t before;
    if(lv_obj_get_scroll_y(page)>0){
        for(unsigned i=0;i<OUSE_HEALTH_SERVICES;++i)if(tiles[i].tile && lv_obj_is_visible(tiles[i].tile)){
            anchor=tiles[i].tile;lv_obj_get_coords(anchor,&before);break;
        }
    }
    ouse_health_snapshot_t health;ouse_health_get(&health);char buffer[160];ouse_theme_colors_t c=ouse_theme_colors();
    bool blocked=!strcmp(health.phase,"blocked"),draining=!strcmp(health.phase,"draining");
    text(summary,!health.available ? local("服务启动中","Starting services") : blocked ? local("收尾未完成","Shutdown paused") : draining ? local("正在安全收尾","Finishing safely") : local("系统运行正常","System running"));
    text(summary_icon,blocked ? LV_SYMBOL_WARNING : draining ? LV_SYMBOL_REFRESH : LV_SYMBOL_OK);color(summary_icon,blocked ? c.error : c.accent);
    unsigned ready=0;for(unsigned i=0;i<health.service_count && i<OUSE_HEALTH_SERVICES;++i)if(health.services[i].status==OUSE_SERVICE_READY)++ready;
    if(health.available && !blocked && !draining && ready<health.service_count){text(summary,local("服务需要关注","Services need attention"));text(summary_icon,LV_SYMBOL_WARNING);color(summary_icon,c.error);}
    snprintf(buffer,sizeof(buffer),local("%u 项活动 · %u/%u 服务就绪","%u active · %u/%u services ready"),health.activities_total,ready,health.service_count);text(totals,buffer);
    snprintf(buffer,sizeof(buffer),"%u",health.activities_total);text(activity_count,buffer);
    hidden(empty,health.activity_count!=0);hidden(activity_list,health.activity_count==0);
    hidden(resume_button,!blocked);disabled(restart_button,!health.available || health.quiescing);
    for(unsigned i=0;i<OUSE_HEALTH_ACTIVITIES;++i){activity_row_t *r=&rows[i];
        if(i>=health.activity_count){if(r->row)hidden(r->row,true);continue;}
        if(!r->row)activity_build(i);
        activities[i]=health.activities[i];const ouse_activity_t *a=&activities[i];ouse_app_info_t info;
        const char *name=a->app_id,*symbol=LV_SYMBOL_LIST;
        if(ouse_app_find_info(a->app_id,&info)==ESP_OK){name=info.name;symbol=info.icon;}
        else if(!strcmp(a->app_id,"ouse.usb")){name="USB OTG";symbol=LV_SYMBOL_USB;}
        else if(!strcmp(a->app_id,"ouse.alarm")){name=local("系统闹钟","System alarm");symbol=LV_SYMBOL_BELL;}
        text(r->name,name);text(r->glyph,symbol);color(r->glyph,c.accent);
        text(r->badge,a->stopping ? local("正在释放资源","Releasing resources") : a->javascript ? local("后台脚本","Background script") : local("原生服务","Native service"));
        color(r->badge,a->stopping ? c.secondary : c.accent);text(r->detail,operation(a));hidden(r->row,false);
        text(lv_obj_get_child(r->button,0),a->stopping ? local("停止中","Stopping") : local("停止","Stop"));
        disabled(r->button,!a->cancellable || a->stopping || health.quiescing);
    }
    static const struct {const char *id,*zh,*en,*icon;} services[]={
        {"wifi","无线网络","Wi-Fi",LV_SYMBOL_WIFI},{"audio","音频","Audio",LV_SYMBOL_VOLUME_MAX},
        {"storage","存储","Storage",LV_SYMBOL_DRIVE},{"settings","设置","Settings",LV_SYMBOL_SETTINGS},
        {"network","网络请求","Network",LV_SYMBOL_DOWNLOAD},{"alarm","闹钟","Alarms",LV_SYMBOL_BELL},
        {"installer","安装服务","Installer",LV_SYMBOL_SAVE},{"usb","USB OTG","USB OTG",LV_SYMBOL_USB}};
    static const char *zh[]={"启动中","就绪","存在错误","恢复中","不可用"};
    static const char *en[]={"Starting","Ready","Error","Recovering","Unavailable"};
    for(unsigned i=0;i<OUSE_HEALTH_SERVICES;++i){service_tile_t *t=&tiles[i];
        if(i>=health.service_count){if(t->tile)hidden(t->tile,true);continue;}if(!t->tile)service_build(i);
        const ouse_service_health_t *s=&health.services[i];const char *name=s->name,*symbol=LV_SYMBOL_SETTINGS;
        for(unsigned j=0;j<sizeof(services)/sizeof(services[0]);++j)if(!strcmp(s->name,services[j].id)){name=local(services[j].zh,services[j].en);symbol=services[j].icon;break;}
        text(t->name,name);text(t->glyph,symbol);color(t->glyph,c.accent);unsigned status=(unsigned)s->status;
        const char *state=status<5 ? local(zh[status],en[status]) : local("未知","Unknown");
        if(s->active)snprintf(buffer,sizeof(buffer),local("%s · %u 项活动","%s · %u active"),state,s->active);else snprintf(buffer,sizeof(buffer),"%s",state);
        text(t->status,buffer);color(t->status,s->status==OUSE_SERVICE_DEGRADED || s->status==OUSE_SERVICE_UNAVAILABLE ? c.error : s->status==OUSE_SERVICE_READY ? c.accent : c.secondary);hidden(t->tile,false);
    }
    if(anchor && !lv_obj_has_flag(anchor,LV_OBJ_FLAG_HIDDEN)){
        lv_obj_update_layout(page);lv_area_t after;lv_obj_get_coords(anchor,&after);
        int delta=after.y1-before.y1;
        if(delta)lv_obj_scroll_to_y(page,lv_obj_get_scroll_y(page)+delta,LV_ANIM_OFF);
    }
}
void ouse_health_ui_close(void)
{
    page=summary=summary_icon=totals=activity_count=empty=activity_list=service_grid=resume_button=restart_button=NULL;
    memset(rows,0,sizeof(rows));memset(tiles,0,sizeof(tiles));memset(activities,0,sizeof(activities));
}
