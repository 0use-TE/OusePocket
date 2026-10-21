typedef struct {
    bool raw,down,initialized,consumed,long_sent;
    uint32_t changed,pressed;
    char origin[48];
} side_key_t;
static OUSE_PSRAM_BSS side_key_t side_keys[2];
static lv_obj_t *volume_hint,*volume_slider;
static uint32_t volume_until;
bool ouse_core_key_down(unsigned key)
{return core.started && core.port.read_key && key<2 && core.port.read_key(core.port.context,key);}
static void power_confirmed(void)
{if(ouse_control_request(OUSE_CONTROL_SHUTDOWN,NULL,0,0)!=ESP_OK)ouse_dialog_show("关机请求未能提交，请稍后重试。",NULL,NULL);}
static void restart_confirmed(void)
{if(ouse_control_request(OUSE_CONTROL_RESTART,NULL,0,0)!=ESP_OK)ouse_dialog_show("重启请求未能提交，请稍后重试。",NULL,NULL);}
static void power_choice(unsigned choice)
{
    if(choice==0)(void)ouse_core_sleep_display();
    else if(choice==1)ouse_dialog_show("保存数据并结束应用后关机？","关机",power_confirmed);
    else if(choice==2)ouse_dialog_show("保存数据并结束应用后重启？","重启",restart_confirmed);
    else if(choice==3 && ouse_control_request(OUSE_CONTROL_RESUME,NULL,0,0)!=ESP_OK)ouse_dialog_show("恢复请求未能提交，请稍后重试。",NULL,NULL);
}
static void side_power_menu(void)
{
    if(ouse_dialog_active())return;
    ouse_ime_hide();ouse_shell_foreground();
    static const char *choices[]={"息屏","关机","重启","恢复运行"};
    ouse_dialog_select("电源",choices,ouse_tasks_quiescing() ? 4 : 3,0,power_choice);
}
static void side_volume_layout(void)
{
    if(lv_obj_get_height(core.screen)>lv_obj_get_width(core.screen)){
        lv_obj_set_size(volume_hint,ouse_ui_dp(56),ouse_ui_dp(200));
        lv_obj_align(volume_hint,LV_ALIGN_RIGHT_MID,-ouse_ui_dp(16),0);
        lv_obj_set_pos(volume_slider,ouse_ui_dp(16),ouse_ui_dp(20));
        lv_obj_set_size(volume_slider,ouse_ui_dp(24),ouse_ui_dp(160));return;
    }
    int width=lv_obj_get_width(core.screen)-ouse_ui_dp(32);
    if(width>ouse_ui_dp(272))width=ouse_ui_dp(272);
    lv_obj_set_size(volume_hint,width,ouse_ui_dp(56));
    lv_obj_align(volume_hint,LV_ALIGN_TOP_MID,0,ouse_ui_dp(24));
    lv_obj_set_pos(volume_slider,ouse_ui_dp(20),ouse_ui_dp(16));lv_obj_set_size(volume_slider,width-ouse_ui_dp(40),ouse_ui_dp(24));
}
static void side_volume_drag(lv_event_t *event)
{
    (void)event;unsigned volume=(unsigned)lv_slider_get_value(volume_slider);
    if(ouse_audio_set_volume(volume)==ESP_OK && volume)(void)ouse_audio_set_muted(false);
    volume_until=lv_tick_get()+2000;
}
static void side_volume(unsigned key)
{
    if(ouse_tasks_quiescing())return;
    ouse_audio_state_t audio;if(ouse_audio_get(&audio)!=ESP_OK || !audio.available)return;
    unsigned volume=key==0 ? (audio.volume>=90 ? 100 : audio.volume+10) : (audio.volume<=10 ? 0 : audio.volume-10);
    if(ouse_audio_set_volume(volume)!=ESP_OK)return;
    if(!key && volume)(void)ouse_audio_set_muted(false);
    if(!volume_hint){
        volume_hint=lv_obj_create(core.screen);lv_obj_remove_style_all(volume_hint);
        lv_obj_remove_flag(volume_hint,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_GESTURE_BUBBLE);
        volume_slider=lv_slider_create(volume_hint);lv_slider_set_range(volume_slider,0,100);ouse_widget_slider(volume_slider,24);
        lv_obj_add_flag(volume_slider,LV_OBJ_FLAG_PRESS_LOCK);lv_obj_remove_flag(volume_slider,LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(volume_slider,side_volume_drag,LV_EVENT_VALUE_CHANGED,NULL);
    }
    side_volume_layout();
    lv_slider_set_value(volume_slider,volume,LV_ANIM_OFF);
    lv_obj_remove_flag(volume_hint,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(volume_hint);volume_until=lv_tick_get()+2000;
}
static void side_keys_tick(lv_timer_t *timer)
{
    (void)timer;if(!core.port.read_key)return;
    uint32_t now=lv_tick_get();
    if(volume_hint && !lv_obj_has_flag(volume_hint,LV_OBJ_FLAG_HIDDEN)){
        bool dragging=lv_obj_has_state(volume_slider,LV_STATE_PRESSED);
        if(dragging)volume_until=now+2000;
        if((int32_t)(now-volume_until)>=0)lv_obj_add_flag(volume_hint,LV_OBJ_FLAG_HIDDEN);
        else{
            side_volume_layout();ouse_audio_state_t audio;
            if(ouse_audio_get(&audio)==ESP_OK){
                if(!dragging)lv_slider_set_value(volume_slider,audio.volume,LV_ANIM_OFF);
            }
        }
    }
    for(unsigned index=0;index<2;++index){
        side_key_t *key=&side_keys[index];bool down=core.port.read_key(core.port.context,index);
        if(!key->initialized){key->initialized=true;key->raw=key->down=down;key->consumed=down;key->pressed=key->changed=now;continue;}
        if(down!=key->raw){key->raw=down;key->changed=now;}
        if(key->raw!=key->down && (uint32_t)(now-key->changed)>=30){
            key->down=key->raw;
            if(key->down){
                key->pressed=now;key->long_sent=false;key->consumed=false;
                ouse_app_stats_t app;key->origin[0]=0;if(ouse_app_get_stats(&app)==ESP_OK)strcpy(key->origin,app.active_id);
                ouse_power_state_t power;if(ouse_power_get(&power)==ESP_OK && power.asleep){key->consumed=true;(void)ouse_core_wake_display();}
            }else if(!key->consumed && !key->long_sent){
                ouse_app_stats_t app;if(ouse_app_get_stats(&app)!=ESP_OK || strcmp(key->origin,app.active_id))continue;
                ouse_shell_state_t shell;ouse_shell_get_state(&shell);
                bool intercepted=!ouse_core_access_blocked() && !ouse_dialog_active() && !ouse_ime_active() && !shell.quick && !shell.recents && !shell.notifications && ouse_app_key(index);
                if(!intercepted)side_volume(index);
            }
        }
        if(index==1 && key->down && !key->consumed && !key->long_sent && (uint32_t)(now-key->pressed)>=2000){key->long_sent=true;side_power_menu();}
    }
}
