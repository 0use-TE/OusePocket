typedef struct {
    lv_obj_t *root,*mark,*name,*dots[3];
    lv_timer_t *timer;
    uint32_t started,changed;
    size_t count;
} boot_animation_t;
static OUSE_PSRAM_BSS boot_animation_t boot_animation;
static void boot_animation_end(void)
{
    if(boot_animation.timer){lv_timer_delete(boot_animation.timer);boot_animation.timer=NULL;}
    if(boot_animation.root){lv_obj_delete(boot_animation.root);boot_animation.root=NULL;}
    boot_animation.mark=boot_animation.name=NULL;
    memset(boot_animation.dots,0,sizeof(boot_animation.dots));
}
static void boot_animation_tick(lv_timer_t *timer)
{
    (void)timer;uint32_t now=lv_tick_get(),elapsed=now-boot_animation.started;
    if(boot_animation.count!=core.app_count){boot_animation.count=core.app_count;boot_animation.changed=now;}
    ouse_app_stats_t app;ouse_power_state_t power;
    bool interrupted=ouse_dialog_active() || (ouse_app_get_stats(&app)==ESP_OK && app.active_id[0]) || (ouse_power_get(&power)==ESP_OK && power.asleep);
    bool ready=launcher.restored && launcher.loaded && now-boot_animation.changed>=180;
    if(interrupted || elapsed>=8000 || (ready && elapsed>=1200)){boot_animation_end();return;}
    unsigned entrance=elapsed>400 ? 400 : elapsed;
    lv_obj_align(boot_animation.mark,LV_ALIGN_CENTER,0,ouse_ui_dp(-52+(int)(400-entrance)/50));
    lv_obj_set_style_text_opa(boot_animation.name,(lv_opa_t)(64+entrance*191/400),0);
    for(unsigned index=0;index<3;++index){
        unsigned phase=(elapsed+1200-index*240)%1200;
        unsigned pulse=phase<600 ? phase : 1200-phase;
        lv_obj_set_style_bg_opa(boot_animation.dots[index],(lv_opa_t)(50+pulse*205/600),0);
    }
}
static void boot_animation_start(void)
{
    if(ouse_memory_pressure()!=OUSE_MEMORY_NORMAL)return;
    boot_animation.root=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(boot_animation.root);
    lv_obj_set_size(boot_animation.root,LV_PCT(100),LV_PCT(100));lv_obj_set_pos(boot_animation.root,0,0);
    lv_obj_set_style_bg_color(boot_animation.root,lv_color_hex(0x10151F),0);lv_obj_set_style_bg_opa(boot_animation.root,LV_OPA_COVER,0);
    lv_obj_remove_flag(boot_animation.root,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_EVENT_BUBBLE|LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(boot_animation.root,LV_OBJ_FLAG_CLICKABLE);
    boot_animation.mark=lv_obj_create(boot_animation.root);lv_obj_remove_style_all(boot_animation.mark);
    lv_obj_set_size(boot_animation.mark,ouse_ui_dp(68),ouse_ui_dp(76));
    lv_obj_set_style_radius(boot_animation.mark,ouse_ui_dp(20),0);lv_obj_set_style_border_width(boot_animation.mark,ouse_ui_dp(3),0);
    lv_obj_set_style_border_color(boot_animation.mark,lv_color_hex(0x5795FF),0);
    lv_obj_remove_flag(boot_animation.mark,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *seam=lv_obj_create(boot_animation.mark);lv_obj_remove_style_all(seam);
    lv_obj_set_size(seam,ouse_ui_dp(36),ouse_ui_dp(3));lv_obj_set_style_radius(seam,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(seam,lv_color_hex(0x5795FF),0);lv_obj_set_style_bg_opa(seam,LV_OPA_COVER,0);
    lv_obj_align(seam,LV_ALIGN_CENTER,0,ouse_ui_dp(-10));lv_obj_remove_flag(seam,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    boot_animation.name=ouse_widget_text(boot_animation.root,"Ouse",40);
    lv_obj_set_style_text_color(boot_animation.name,lv_color_hex(0xF5F7FF),0);lv_obj_align(boot_animation.name,LV_ALIGN_CENTER,0,ouse_ui_dp(34));
    for(unsigned index=0;index<3;++index){
        lv_obj_t *dot=lv_obj_create(boot_animation.root);lv_obj_remove_style_all(dot);boot_animation.dots[index]=dot;
        lv_obj_set_size(dot,ouse_ui_dp(6),ouse_ui_dp(6));lv_obj_set_style_radius(dot,LV_RADIUS_CIRCLE,0);
        lv_obj_set_style_bg_color(dot,lv_color_hex(0x5795FF),0);lv_obj_align(dot,LV_ALIGN_CENTER,ouse_ui_dp((int)index*16-16),ouse_ui_dp(92));
        lv_obj_remove_flag(dot,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    }
    boot_animation.started=boot_animation.changed=lv_tick_get();boot_animation.count=core.app_count;
    boot_animation.timer=lv_timer_create(boot_animation_tick,40,NULL);
    if(!boot_animation.timer){boot_animation_end();return;}
    boot_animation_tick(NULL);
}
