// SPDX-License-Identifier: MIT
#include "ouse_logs_app.h"
#include "ouse_journal.h"
#include "ouse_widgets.h"
#include "ouse_dialog.h"
#include <stdio.h>
#include <string.h>
static lv_obj_t *content,*status,*older,*newer,*export_button;static lv_timer_t *timer;static unsigned revision,page,pages;static bool exporting;
static const char *local(const char *zh,const char *en){return ouse_ui_locale()?en:zh;}
static void text(lv_obj_t *label,const char *value){if(strcmp(lv_label_get_text(label),value))lv_label_set_text(label,value);}
static void refresh(lv_event_t *event){(void)event;ouse_journal_request(0,false);}
static void older_event(lv_event_t *event){(void)event;ouse_journal_request(page+1,false);}
static void newer_event(lv_event_t *event){(void)event;ouse_journal_request(page?page-1:0,false);}
static void export_event(lv_event_t *event){(void)event;if(ouse_journal_export()!=ESP_OK)ouse_dialog_show(local("导出未能开始，请稍后重试","Unable to export; try again"),NULL,NULL);else exporting=true;}
static void clear_confirm(void){ouse_journal_request(0,true);}
static void clear_event(lv_event_t *event){(void)event;ouse_dialog_show(local("清除全部系统日志？","Clear all system logs?"),local("清除","Clear"),clear_confirm);}
static void tick(lv_timer_t *unused){
    (void)unused;static ouse_journal_view_t view;ouse_journal_get(&view);if(!content)return;
    char buffer[64];
    snprintf(buffer,sizeof(buffer),local("%u / %u 页","%u / %u"),view.page+1,view.pages?view.pages:1);
    text(status,view.busy?local("读取中…","Loading…"):buffer);
    text(lv_obj_get_child(export_button,0),view.exporting?local("导出中","Exporting"):local("导出","Export"));
    if(exporting&&!view.exporting)ouse_dialog_show(view.export_error!=ESP_OK?local("导出失败，请检查 SD 卡","Export failed; check SD card"):local("已导出到 SD 卡的 Ouse/Downloads","Saved to SD: Ouse/Downloads"),NULL,NULL);
    exporting=view.exporting;page=view.page;pages=view.pages;
    if(revision!=view.revision){revision=view.revision;lv_label_set_text(content,*view.text?view.text:local("暂无日志","No logs"));lv_obj_scroll_to_y(lv_obj_get_parent(content),0,LV_ANIM_OFF);}
    if(!view.ready&&!view.busy)lv_label_set_text(content,local("日志服务尚未就绪","Log service unavailable"));
    if(view.busy||page+1>=pages)lv_obj_add_state(older,LV_STATE_DISABLED);else lv_obj_remove_state(older,LV_STATE_DISABLED);
    if(view.busy||!page)lv_obj_add_state(newer,LV_STATE_DISABLED);else lv_obj_remove_state(newer,LV_STATE_DISABLED);
}
static esp_err_t open(lv_obj_t *root,void *context){
    (void)context;revision=0;page=0;lv_obj_set_flex_flow(root,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_all(root,ouse_ui_dp(10),0);lv_obj_set_style_pad_gap(root,ouse_ui_dp(6),0);
    ouse_widget_text(root,local("系统日志","System logs"),20);
    lv_obj_t *buttons=ouse_widget_box(root,OUSE_STYLE_APP,0);lv_obj_set_size(buttons,LV_PCT(100),LV_SIZE_CONTENT);lv_obj_set_flex_flow(buttons,LV_FLEX_FLOW_ROW);lv_obj_set_style_pad_gap(buttons,ouse_ui_dp(8),0);
    lv_obj_t *refresh_button=ouse_widget_button(buttons,local("刷新","Refresh"),refresh,NULL);
    export_button=ouse_widget_button(buttons,local("导出","Export"),export_event,NULL);
    lv_obj_t *clear_button=ouse_widget_button(buttons,local("清除","Clear"),clear_event,NULL);
    lv_obj_t *actions[]={refresh_button,export_button,clear_button};
    for(unsigned i=0;i<3;++i){lv_obj_set_width(actions[i],0);lv_obj_set_flex_grow(actions[i],1);lv_obj_set_height(actions[i],ouse_ui_dp(44));lv_obj_set_style_pad_hor(actions[i],ouse_ui_dp(6),0);}
    lv_obj_t *scroll=ouse_widget_box(root,OUSE_STYLE_PANEL,8);lv_obj_set_width(scroll,LV_PCT(100));lv_obj_set_height(scroll,0);lv_obj_set_flex_grow(scroll,1);lv_obj_set_style_pad_all(scroll,ouse_ui_dp(10),0);lv_obj_add_flag(scroll,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(scroll,LV_DIR_VER);
    content=ouse_widget_text(scroll,local("读取中","Loading"),16);lv_obj_set_width(content,LV_PCT(100));lv_obj_set_style_text_line_space(content,ouse_ui_dp(5),0);
    lv_obj_t *pager=ouse_widget_box(root,OUSE_STYLE_APP,0);lv_obj_set_size(pager,LV_PCT(100),LV_SIZE_CONTENT);lv_obj_set_flex_flow(pager,LV_FLEX_FLOW_ROW);lv_obj_set_flex_align(pager,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);lv_obj_set_style_pad_gap(pager,ouse_ui_dp(8),0);
    newer=ouse_widget_button(pager,local("较新","Newer"),newer_event,NULL);
    status=ouse_widget_text(pager,"",16);lv_obj_set_width(status,0);lv_obj_set_flex_grow(status,1);lv_obj_set_style_text_align(status,LV_TEXT_ALIGN_CENTER,0);
    older=ouse_widget_button(pager,local("较早","Older"),older_event,NULL);
    exporting=false;
    ouse_journal_request(0,false);timer=lv_timer_create(tick,300,NULL);tick(NULL);return ESP_OK;
}
static void close(void *context){(void)context;if(timer)lv_timer_delete(timer);timer=NULL;content=status=older=newer=export_button=NULL;}
static void pause(void *context){(void)context;if(timer)lv_timer_pause(timer);}
static void resume(void *context){(void)context;if(timer)lv_timer_resume(timer);ouse_journal_request(0,false);}
const ouse_app_definition_t *ouse_logs_app(void){static const ouse_app_definition_t app={.id="ouse.logs",.name="系统日志",.name_zh="系统日志",.name_en="System logs",.icon=LV_SYMBOL_LIST,.open=open,.close=close,.pause=pause,.resume=resume};return &app;}
