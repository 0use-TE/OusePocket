// SPDX-License-Identifier: MIT
#include "ouse_native_app.h"
#include "ouse_native.h"
#include "ouse_widgets.h"
#include "ouse_tasks.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *status_label,*result_label,*details_label,*run_button,*stop_button;
static lv_timer_t *timer;
static uint32_t owned_task;
static esp_err_t request_error;
static const char *local(const char *zh,const char *en){return ouse_ui_locale()?en:zh;}
static void text(lv_obj_t *label,const char *value)
{if(strcmp(lv_label_get_text(label),value))lv_label_set_text(label,value);}
static void enabled(lv_obj_t *button,bool value)
{if(value)lv_obj_remove_state(button,LV_STATE_DISABLED);else lv_obj_add_state(button,LV_STATE_DISABLED);}
static const char *failure(esp_err_t error)
{
    if(error==ESP_ERR_NO_MEM)return local("内存不足，请关闭其他应用后重试","Not enough memory. Close other apps and retry.");
    if(error==ESP_ERR_INVALID_ARG || error==ESP_ERR_NOT_SUPPORTED)
        return local("程序不兼容，无法运行","This program is incompatible.");
    if(error==ESP_ERR_NOT_FOUND)return local("找不到已保存的程序","The saved program was not found.");
    if(error==ESP_ERR_INVALID_STATE)return local("暂时无法启动，请稍后重试","Unable to start yet. Please retry shortly.");
    return local("运行失败，请重新尝试","The program failed. Please try again.");
}
static void update(lv_timer_t *unused)
{
    (void)unused;if(!status_label)return;
    ouse_native_state_t s;ouse_native_get(&s);
    bool ours=owned_task && s.task_id==owned_task;
    const char *status=local("准备就绪","Ready");char result[80],details[128];
    if(s.active && s.installing)status=local("正在保存程序…","Saving program…");
    else if(s.active && !ours)status=local("另一个程序正在运行","Another program is running");
    else if(request_error!=ESP_OK)status=failure(request_error);
    else if(ours){
        if(s.active)status=s.stop_requested?local("正在停止…","Stopping…"):local("正在运行","Running");
        else if(!strcmp(s.phase,"cancelled"))status=local("已停止","Stopped");
        else if(s.error!=ESP_OK)status=failure(s.error);
        else status=local("运行完成","Finished");
    }
    text(status_label,status);
    if(ours && s.reported)snprintf(result,sizeof(result),"%ld",(long)s.result);
    else snprintf(result,sizeof(result),"%s",local("等待输出","Waiting for output"));
    text(result_label,result);
    if(ours){
        double elapsed=(s.finished_us-s.started_us)/1000000.0;
        if(!s.active)snprintf(details,sizeof(details),local("用时 %.1f 秒 · 退出代码 %ld","%.1f seconds · Exit code %ld"),elapsed,(long)s.exit_code);
        else snprintf(details,sizeof(details),"%s",local("程序正在设备上执行","The program is running on this device"));
    }else snprintf(details,sizeof(details),"%s",local("点击运行，启动本机程序","Tap Run to start the saved program"));
    text(details_label,details);
    enabled(run_button,!s.active && !ouse_tasks_quiescing());
    enabled(stop_button,s.active && ours && !s.installing && !s.stop_requested);
}
static void start(lv_event_t *event)
{
    (void)event;uint32_t id;
    request_error=ouse_native_start_desktop(&id);
    if(request_error==ESP_OK)owned_task=id;
    update(NULL);
}
static void stop(lv_event_t *event)
{(void)event;if(owned_task)(void)ouse_native_stop_task(owned_task);update(NULL);}
static esp_err_t open(lv_obj_t *root,void *context)
{
    (void)context;owned_task=0;request_error=ESP_OK;
    lv_obj_set_flex_flow(root,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root,ouse_ui_dp(16),0);
    lv_obj_set_style_pad_gap(root,ouse_ui_dp(12),0);
    ouse_widget_text(root,local("原生程序","Native program"),24);
    status_label=ouse_widget_text(root,local("准备就绪","Ready"),18);
    lv_obj_set_width(status_label,LV_PCT(100));
    lv_obj_t *panel=ouse_widget_box(root,OUSE_STYLE_PANEL,12);
    lv_obj_set_size(panel,LV_PCT(100),LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(panel,ouse_ui_dp(18),0);
    lv_obj_set_style_pad_gap(panel,ouse_ui_dp(12),0);
    lv_obj_set_flex_flow(panel,LV_FLEX_FLOW_COLUMN);
    ouse_widget_text(panel,local("程序输出","Program output"),16);
    result_label=ouse_widget_text(panel,local("等待输出","Waiting for output"),32);
    lv_obj_set_width(result_label,LV_PCT(100));
    details_label=ouse_widget_text(root,"",14);lv_obj_set_width(details_label,LV_PCT(100));
    lv_obj_t *buttons=ouse_widget_box(root,OUSE_STYLE_APP,0);
    lv_obj_set_size(buttons,LV_PCT(100),LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(buttons,LV_FLEX_FLOW_ROW);lv_obj_set_style_pad_gap(buttons,ouse_ui_dp(12),0);
    run_button=ouse_widget_button(buttons,local("运行","Run"),start,NULL);
    stop_button=ouse_widget_button(buttons,local("停止","Stop"),stop,NULL);
    lv_obj_t *actions[]={run_button,stop_button};
    for(unsigned i=0;i<2;++i){lv_obj_set_width(actions[i],0);lv_obj_set_flex_grow(actions[i],1);lv_obj_set_height(actions[i],ouse_ui_dp(48));}
    lv_obj_t *hint=ouse_widget_text(root,local("返回桌面时会停止程序。未安装其他程序时，运行自带的计数示例。",
        "Returning home stops the program. The counter demo runs until you install another program."),14);
    lv_obj_set_width(hint,LV_PCT(100));
    timer=lv_timer_create(update,100,NULL);
    start(NULL);return ESP_OK;
}
static void close(void *context)
{
    (void)context;if(timer)lv_timer_delete(timer);timer=NULL;
    if(owned_task)(void)ouse_native_stop_task(owned_task);
    owned_task=0;status_label=result_label=details_label=run_button=stop_button=NULL;
}
static void pause(void *context)
{(void)context;if(timer)lv_timer_pause(timer);if(owned_task)(void)ouse_native_stop_task(owned_task);}
static void resume(void *context){(void)context;if(timer)lv_timer_resume(timer);update(NULL);}
const ouse_app_definition_t *ouse_native_app(void)
{
    static const ouse_app_definition_t app={.id="ouse.native",.name="原生程序",.name_zh="原生程序",.name_en="Native program",
        .icon=LV_SYMBOL_PLAY,.open=open,.close=close,.pause=pause,.resume=resume};
    return &app;
}
