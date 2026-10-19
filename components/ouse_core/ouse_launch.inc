// Native launcher transition. No full-screen snapshots or app-page transforms.
typedef struct {
    lv_obj_t *layer,*surface,*icon,*source;
    lv_timer_t *timer;
    lv_image_dsc_t image;
    lv_area_t origin,target;
    char id[48];
    uint32_t started,reveal_started,duration;
    bool resumed,revealing,expanded;
} launch_animation_t;
static OUSE_PSRAM_BSS launch_animation_t launch_animation;

bool ouse_core_launch_active(void) {return launch_animation.layer!=NULL;}
static void launch_source_deleted(lv_event_t *event)
{(void)event;launch_animation.source=NULL;}
static void launch_animation_end(void)
{
    if(launch_animation.timer){lv_timer_delete(launch_animation.timer);launch_animation.timer=NULL;}
    if(launch_animation.source){
        lv_obj_remove_event_cb(launch_animation.source,launch_source_deleted);
        lv_obj_remove_flag(launch_animation.source,LV_OBJ_FLAG_HIDDEN);
    }
    if(launch_animation.layer)lv_obj_delete(launch_animation.layer);
    if(launch_animation.image.data){lv_image_cache_drop(&launch_animation.image);free((void *)launch_animation.image.data);}
    memset(&launch_animation,0,sizeof(launch_animation));
    ouse_app_stats_t state={0};ouse_app_get_stats(&state);
    if(core.home && core.app_host){
        if(state.active_id[0]){lv_obj_add_flag(core.home,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(core.app_host,LV_OBJ_FLAG_HIDDEN);}
        else {lv_obj_remove_flag(core.home,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(core.app_host,LV_OBJ_FLAG_HIDDEN);}
    }
}
void ouse_core_launch_cancel(void)
{
    if(!ouse_core_launch_active())return;
    (void)ouse_apps_background_fast();
    launch_animation_end();
}
static int launch_mix(int from,int to,unsigned progress)
{return from+(to-from)*(int)progress/1024;}
static void launch_animation_tick(lv_timer_t *timer)
{
    (void)timer;
    ouse_app_stats_t state={0};ouse_power_state_t power={0};ouse_app_get_stats(&state);ouse_power_get(&power);
    if(strcmp(state.active_id,launch_animation.id) || ouse_dialog_active() || power.asleep ||
       ouse_memory_pressure()!=OUSE_MEMORY_NORMAL){launch_animation_end();return;}
    uint32_t elapsed=lv_tick_elaps(launch_animation.started);
    lv_obj_t *root=ouse_apps_root(launch_animation.id);
    if(!root){launch_animation_end();return;}
    if(elapsed>=8000){
        launch_animation_end();
        ouse_dialog_show(ouse_ui_locale() ? "App is taking longer to open. You can return Home and retry." : "应用启动较慢，可返回桌面后重试。",NULL,NULL);
        return;
    }
    if(launch_animation.revealing){
        unsigned fade_ms=launch_animation.resumed ? 48 : 60;
        unsigned fade=lv_tick_elaps(launch_animation.reveal_started)*255/fade_ms;
        if(fade>=255){launch_animation_end();return;}
        lv_obj_set_style_bg_opa(launch_animation.surface,(lv_opa_t)(255-fade),0);
        lv_obj_set_style_opa(launch_animation.icon,(lv_opa_t)(255-fade),0);
        return;
    }
    if(!launch_animation.expanded){
        unsigned press=launch_animation.resumed ? 16 : 24;
        unsigned progress=elapsed<=press ? 0 : (elapsed-press)*1024/(launch_animation.duration-press);
        if(progress>1024)progress=1024;
        unsigned inverse=1024-progress;
        progress=1024-(unsigned)((uint64_t)inverse*inverse*inverse/(1024*1024));
        int original=lv_area_get_width(&launch_animation.origin);
        int shrink=elapsed<press ? original*(int)elapsed/(int)press/16 : original/16;
        int sx=launch_animation.origin.x1+shrink/2,sy=launch_animation.origin.y1+shrink/2;
        int sw=original-shrink,sh=lv_area_get_height(&launch_animation.origin)-shrink;
        int x=launch_mix(sx,launch_animation.target.x1,progress);
        int y=launch_mix(sy,launch_animation.target.y1,progress);
        int width=launch_mix(sw,lv_area_get_width(&launch_animation.target),progress);
        int height=launch_mix(sh,lv_area_get_height(&launch_animation.target),progress);
        lv_obj_set_pos(launch_animation.surface,x,y);lv_obj_set_size(launch_animation.surface,width,height);
        lv_obj_set_style_radius(launch_animation.surface,ouse_ui_dp(12)*(int)(1024-progress)/1024,0);
        int size=launch_mix(sw,ouse_ui_dp(64),progress);
        int cx=(launch_animation.target.x1+launch_animation.target.x2+1)/2;
        int cy=(launch_animation.target.y1+launch_animation.target.y2+1)/2;
        // The icon is a small fixed-size object; scaling its bounded layer never
        // allocates a transformed copy of the application or the whole screen.
        lv_obj_set_pos(launch_animation.icon,launch_mix(sx+sw/2,cx,progress)-ouse_ui_dp(20),
                       launch_mix(sy+sh/2,cy,progress)-ouse_ui_dp(20));
        int scale=size*256/ouse_ui_dp(40);
        lv_obj_set_style_transform_scale_x(launch_animation.icon,scale,0);
        lv_obj_set_style_transform_scale_y(launch_animation.icon,scale,0);
        if(progress==1024)launch_animation.expanded=true;
    }
    if(launch_animation.expanded && !lv_obj_has_flag(root,OUSE_APP_LOADING_FLAG)){
        launch_animation.revealing=true;launch_animation.reveal_started=lv_tick_get();
        lv_obj_add_flag(core.home,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(core.app_host,LV_OBJ_FLAG_HIDDEN);
    }
}
static bool launch_animation_begin(const char *id,lv_obj_t *source)
{
    if(ouse_core_launch_active() || ouse_memory_pressure()!=OUSE_MEMORY_NORMAL)return false;
    ouse_app_info_t info;
    if(ouse_app_find_info(id,&info)!=ESP_OK)return false;
    launch_animation.resumed=ouse_apps_root(id)!=NULL;
    if(info.icon_image){
        const lv_image_dsc_t *image=info.icon_image;
        if(!image->data || image->header.w>96 || image->header.h>96 || image->data_size>96u*96u*4u)return false;
        void *copy=ouse_psram_calloc(1,image->data_size);
        if(!copy)return false;
        memcpy(copy,image->data,image->data_size);launch_animation.image=*image;
        launch_animation.image.reserved=NULL;launch_animation.image.reserved_2=NULL;
        launch_animation.image.data=copy;info.icon_image=&launch_animation.image;
    }
    strcpy(launch_animation.id,id);
    lv_obj_update_layout(core.screen);
    lv_obj_get_coords(source,&launch_animation.origin);lv_obj_get_coords(core.app_host,&launch_animation.target);
    launch_animation.layer=lv_obj_create(core.screen);lv_obj_remove_style_all(launch_animation.layer);
    lv_obj_set_pos(launch_animation.layer,0,0);lv_obj_set_size(launch_animation.layer,LV_PCT(100),LV_PCT(100));
    lv_obj_remove_flag(launch_animation.layer,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_EVENT_BUBBLE|LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(launch_animation.layer,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_PRESS_LOCK);
    launch_animation.surface=lv_obj_create(launch_animation.layer);lv_obj_remove_style_all(launch_animation.surface);
    lv_obj_set_style_bg_color(launch_animation.surface,lv_color_hex(ouse_theme_colors().background),0);
    lv_obj_set_style_bg_opa(launch_animation.surface,LV_OPA_COVER,0);
    lv_obj_remove_flag(launch_animation.surface,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    launch_animation.icon=ouse_widget_app_icon(launch_animation.layer,&info,40);
    lv_obj_set_style_transform_pivot_x(launch_animation.icon,ouse_ui_dp(20),0);
    lv_obj_set_style_transform_pivot_y(launch_animation.icon,ouse_ui_dp(20),0);
    launch_animation.source=source;lv_obj_add_flag(source,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(source,launch_source_deleted,LV_EVENT_DELETE,NULL);
    launch_animation.started=lv_tick_get();launch_animation.duration=launch_animation.resumed ? 120 : 180;
    launch_animation.timer=lv_timer_create(launch_animation_tick,16,NULL);
    if(!launch_animation.timer){launch_animation_end();return false;}
    // Set initial geometry before the first refresh, without waiting for open.
    lv_obj_set_pos(launch_animation.surface,launch_animation.origin.x1,launch_animation.origin.y1);
    lv_obj_set_size(launch_animation.surface,lv_area_get_width(&launch_animation.origin),lv_area_get_height(&launch_animation.origin));
    lv_obj_set_style_radius(launch_animation.surface,ouse_ui_dp(12),0);
    lv_obj_set_pos(launch_animation.icon,launch_animation.origin.x1+lv_area_get_width(&launch_animation.origin)/2-ouse_ui_dp(20),
                   launch_animation.origin.y1+lv_area_get_height(&launch_animation.origin)/2-ouse_ui_dp(20));
    int scale=lv_area_get_width(&launch_animation.origin)*256/ouse_ui_dp(40);
    lv_obj_set_style_transform_scale_x(launch_animation.icon,scale,0);lv_obj_set_style_transform_scale_y(launch_animation.icon,scale,0);
    return true;
}
