#include "ouse_app_manager_ui.h"
#include "ouse_app_manager.h"
#include "ouse_widgets.h"
#include "ouse_dialog.h"
#include "ouse_memory.h"
#include "ouse_permissions.h"
#include <stdio.h>
#include <string.h>
static lv_obj_t *page,*list,*detail,*rows,*heading,*message,*retry,*open_button,*clear_button,*remove_button;
static lv_obj_t *identity,*package_size,*data_size,*total_size,*alarm_count;
static OUSE_PSRAM_BSS ouse_manager_state_t snapshot;
static char selected[48],confirmed[48],return_app[48];
static ouse_manager_action_t confirmed_action;
static uint32_t catalog,completed,own_request;
static bool confirming;
static lv_obj_t *permission_rows[OUSE_PERMISSION_COUNT];
static char permission_target[48];
static ouse_permission_t permission_selected;
static unsigned language;
static void (*leave)(void);
static const char *local(const char *zh,const char *en){return ouse_ui_locale()==1 ? en : zh;}
static void hidden(lv_obj_t *object,bool value){if(value)lv_obj_add_flag(object,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(object,LV_OBJ_FLAG_HIDDEN);}
static void disabled(lv_obj_t *object,bool value)
{
    lv_obj_set_style_bg_color(object,lv_color_hex(ouse_theme_colors().disabled),LV_STATE_DISABLED);
    if(lv_obj_get_child_count(object))lv_obj_set_style_opa(lv_obj_get_child(object,0),value ? LV_OPA_50 : LV_OPA_COVER,0);
    if(value)lv_obj_add_state(object,LV_STATE_DISABLED);else lv_obj_remove_state(object,LV_STATE_DISABLED);
}
static const char *app_name(const ouse_managed_app_t *app)
{const char *name=ouse_ui_locale()==1 ? app->name_en : app->name_zh;return *name ? name : app->name;}
static void format_size(size_t bytes,char out[40])
{if(bytes<1024)snprintf(out,40,"%u B",(unsigned)bytes);else if(bytes<1024*1024)snprintf(out,40,"%.1f KiB",bytes/1024.0);else snprintf(out,40,"%.2f MiB",bytes/(1024.0*1024));}
static lv_obj_t *column(lv_obj_t *parent)
{
    lv_obj_t *object=ouse_widget_box(parent,OUSE_STYLE_APP,0);lv_obj_set_width(object,LV_PCT(100));lv_obj_set_height(object,LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(object,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(object,ouse_ui_dp(8),0);return object;
}
static const ouse_managed_app_t *find_selected(void)
{for(unsigned index=0;index<snapshot.count;++index)if(!strcmp(snapshot.apps[index].id,selected))return &snapshot.apps[index];return NULL;}
static void submit(ouse_manager_action_t action,const char *id)
{
    esp_err_t error=ouse_manager_request(action,id,&own_request);
    if(error!=ESP_OK)ouse_dialog_show(local("应用服务繁忙，请稍后重试","Application service busy. Try again."),NULL,NULL);
    ouse_manager_ui_update();
}
void ouse_manager_ui_refresh(void)
{
    if(!page)return;
    ouse_manager_get(&snapshot);if(!snapshot.busy && !snapshot.recovery_pending && snapshot.ready)submit(OUSE_MANAGER_REFRESH,NULL);
}
static void retry_event(lv_event_t *event){(void)event;submit(OUSE_MANAGER_RETRY,NULL);}
static void permission_changed(unsigned choice)
{
    if(choice>OUSE_PERMISSION_DENY)return;
    if(ouse_permissions_set(permission_target,permission_selected,choice)!=ESP_OK)ouse_dialog_show("设置未能保存，请重试",NULL,NULL);
    ouse_manager_ui_update();
}
static void permission_event(lv_event_t *event)
{
    if(!selected[0] || snapshot.busy)return;
    permission_selected=(ouse_permission_t)(uintptr_t)lv_event_get_user_data(event);
    strcpy(permission_target,selected);
    static const char *choices[]={"询问","允许","拒绝"};
    static const char *names[]={"麦克风","摄像头","删除共享文件","蓝牙","键盘与鼠标控制"};
    ouse_dialog_select(names[permission_selected],choices,3,ouse_permissions_get(selected,permission_selected),permission_changed);
}
static void confirmed_event(void){confirming=false;submit(confirmed_action,confirmed);}
static void destructive(lv_event_t *event)
{
    ouse_manager_get(&snapshot);const ouse_managed_app_t *app=find_selected();if(!app || app->system || snapshot.busy)return;
    confirmed_action=(ouse_manager_action_t)(uintptr_t)lv_event_get_user_data(event);strcpy(confirmed,selected);confirming=true;
    char body[360];snprintf(body,sizeof(body),"%s\n\n%s",app_name(app),confirmed_action==OUSE_MANAGER_CLEAR ?
        local("清除本地数据和闹钟，保留应用及 SD 卡文件？","Clear local data and alarms? Keep the app and SD files.") :
        local("卸载应用并删除本地数据和闹钟？SD 卡文件会保留。","Uninstall this app and its local data and alarms? SD files stay."));
    ouse_dialog_show(body,confirmed_action==OUSE_MANAGER_CLEAR ? "清除数据" : "卸载",confirmed_event);
}
static void open_event(lv_event_t *event)
{(void)event;if(!snapshot.busy && selected[0] && strcmp(selected,"ouse.settings"))if(ouse_app_open(selected)!=ESP_OK)ouse_dialog_show("应用打开失败",NULL,NULL);}
static void select_event(lv_event_t *event)
{
    unsigned index=(unsigned)(uintptr_t)lv_event_get_user_data(event);if(index>=snapshot.count)return;
    strcpy(selected,snapshot.apps[index].id);hidden(list,true);hidden(detail,false);lv_obj_scroll_to_y(page,0,LV_ANIM_OFF);ouse_manager_ui_update();
}
bool ouse_manager_ui_back(void)
{
    if(!page || !selected[0])return false;
    if(return_app[0]){char id[48];strcpy(id,return_app);return_app[0]=0;if(ouse_app_open(id)==ESP_OK)return true;}
    selected[0]=0;hidden(detail,true);hidden(list,false);ouse_ui_text(heading,"应用管理");return true;
}
void ouse_manager_ui_select(const char *id)
{
    if(!page || !id || strlen(id)>=sizeof(selected))return;
    snprintf(selected,sizeof(selected),"%s",id);snprintf(return_app,sizeof(return_app),"%s",id);
    hidden(list,true);hidden(detail,false);ouse_manager_ui_update();
}
static void back_event(lv_event_t *event){(void)event;if(!ouse_manager_ui_back() && leave)leave();}
void ouse_manager_ui_release_icons(void)
{if(rows){lv_obj_clean(rows);catalog=UINT32_MAX;}}
void ouse_manager_ui_build(lv_obj_t *root,void (*back)(void))
{
    page=root;leave=back;selected[0]=return_app[0]=0;confirming=false;own_request=0;catalog=UINT32_MAX;language=ouse_ui_locale();
    lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    ouse_manager_get(&snapshot);completed=snapshot.completed_id;
    lv_obj_t *header=ouse_widget_box(root,OUSE_STYLE_APP,0);lv_obj_set_width(header,LV_PCT(100));lv_obj_set_height(header,LV_SIZE_CONTENT);lv_obj_set_flex_flow(header,LV_FLEX_FLOW_ROW);lv_obj_set_style_pad_column(header,ouse_ui_dp(8),0);
    ouse_widget_button(header,"返回",back_event,NULL);heading=ouse_widget_text(header,"应用管理",22);lv_obj_set_width(heading,0);lv_obj_set_flex_grow(heading,1);
    message=ouse_widget_text(root,"",14);lv_obj_set_width(message,LV_PCT(100));retry=ouse_widget_button(root,"重试",retry_event,NULL);
    list=column(root);lv_obj_set_height(list,0);lv_obj_set_flex_grow(list,1);
    lv_obj_add_flag(list,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);lv_obj_set_scroll_dir(list,LV_DIR_VER);rows=column(list);
    detail=column(root);lv_obj_set_height(detail,0);lv_obj_set_flex_grow(detail,1);
    lv_obj_t *information=column(detail);lv_obj_set_height(information,0);lv_obj_set_flex_grow(information,1);
    lv_obj_add_flag(information,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);lv_obj_set_scroll_dir(information,LV_DIR_VER);
    identity=ouse_widget_text(information,"",14);lv_obj_set_width(identity,LV_PCT(100));
    package_size=ouse_widget_text(information,"",18);data_size=ouse_widget_text(information,"",18);total_size=ouse_widget_text(information,"",18);alarm_count=ouse_widget_text(information,"",14);
    for(unsigned index=0;index<OUSE_PERMISSION_COUNT;++index){permission_rows[index]=ouse_widget_button(information,"",permission_event,(void *)(uintptr_t)index);ouse_widget_button_expand(permission_rows[index]);}
    lv_obj_t *actions=column(detail);lv_obj_set_flex_flow(actions,LV_FLEX_FLOW_ROW);lv_obj_set_style_pad_column(actions,ouse_ui_dp(6),0);
    open_button=ouse_widget_button(actions,"打开",open_event,NULL);
    clear_button=ouse_widget_button(actions,"清除数据",destructive,(void *)(uintptr_t)OUSE_MANAGER_CLEAR);
    remove_button=ouse_widget_button(actions,"卸载",destructive,(void *)(uintptr_t)OUSE_MANAGER_UNINSTALL);
    lv_obj_t *buttons[]={open_button,clear_button,remove_button};
    for(unsigned i=0;i<3;i++){lv_obj_set_width(buttons[i],0);lv_obj_set_flex_grow(buttons[i],1);lv_obj_set_style_pad_hor(buttons[i],ouse_ui_dp(6),0);}
    hidden(detail,true);ouse_manager_ui_update();ouse_manager_ui_refresh();
}
void ouse_manager_ui_update(void)
{
    if(!page)return;
    ouse_manager_get(&snapshot);if(confirming && !ouse_dialog_active())confirming=false;
    if(own_request && snapshot.completed_id==own_request && completed!=snapshot.completed_id && snapshot.error!=ESP_OK && !ouse_dialog_active())ouse_dialog_show(local("操作未完成，请重试","Operation incomplete. Please retry."),NULL,NULL);
    completed=snapshot.completed_id;
    const char *status=!snapshot.ready ? local("应用存储不可用","Application storage unavailable") : snapshot.busy && snapshot.action!=OUSE_MANAGER_REFRESH ? local("正在处理…","Working…") : snapshot.recovery_pending ? local("有操作尚未完成，请重试","Recovery required. Please retry.") : snapshot.error!=ESP_OK ? local("操作失败，请刷新或重试","Operation failed. Refresh or retry.") : "";
    if(strcmp(lv_label_get_text(message),status))lv_label_set_text(message,status);
    hidden(message,!*status);hidden(retry,!snapshot.recovery_pending);disabled(retry,snapshot.busy);
    char size[40],value[160];
    if(catalog!=snapshot.catalog_revision || language!=ouse_ui_locale()){
        lv_obj_clean(rows);catalog=snapshot.catalog_revision;language=ouse_ui_locale();
        for(unsigned index=0;index<snapshot.count;++index){const ouse_managed_app_t *app=&snapshot.apps[index];
            lv_obj_t *row=ouse_widget_row(rows,NULL,app_name(app),NULL,select_event,(void *)(uintptr_t)index,NULL);
            ouse_app_info_t info;if(ouse_app_find_info(app->id,&info)==ESP_OK){lv_obj_t *icon=ouse_widget_app_icon(row,&info,32);lv_obj_move_to_index(icon,0);}
        }
    }
    if(!selected[0])return;
    const ouse_managed_app_t *app=find_selected();if(!app){if(snapshot.busy || !snapshot.ready)return;(void)ouse_manager_ui_back();return;}
    lv_label_set_text(heading,app_name(app));
    const char *permission_names[]={"麦克风","摄像头","删除共享文件","蓝牙","键盘与鼠标控制"},*permission_states[]={"询问","允许","拒绝"};
    for(unsigned index=0;index<OUSE_PERMISSION_COUNT;++index){
        hidden(permission_rows[index],app->system || !(app->permissions&(1u<<index)));
        char caption[96];snprintf(caption,sizeof(caption),"%s：%s",permission_names[index],permission_states[ouse_permissions_get(app->id,index)]);
        lv_label_set_text(lv_obj_get_child(permission_rows[index],0),caption);disabled(permission_rows[index],snapshot.busy);
    }
    lv_label_set_text(identity,app->id);
    format_size(app->package_bytes,size);snprintf(value,sizeof(value),local("应用大小：%s","App size: %s"),app->system ? local("包含在系统固件中","Included in firmware") : app->measured ? size : "--");lv_label_set_text(package_size,value);
    format_size(app->data_bytes,size);snprintf(value,sizeof(value),local("本地数据：%s","Local data: %s"),app->system ? "--" : app->measured ? size : "--");lv_label_set_text(data_size,value);
    format_size(app->package_bytes+app->data_bytes,size);snprintf(value,sizeof(value),local("合计：%s","Total: %s"),app->system ? "--" : app->measured ? size : "--");lv_label_set_text(total_size,value);
    snprintf(value,sizeof(value),local("原生闹钟：%u","Native alarms: %u"),app->alarms);lv_label_set_text(alarm_count,value);hidden(alarm_count,!app->alarms);
    disabled(open_button,snapshot.busy || !strcmp(app->id,"ouse.settings"));disabled(clear_button,snapshot.busy || snapshot.recovery_pending || app->system);disabled(remove_button,snapshot.busy || snapshot.recovery_pending || app->system);
}
void ouse_manager_ui_close(void)
{
    if(confirming)ouse_dialog_dismiss();
    page=list=detail=rows=heading=message=retry=open_button=clear_button=remove_button=NULL;
    selected[0]=return_app[0]=0;confirming=false;
    ouse_manager_release(&snapshot);
}
