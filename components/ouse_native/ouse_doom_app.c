// SPDX-License-Identifier: MIT
#include "ouse_native_game.h"
#include "ouse_native.h"
#include "ouse_widgets.h"
#include "ouse_core.h"
#include "esp_heap_caps.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <stdio.h>

#define WIDTH 480u
#define HEIGHT 320u
static lv_obj_t *video,*failure_label;
static lv_timer_t *timer;
static lv_image_dsc_t image;
static uint16_t *front;
static unsigned serial;
static int64_t activity;
static uint32_t owned;
static esp_err_t error;
static bool presenting,saved_landscape,restart;

static void start(void)
{
    uint32_t id;error=ouse_native_start_doom(&id);
    if(error==ESP_OK){owned=id;serial=0;}
    else if(error==ESP_ERR_INVALID_STATE){ouse_native_state_t s;ouse_native_get(&s);if(s.active){restart=true;error=ESP_OK;}}
}
static void restore_display(void)
{
    if(!presenting)return;
    (void)ouse_core_set_fullscreen(false);
    (void)ouse_core_set_orientation(saved_landscape);
    presenting=false;
}
static void refresh(lv_timer_t *unused)
{
    (void)unused;if(!video)return;
    // The app becomes foreground after open/resume; apply its scoped
    // fullscreen setting on the next UI tick.
    if(!presenting){
        if(ouse_core_set_orientation(true)!=ESP_OK)return;
        if(ouse_core_set_fullscreen(true)!=ESP_OK)return;
        presenting=true;
    }
    ouse_native_state_t s;ouse_native_get(&s);
    if(restart && !s.active){restart=false;start();ouse_native_get(&s);}
    bool ours=owned && owned==s.task_id;
    int64_t latest=ouse_game_input_activity();
    if(ours && s.active && latest && latest!=activity){activity=latest;(void)ouse_core_wake_display();}
    if(front && ouse_game_copy_frame(front,WIDTH,HEIGHT,&serial)){
        lv_image_cache_drop(&image);lv_obj_invalidate(video);
    }
    esp_err_t failure=error!=ESP_OK?error:ours?s.error:ESP_OK;
    const char *message=NULL;char text[96];
    if(failure==ESP_ERR_NO_MEM)message="内存不足，请关闭其他应用后重试";
    else if(failure==ESP_ERR_NOT_FOUND)message="请先安装游戏程序与试玩版资源";
    else if(failure!=ESP_OK){snprintf(text,sizeof(text),"启动失败：%s",esp_err_to_name(failure));message=text;}
    else if(ours && !s.active)message="游戏已停止";
    if(message){lv_label_set_text(failure_label,message);lv_obj_remove_flag(failure_label,LV_OBJ_FLAG_HIDDEN);}
    else lv_obj_add_flag(failure_label,LV_OBJ_FLAG_HIDDEN);
}
static esp_err_t open(lv_obj_t *root,void *context)
{
    (void)context;owned=0;serial=0;activity=0;error=ESP_OK;restart=presenting=false;
    saved_landscape=ouse_core_landscape();
    front=heap_caps_calloc(WIDTH*HEIGHT,2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!front)return ESP_ERR_NO_MEM;
    lv_obj_remove_style_all(root);lv_obj_set_style_bg_color(root,lv_color_black(),0);
    lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    image=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=WIDTH,.h=HEIGHT,.stride=WIDTH*2},
        .data_size=WIDTH*HEIGHT*2,.data=(const uint8_t *)front};
    video=lv_image_create(root);lv_image_set_src(video,&image);lv_obj_set_pos(video,0,0);
    lv_obj_set_size(video,LV_PCT(100),LV_PCT(100));lv_image_set_inner_align(video,LV_IMAGE_ALIGN_STRETCH);
    failure_label=ouse_widget_text(root,"",18);lv_obj_set_width(failure_label,LV_PCT(100));
    lv_obj_set_style_text_align(failure_label,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(failure_label);
    lv_obj_add_flag(failure_label,LV_OBJ_FLAG_HIDDEN);
    timer=lv_timer_create(refresh,40,NULL);start();return ESP_OK;
}
static void close(void *context)
{
    (void)context;if(timer)lv_timer_delete(timer);timer=NULL;
    if(owned)(void)ouse_native_stop_task(owned);
    restore_display();owned=0;if(video)lv_image_set_src(video,NULL);
    lv_image_cache_drop(&image);free(front);front=NULL;video=failure_label=NULL;
}
static void pause(void *context)
{
    (void)context;if(timer)lv_timer_pause(timer);
    if(owned)(void)ouse_native_stop_task(owned);
    restore_display();
}
static void resume(void *context)
{
    (void)context;saved_landscape=ouse_core_landscape();restart=true;
    if(timer){lv_timer_resume(timer);lv_timer_ready(timer);}
}
const ouse_app_definition_t *ouse_doom_app(void)
{
    static const ouse_app_definition_t app={.id="ouse.doom",.name="毁灭战士",.name_zh="毁灭战士",.name_en="Doom",
        .icon=LV_SYMBOL_PLAY,.open=open,.close=close,.pause=pause,.resume=resume};
    return &app;
}
