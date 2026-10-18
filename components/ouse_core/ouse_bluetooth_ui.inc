static struct {
    lv_obj_t *toggle,*status,*address,*peers,*scan,*discover,*disconnect;
    uint32_t revision,prompt_id;
    unsigned count;
    ouse_ble_peer_t items[OUSE_BLE_PEERS_MAX],selected;
} bluetooth_ui;
static void bluetooth_result(esp_err_t error)
{if(error!=ESP_OK)ouse_dialog_show(ouse_ui_locale() ? "Bluetooth request failed." : "蓝牙操作失败，请重试。",NULL,NULL);}
static void bluetooth_toggle(lv_event_t *event)
{bluetooth_result(ouse_ble_enable(lv_obj_has_state(lv_event_get_target_obj(event),LV_STATE_CHECKED)));}
static void bluetooth_scan(lv_event_t *event){(void)event;bluetooth_result(ouse_ble_scan());}
static void bluetooth_discover(lv_event_t *event){(void)event;bluetooth_result(ouse_ble_discoverable());}
static void bluetooth_disconnect(lv_event_t *event){(void)event;bluetooth_result(ouse_ble_disconnect());}
static void bluetooth_peer_choice(unsigned choice)
{
    if(choice==0)bluetooth_result(ouse_ble_connect(bluetooth_ui.selected.address,bluetooth_ui.selected.type));
    else bluetooth_result(ouse_ble_forget(bluetooth_ui.selected.address,bluetooth_ui.selected.type));
}
static void bluetooth_peer(lv_event_t *event)
{
    unsigned index=(unsigned)(uintptr_t)lv_event_get_user_data(event);if(index>=bluetooth_ui.count)return;
    bluetooth_ui.selected=bluetooth_ui.items[index];
    if(bluetooth_ui.selected.bonded){const char *choices[]={"连接","忘记设备"};ouse_dialog_select(bluetooth_ui.selected.name[0] ? bluetooth_ui.selected.name : bluetooth_ui.selected.address,choices,2,0,bluetooth_peer_choice);}
    else bluetooth_result(ouse_ble_connect(bluetooth_ui.selected.address,bluetooth_ui.selected.type));
}
static void bluetooth_pair_choice(unsigned choice)
{uint32_t id=bluetooth_ui.prompt_id;bluetooth_ui.prompt_id=0;bluetooth_result(ouse_ble_confirm(id,choice==0));}
static void bluetooth_pair_prompt(void)
{
    ouse_ble_state_t state;ouse_ble_get(&state);
    if(bluetooth_ui.prompt_id){
        if(!state.pair_pending || state.pair_id!=bluetooth_ui.prompt_id){if(ouse_dialog_choice_active())ouse_dialog_dismiss();bluetooth_ui.prompt_id=0;}
        else if(!ouse_dialog_active()){(void)ouse_ble_confirm(bluetooth_ui.prompt_id,false);bluetooth_ui.prompt_id=0;}
        return;
    }
    if(!state.pair_pending || ouse_dialog_active())return;
    char title[96];snprintf(title,sizeof(title),"%s\n%06lu",ouse_ui_locale() ? "Bluetooth pairing" : "蓝牙配对",(unsigned long)state.number);
    const char *choices[]={"配对","拒绝"};bluetooth_ui.prompt_id=state.pair_id;
    ouse_dialog_select(title,choices,2,1,bluetooth_pair_choice);
}
static void bluetooth_build(lv_obj_t *page)
{
    lv_obj_t *row=ouse_widget_row(page,LV_SYMBOL_BLUETOOTH,"蓝牙",NULL,NULL,NULL,NULL);lv_obj_delete(lv_obj_get_child(row,-1));
    bluetooth_ui.toggle=ouse_widget_switch(row);lv_obj_add_event_cb(bluetooth_ui.toggle,bluetooth_toggle,LV_EVENT_VALUE_CHANGED,NULL);
    bluetooth_ui.status=ouse_widget_text(page,"",18);lv_obj_set_width(bluetooth_ui.status,LV_PCT(100));
    bluetooth_ui.address=ouse_widget_text(page,"OusePocket",18);lv_obj_set_width(bluetooth_ui.address,LV_PCT(100));
    row=ouse_widget_box(page,OUSE_STYLE_PANEL,0);lv_obj_set_size(row,LV_PCT(100),LV_SIZE_CONTENT);lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW);lv_obj_set_style_pad_column(row,ouse_ui_dp(8),0);
    bluetooth_ui.scan=ouse_widget_button(row,"搜索设备",bluetooth_scan,NULL);
    bluetooth_ui.discover=ouse_widget_button(row,"可被发现",bluetooth_discover,NULL);
    bluetooth_ui.disconnect=ouse_widget_button(page,"断开连接",bluetooth_disconnect,NULL);
    bluetooth_ui.peers=ouse_widget_box(page,OUSE_STYLE_PANEL,0);lv_obj_set_size(bluetooth_ui.peers,LV_PCT(100),LV_SIZE_CONTENT);lv_obj_set_flex_flow(bluetooth_ui.peers,LV_FLEX_FLOW_COLUMN);
    bluetooth_ui.revision=UINT32_MAX;bluetooth_refresh();
}
static void bluetooth_refresh(void)
{
    if(!bluetooth_ui.toggle)return;
    ouse_ble_state_t state;ouse_ble_get(&state);if(state.revision==bluetooth_ui.revision)return;
    bluetooth_ui.revision=state.revision;
    if(state.enabled)lv_obj_add_state(bluetooth_ui.toggle,LV_STATE_CHECKED);else lv_obj_remove_state(bluetooth_ui.toggle,LV_STATE_CHECKED);
    if(!state.available)lv_obj_add_state(bluetooth_ui.toggle,LV_STATE_DISABLED);else lv_obj_remove_state(bluetooth_ui.toggle,LV_STATE_DISABLED);
    const char *status=state.starting ? "正在启动" : !state.available ? "蓝牙不可用" : !state.enabled ? "已关闭" : state.error ? "操作失败，请重试" : state.connected ? (state.bonded ? "已配对" : "已连接") : state.scanning ? "正在搜索" : state.advertising ? "可被发现" : "已开启";
    ouse_ui_text(bluetooth_ui.status,status);
    lv_label_set_text(bluetooth_ui.address,state.connected ? state.peer : "OusePocket");
    lv_obj_t *actions[]={bluetooth_ui.scan,bluetooth_ui.discover};
    for(unsigned index=0;index<2;++index)if(!state.enabled || !state.ready || state.scanning)lv_obj_add_state(actions[index],LV_STATE_DISABLED);else lv_obj_remove_state(actions[index],LV_STATE_DISABLED);
    if(state.connected)lv_obj_remove_flag(bluetooth_ui.disconnect,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(bluetooth_ui.disconnect,LV_OBJ_FLAG_HIDDEN);
    if(bluetooth_ui.count!=state.count || memcmp(bluetooth_ui.items,state.peers,sizeof(bluetooth_ui.items))){
        bluetooth_ui.count=state.count;memcpy(bluetooth_ui.items,state.peers,sizeof(bluetooth_ui.items));lv_obj_clean(bluetooth_ui.peers);
        for(unsigned index=0;index<state.count;++index){
            const ouse_ble_peer_t *peer=&state.peers[index];
            ouse_widget_row(bluetooth_ui.peers,LV_SYMBOL_BLUETOOTH,peer->name[0] ? peer->name : peer->address,peer->bonded ? "已配对" : NULL,bluetooth_peer,(void *)(uintptr_t)index,NULL);
        }
    }
    if(state.enabled)lv_obj_remove_flag(bluetooth_ui.peers,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(bluetooth_ui.peers,LV_OBJ_FLAG_HIDDEN);
}
static void bluetooth_close(void)
{
    if(bluetooth_ui.prompt_id){(void)ouse_ble_confirm(bluetooth_ui.prompt_id,false);if(ouse_dialog_choice_active())ouse_dialog_dismiss();}
    memset(&bluetooth_ui,0,sizeof(bluetooth_ui));
}
