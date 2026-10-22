// SPDX-License-Identifier: MIT
#include "ouse_native_game.h"
#include "ouse_native.h"
#include "ouse_widgets.h"
#include "ouse_core.h"
#include "esp_heap_caps.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *video,*status,*run_button,*stop_button;
static lv_timer_t *timer;
static lv_image_dsc_t image;
static uint16_t *front;
static unsigned serial;
static int64_t activity;
static uint32_t owned;
static esp_err_t error;
static void refresh(lv_timer_t *unused)
{
    (void)unused;if(!status)return;
    ouse_native_state_t s;ouse_native_get(&s);
    bool ours=owned && owned==s.task_id;
    int64_t latest=ouse_game_input_activity();
    if(ours && s.active && latest && latest!=activity){activity=latest;(void)ouse_core_wake_display();}
    char text[128];
    esp_err_t failure=error!=ESP_OK?error:ours?s.error:ESP_OK;
    if(failure==ESP_ERR_NO_MEM)snprintf(text,sizeof(text),"内存不足，请关闭其他应用后重试");
    else if(failure==ESP_ERR_NOT_FOUND)snprintf(text,sizeof(text),"请先安装游戏程序与试玩版资源");
    else if(failure!=ESP_OK)snprintf(text,sizeof(text),"启动失败：%s",esp_err_to_name(failure));
    else if(ours && s.active)snprintf(text,sizeof(text),"%s · %u 帧",s.stop_requested?"正在停止":"运行中",serial);
    else snprintf(text,sizeof(text),"%s",ours?"已停止":"准备就绪");
    if(strcmp(lv_label_get_text(status),text))lv_label_set_text(status,text);
    if(front && ouse_game_copy_frame(front,&serial)){lv_image_cache_drop(&image);lv_obj_invalidate(video);}
    if(s.active)lv_obj_add_state(run_button,LV_STATE_DISABLED);else lv_obj_remove_state(run_button,LV_STATE_DISABLED);
    if(s.active && ours && !s.stop_requested)lv_obj_remove_state(stop_button,LV_STATE_DISABLED);
    else lv_obj_add_state(stop_button,LV_STATE_DISABLED);
}
static void start(lv_event_t *event)
{(void)event;uint32_t id;error=ouse_native_start_quake2(&id);if(error==ESP_OK)owned=id;refresh(NULL);}
static void stop(lv_event_t *event)
{(void)event;if(owned)(void)ouse_native_stop_task(owned);refresh(NULL);}
static esp_err_t open(lv_obj_t *root,void *context)
{
    (void)context;owned=0;serial=0;activity=0;error=ESP_OK;
    front=heap_caps_calloc(320u*240u,2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!front)return ESP_ERR_NO_MEM;
    lv_obj_set_flex_flow(root,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_all(root,0,0);
    lv_obj_set_style_pad_gap(root,8,0);
    ouse_widget_text(root,"雷神之锤 2",24);
    status=ouse_widget_text(root,"准备就绪",14);lv_obj_set_width(status,LV_PCT(100));
    image=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=320,.h=240,.stride=640},
        .data_size=320u*240u*2u,.data=(const uint8_t *)front};
    video=lv_image_create(root);lv_image_set_src(video,&image);
    lv_obj_t *row=ouse_widget_box(root,OUSE_STYLE_APP,0);lv_obj_set_size(row,LV_PCT(100),48);
    lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW);
    run_button=ouse_widget_button(row,"启动",start,NULL);stop_button=ouse_widget_button(row,"停止",stop,NULL);
    lv_obj_set_flex_grow(run_button,1);lv_obj_set_flex_grow(stop_button,1);
    lv_obj_t *hint=ouse_widget_text(root,"通过电脑串口控制。返回桌面会停止游戏。",14);
    lv_obj_set_width(hint,LV_PCT(100));
    timer=lv_timer_create(refresh,40,NULL);start(NULL);return ESP_OK;
}
static void close(void *context)
{
    (void)context;if(timer)lv_timer_delete(timer);timer=NULL;
    if(owned)(void)ouse_native_stop_task(owned);
    owned=0;if(video)lv_image_set_src(video,NULL);
    lv_image_cache_drop(&image);free(front);front=NULL;
    video=status=run_button=stop_button=NULL;
}
static void pause(void *context)
{(void)context;if(timer)lv_timer_pause(timer);if(owned)(void)ouse_native_stop_task(owned);}
static void resume(void *context){(void)context;if(timer)lv_timer_resume(timer);refresh(NULL);}
const ouse_app_definition_t *ouse_quake2_app(void)
{
    static const ouse_app_definition_t app={.id="ouse.quake2",.name="雷神之锤2",.name_zh="雷神之锤2",.name_en="Quake II",
        .icon=LV_SYMBOL_PLAY,.open=open,.close=close,.pause=pause,.resume=resume};
    return &app;
}
