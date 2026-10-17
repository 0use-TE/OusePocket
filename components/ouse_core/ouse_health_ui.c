// SPDX-License-Identifier: MIT
#include "ouse_health_ui.h"
#include "ouse_health.h"
#include "ouse_widgets.h"
#include "ouse_dialog.h"
#include <stdio.h>
#include <string.h>
static lv_obj_t *summary,*services,*rows[OUSE_HEALTH_ACTIVITIES],*labels[OUSE_HEALTH_ACTIVITIES],*buttons[OUSE_HEALTH_ACTIVITIES];
static ouse_activity_t activities[OUSE_HEALTH_ACTIVITIES];
static void text(lv_obj_t *label,const char *value){if(strcmp(lv_label_get_text(label),value))lv_label_set_text(label,value);}
static void restart(void){if(ouse_control_request(OUSE_CONTROL_RESTART,NULL,0,0)!=ESP_OK)ouse_dialog_show("操作未能提交，请稍后重试",NULL,NULL);}
static void restart_event(lv_event_t *event){(void)event;ouse_dialog_show("结束应用与原生任务，保存完成后重启？","重启",restart);}
static void resume_event(lv_event_t *event){(void)event;(void)ouse_control_request(OUSE_CONTROL_RESUME,NULL,0,0);}
static void stop_event(lv_event_t *event)
{
    unsigned index=(unsigned)(uintptr_t)lv_event_get_user_data(event);if(index>=OUSE_HEALTH_ACTIVITIES)return;
    const ouse_activity_t *a=&activities[index];
    if(ouse_control_request(OUSE_CONTROL_STOP,a->app_id,a->task_id,a->instance_id)!=ESP_OK)ouse_dialog_show("操作未能提交，请稍后重试",NULL,NULL);
}
void ouse_health_ui_build(lv_obj_t *parent)
{
    summary=ouse_widget_text(parent,"",14);lv_obj_set_width(summary,LV_PCT(100));
    services=ouse_widget_text(parent,"",14);lv_obj_set_width(services,LV_PCT(100));
    ouse_widget_button(parent,"安全重启",restart_event,NULL);
    ouse_widget_button(parent,"收尾失败后恢复使用",resume_event,NULL);
    ouse_widget_text(parent,"后台脚本与原生任务分别管理",14);
    for(unsigned i=0;i<OUSE_HEALTH_ACTIVITIES;++i){
        rows[i]=ouse_widget_box(parent,OUSE_STYLE_PANEL,12);lv_obj_set_width(rows[i],LV_PCT(100));lv_obj_set_height(rows[i],LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(rows[i],LV_FLEX_FLOW_COLUMN);
        labels[i]=ouse_widget_text(rows[i],"",14);lv_obj_set_width(labels[i],LV_PCT(100));
        buttons[i]=ouse_widget_button(rows[i],"停止",stop_event,(void *)(uintptr_t)i);
        lv_obj_add_flag(rows[i],LV_OBJ_FLAG_HIDDEN);
    }
    ouse_health_ui_refresh();
}
void ouse_health_ui_refresh(void)
{
    if(!summary || !lv_obj_is_visible(summary))return;
    // Keep row identity stable from press through click delivery.
    for(unsigned i=0;i<OUSE_HEALTH_ACTIVITIES;++i)if(buttons[i] && lv_obj_has_state(buttons[i],LV_STATE_PRESSED))return;
    ouse_health_snapshot_t health;ouse_health_get(&health);char buffer[768];
    snprintf(buffer,sizeof(buffer),"%s\n%s%s\n活动 %u / 显示 %u",
        !health.available ? "服务管理尚未就绪" : !strcmp(health.phase,"draining") ? "正在安全收尾" : !strcmp(health.phase,"blocked") ? "收尾未完成，未重启" : "系统运行中",
        health.blocker[0] ? "等待：" : "",health.blocker,health.activities_total,health.activity_count);text(summary,buffer);
    size_t length=0;
    static const char *names[]={"启动中","就绪","存在错误","恢复中","不可用"};
    for(unsigned i=0;i<health.service_count;++i){const ouse_service_health_t *s=&health.services[i];
        int n=snprintf(buffer+length,sizeof(buffer)-length,"%s  %s  活动 %u\n",s->name,(unsigned)s->status<5 ? names[s->status] : "未知",s->active);
        if(n<0 || (size_t)n>=sizeof(buffer)-length)break;
        length+=(size_t)n;
    }
    if(!length)buffer[0]=0;
    text(services,buffer);
    for(unsigned i=0;i<OUSE_HEALTH_ACTIVITIES;++i){
        if(i>=health.activity_count){lv_obj_add_flag(rows[i],LV_OBJ_FLAG_HIDDEN);continue;}
        activities[i]=health.activities[i];const ouse_activity_t *a=&activities[i];
        snprintf(buffer,sizeof(buffer),"%.47s\n%s%.63s\n%s",a->app_id,a->javascript ? "后台脚本：" : "原生任务：",a->reason,
            a->stopping ? "已请求停止，等待资源释放" : a->javascript ? "停止将结束后台脚本许可" : a->cancellable ? "可请求停止" : "正在安全完成，不能中断");
        text(labels[i],buffer);lv_obj_remove_flag(rows[i],LV_OBJ_FLAG_HIDDEN);
        if(!a->cancellable || a->stopping || health.quiescing)lv_obj_add_state(buttons[i],LV_STATE_DISABLED);else lv_obj_remove_state(buttons[i],LV_STATE_DISABLED);
    }
}
void ouse_health_ui_close(void){summary=services=NULL;memset(rows,0,sizeof(rows));memset(labels,0,sizeof(labels));memset(buttons,0,sizeof(buttons));}
