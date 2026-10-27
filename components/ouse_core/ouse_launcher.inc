typedef struct {
    char (*ids)[48];
    unsigned count;
    unsigned hidden_native;
    lv_obj_t **tiles,**badges,*ghost;
    unsigned tile_capacity,from,to;
    lv_indev_t *pointer;
    lv_point_t point;
    uint32_t edge_since,revision,saving_revision;
    int edge;
    bool dragging,suppress_click,loaded,restored,io,writing,dirty,failed,editing;
    char remove_id[48];
    uint32_t remove_request;
    atomic_bool completed;
    esp_err_t error;
    char *result;
} launcher_state_t;
static OUSE_PSRAM_BSS launcher_state_t launcher;
static void launcher_cancel(void);
static void launcher_changed(void);
static const char *const launcher_native_ids[]={"ouse.native","ouse.quake2","ouse.doom","ouse.nes"};
static unsigned launcher_native_bit(const char *id)
{
    for(unsigned i=0;i<4;++i)if(!strcmp(id,launcher_native_ids[i]))return 1u<<i;
    return 0;
}
static void launcher_restore_icons(lv_event_t *event)
{
    (void)event;if(!launcher.loaded)return;
    launcher.hidden_native=0;launcher_changed();rebuild_launcher();
}
static void launcher_edit(bool enabled)
{
    launcher.editing=enabled;
    for(unsigned index=0;index<core.app_count && index<launcher.tile_capacity;++index){
        if(!launcher.badges[index])continue;
        if(enabled)lv_obj_remove_flag(launcher.badges[index],LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(launcher.badges[index],LV_OBJ_FLAG_HIDDEN);
    }
}
static void launcher_blank(lv_event_t *event)
{
    if(launcher.editing && lv_event_get_target_obj(event)==lv_event_get_current_target_obj(event))launcher_edit(false);
}
static void launcher_remove_confirmed(void)
{
    unsigned native=launcher_native_bit(launcher.remove_id);
    if(native){launcher.hidden_native|=native;launcher_changed();rebuild_launcher();return;}
    if(launcher.remove_request)return;
    esp_err_t error=ouse_manager_request(OUSE_MANAGER_UNINSTALL,launcher.remove_id,&launcher.remove_request);
    if(error!=ESP_OK)ouse_dialog_show(ouse_ui_locale() ? "Application service busy. Try again." : "应用服务繁忙，请稍后重试。",NULL,NULL);
}
static void launcher_remove_clicked(lv_event_t *event)
{
    if(launcher.dragging || launcher.remove_request || ouse_dialog_active())return;
    const char *id=lv_event_get_user_data(event);ouse_app_info_t info;
    bool native=launcher_native_bit(id)!=0;
    if(!launcher.editing || (!native && strncmp(id,"user.",5)) || ouse_app_find_info(id,&info)!=ESP_OK)return;
    strcpy(launcher.remove_id,id);
    if(native){
        char body[400];snprintf(body,sizeof(body),"%s\n\n%s",info.name,ouse_ui_locale() ?
            "Remove this desktop icon? The program and SD files stay. Restore it in Settings > Appearance." :
            "移除此桌面图标？程序和 SD 卡文件会保留，可在设置的外观中恢复图标。");
        ouse_dialog_show(body,ouse_ui_locale() ? "Remove" : "移除",launcher_remove_confirmed);return;
    }
    char body[360];snprintf(body,sizeof(body),"%s\n\n%s",info.name,ouse_ui_locale() ?
        "Uninstall this app and its local data and alarms? SD files stay." :
        "卸载应用并删除本地数据和闹钟？SD 卡文件会保留。");
    ouse_dialog_show(body,ouse_ui_locale() ? "Uninstall" : "卸载",launcher_remove_confirmed);
}
static void launcher_badge(unsigned index,int cell_width)
{
    launcher.badges[index]=NULL;
    if((strncmp(core.icons[index].id,"user.",5) && !launcher_native_bit(core.icons[index].id))||!strcmp(core.icons[index].id,"user.store"))return;
    lv_obj_t *badge=lv_button_create(launcher.tiles[index]);lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge,ouse_ui_dp(28),ouse_ui_dp(28));
    lv_obj_set_pos(badge,(cell_width+ouse_ui_dp(40))/2-ouse_ui_dp(18),0);
    lv_obj_set_style_bg_color(badge,lv_color_hex(0xD63648),0);
    lv_obj_set_style_bg_opa(badge,LV_OPA_COVER,0);lv_obj_set_style_radius(badge,LV_RADIUS_CIRCLE,0);
    lv_obj_remove_flag(badge,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t *mark=lv_label_create(badge);ouse_ui_text(mark,"X");ouse_ui_use_font(mark,16);
    lv_obj_set_style_text_color(mark,lv_color_hex(0xFFFFFF),0);lv_obj_center(mark);
    lv_obj_add_event_cb(badge,launcher_remove_clicked,LV_EVENT_CLICKED,core.icons[index].id);
    launcher.badges[index]=badge;
    if(!launcher.editing)lv_obj_add_flag(badge,LV_OBJ_FLAG_HIDDEN);
}

static int launcher_rank(const char *id)
{
    for(unsigned index=0;index<launcher.count;++index)if(!strcmp(launcher.ids[index],id))return (int)index;
    return -1;
}
static int launcher_compare(const void *first,const void *second)
{
    const ouse_app_info_t *left=first,*right=second;
    if(!strcmp(left->id,right->id))return 0;
    int left_rank=launcher_rank(left->id),right_rank=launcher_rank(right->id);
    if(left_rank>=0 || right_rank>=0){
        if(left_rank<0)return 1;
        if(right_rank<0)return -1;
        return (left_rank>right_rank)-(left_rank<right_rank);
    }
    if(!strcmp(left->id,"ouse.settings"))return -1;
    if(!strcmp(right->id,"ouse.settings"))return 1;
    return strcmp(left->id,right->id);
}
static void launcher_sort(void){qsort(core.icons,core.app_count,sizeof(*core.icons),launcher_compare);}
static bool launcher_tiles(unsigned count)
{
    if(count>launcher.tile_capacity){
        lv_obj_t **tiles=ouse_psram_calloc(count,sizeof(*tiles));if(!tiles)return false;
        lv_obj_t **badges=ouse_psram_calloc(count,sizeof(*badges));if(!badges){free(tiles);return false;}
        free(launcher.tiles);free(launcher.badges);launcher.tiles=tiles;launcher.badges=badges;launcher.tile_capacity=count;
    }
    return true;
}
static void launcher_changed(void){launcher.dirty=true;launcher.failed=false;++launcher.revision;}
static bool launcher_capture(void)
{
    char (*ids)[48]=ouse_psram_calloc(core.app_count ? core.app_count : 1,sizeof(*ids));
    if(!ids)return false;
    for(unsigned index=0;index<core.app_count;++index)strcpy(ids[index],core.icons[index].id);
    free(launcher.ids);launcher.ids=ids;launcher.count=core.app_count;launcher_changed();return true;
}
static void launcher_forget(const char *id)
{
    int index=launcher_rank(id);if(index<0)return;
    memmove(launcher.ids+index,launcher.ids+index+1,(launcher.count-(unsigned)index-1)*sizeof(*launcher.ids));
    --launcher.count;launcher_changed();
}
static bool launcher_parse(const char *value)
{
    if(!*value){free(launcher.ids);launcher.ids=NULL;launcher.count=0;launcher.hidden_native=0;return true;}
    if(strncmp(value,"OUSE1\n",6))return false;
    unsigned count=0;const char *cursor=value+6;
    while(*cursor){const char *end=strchr(cursor,'\n');if(!end || end==cursor || end-cursor>=48)return false;++count;cursor=end+1;}
    char (*ids)[48]=ouse_psram_calloc(count ? count : 1,sizeof(*ids));if(!ids)return false;
    cursor=value+6;unsigned kept=0,hidden=0;
    for(unsigned entry=0;entry<count;++entry){
        unsigned index=kept;
        const char *end=strchr(cursor,'\n');memcpy(ids[index],cursor,(size_t)(end-cursor));cursor=end+1;
        if(ids[index][0]=='-'){
            unsigned bit=launcher_native_bit(ids[index]+1);
            if(!bit || (hidden&bit)){free(ids);return false;}
            hidden|=bit;memset(ids[index],0,sizeof(*ids));continue;
        }
        for(const char *letter=ids[index];*letter;++letter)if(!((*letter>='a'&&*letter<='z') || (*letter>='0'&&*letter<='9') || *letter=='.' || *letter=='_' || *letter=='-')){free(ids);return false;}
        for(unsigned previous=0;previous<index;++previous)if(!strcmp(ids[index],ids[previous])){free(ids);return false;}
        ++kept;
    }
    free(launcher.ids);launcher.ids=ids;launcher.count=kept;launcher.hidden_native=hidden;return true;
}
static void launcher_complete(void *context,uint32_t token,esp_err_t error,char *result)
{
    (void)context;(void)token;launcher.error=error;launcher.result=result;
    atomic_store_explicit(&launcher.completed,true,memory_order_release);
}
static void launcher_retry(void){launcher.failed=false;}
static void launcher_error(void)
{
    launcher.failed=true;
    ouse_dialog_show(ouse_ui_locale() ? "Could not save desktop order." : "桌面排序保存失败。",ouse_ui_locale() ? "Retry" : "重试",launcher_retry);
}
static void launcher_shape(int width,int height,int *columns,int *rows)
{*columns=width>height?6:3;*rows=width>height?2:4;}
static void launcher_reposition(void)
{
    int width=lv_obj_get_width(core.grid),height=lv_obj_get_height(core.grid),cols,rows;
    launcher_shape(width,height,&cols,&rows);
    unsigned capacity=(unsigned)(cols*rows);
    for(unsigned index=0;index<core.app_count;++index){
        lv_obj_t *tile=launcher.tiles[index],*page=lv_obj_get_child(core.grid,index/capacity);
        if(lv_obj_get_parent(tile)!=page)lv_obj_set_parent(tile,page);
        lv_obj_set_pos(tile,(int)((index%capacity)%cols)*(width/cols),(int)((index%capacity)/cols)*(height/rows));
    }
}
static void launcher_move(unsigned target)
{
    if(target==launcher.to || target>=core.app_count)return;
    ouse_app_info_t icon=core.icons[launcher.to];lv_obj_t *tile=launcher.tiles[launcher.to];
    if(target>launcher.to){
        memmove(core.icons+launcher.to,core.icons+launcher.to+1,(target-launcher.to)*sizeof(*core.icons));
        memmove(launcher.tiles+launcher.to,launcher.tiles+launcher.to+1,(target-launcher.to)*sizeof(*launcher.tiles));
    }else{
        memmove(core.icons+target+1,core.icons+target,(launcher.to-target)*sizeof(*core.icons));
        memmove(launcher.tiles+target+1,launcher.tiles+target,(launcher.to-target)*sizeof(*launcher.tiles));
    }
    core.icons[target]=icon;launcher.tiles[target]=tile;launcher.to=target;launcher_reposition();
}
static void launcher_end(bool commit)
{
    if(!launcher.dragging)return;
    launcher.dragging=false;
    if(!commit)launcher_move(launcher.from);
    lv_obj_remove_flag(launcher.tiles[launcher.to],LV_OBJ_FLAG_HIDDEN);
    lv_obj_delete(launcher.ghost);launcher.ghost=NULL;launcher.pointer=NULL;launcher.edge=0;
    lv_obj_add_flag(core.grid,LV_OBJ_FLAG_SCROLLABLE);
    if(commit && launcher.from!=launcher.to && !launcher_capture())launcher_error();
    rebuild_launcher();
}
static void launcher_cancel(void){if(launcher.dragging)launcher_end(false);}
static void launcher_drag_point(void)
{
    lv_indev_get_point(launcher.pointer,&launcher.point);
    lv_obj_set_pos(launcher.ghost,launcher.point.x-ouse_ui_dp(24),launcher.point.y-ouse_ui_dp(24));
    lv_area_t bounds;lv_obj_get_coords(core.grid,&bounds);
    if(launcher.point.y<bounds.y1 || launcher.point.y>bounds.y2){launcher.edge=0;return;}
    int edge=launcher.point.x<bounds.x1+ouse_ui_dp(24) ? -1 : launcher.point.x>bounds.x2-ouse_ui_dp(24) ? 1 : 0;
    if(edge!=launcher.edge){launcher.edge=edge;launcher.edge_since=lv_tick_get();}
    if(edge)return;
    int width=lv_obj_get_width(core.grid),height=lv_obj_get_height(core.grid);
    int cols,rows;launcher_shape(width,height,&cols,&rows);
    int column=(launcher.point.x-bounds.x1)/(width/cols),row=(launcher.point.y-bounds.y1)/(height/rows);
    if(column<0 || column>=cols || row<0 || row>=rows)return;
    unsigned target=core.launcher_page*(unsigned)(cols*rows)+(unsigned)(row*cols+column);
    if(target>=core.app_count)target=core.app_count-1;
    launcher_move(target);
}
static void launcher_touch(lv_event_t *event)
{
    lv_event_code_t code=lv_event_get_code(event);lv_obj_t *tile=lv_event_get_target_obj(event);
    if(code==LV_EVENT_PRESSED)launcher.suppress_click=false;
    if(code==LV_EVENT_LONG_PRESSED && !launcher.dragging && launcher.loaded && launcher.restored){
        launcher_edit(true);
        unsigned index=0;while(index<core.app_count && launcher.tiles[index]!=tile)++index;
        lv_indev_t *pointer=lv_event_get_indev(event);if(index==core.app_count || !pointer)return;
        launcher.ghost=lv_obj_create(core.screen);lv_obj_remove_style_all(launcher.ghost);
        lv_obj_set_size(launcher.ghost,ouse_ui_dp(48),ouse_ui_dp(48));
        lv_obj_remove_flag(launcher.ghost,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *icon=ouse_widget_app_icon(launcher.ghost,&core.icons[index],48);lv_obj_center(icon);
        launcher.dragging=true;launcher.suppress_click=true;launcher.pointer=pointer;launcher.from=launcher.to=index;
        lv_obj_remove_flag(core.grid,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(tile,LV_OBJ_FLAG_HIDDEN);
        launcher_drag_point();
    }else if(code==LV_EVENT_PRESSING && launcher.dragging)launcher_drag_point();
    else if((code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) && launcher.dragging){
        lv_area_t bounds;lv_obj_get_coords(core.grid,&bounds);
        bool inside=launcher.point.x>=bounds.x1 && launcher.point.x<=bounds.x2 && launcher.point.y>=bounds.y1 && launcher.point.y<=bounds.y2;
        launcher_end(code==LV_EVENT_RELEASED && inside);
    }
}
static void launcher_tick(lv_timer_t *timer)
{
    (void)timer;
    if(launcher.remove_request){
        ouse_manager_state_t state={0};ouse_manager_get(&state);
        if(state.completed_id==launcher.remove_request){
            launcher.remove_request=0;
            if(state.error!=ESP_OK)ouse_dialog_show(ouse_ui_locale() ? "Uninstall failed. Try again in Settings." : "卸载失败，请在设置中重试。",NULL,NULL);
        }
        ouse_manager_release(&state);
    }
    if(launcher.dragging){
        if(!lv_obj_is_visible(core.home)){launcher_cancel();return;}
        if(launcher.edge && lv_tick_elaps(launcher.edge_since)>=500){
            int page=(int)core.launcher_page+launcher.edge;
            if(page>=0 && page<(int)core.launcher_pages){
                lv_obj_scroll_to_x(core.grid,page*lv_obj_get_width(core.grid),LV_ANIM_OFF);
                unsigned target=(unsigned)page*12+(launcher.to%12);if(target>=core.app_count)target=core.app_count-1;
                launcher_move(target);
            }
            launcher.edge_since=lv_tick_get();
        }
        return;
    }
    if(atomic_exchange_explicit(&launcher.completed,false,memory_order_acquire)){
        launcher.io=false;
        if(launcher.writing){
            if(launcher.error==ESP_OK){if(launcher.saving_revision==launcher.revision)launcher.dirty=false;}
            else launcher_error();
        }else{
            if(launcher.error==ESP_OK && launcher.result && launcher_parse(launcher.result)){launcher.loaded=true;rebuild_launcher();}
            else{launcher.failed=true;ouse_dialog_show(ouse_ui_locale() ? "Could not load desktop order." : "桌面排序读取失败。",ouse_ui_locale() ? "Retry" : "重试",launcher_retry);}
        }
        free(launcher.result);launcher.result=NULL;
    }
    if(launcher.io || launcher.failed || ouse_tasks_quiescing())return;
    ouse_storage_stats_t storage;ouse_storage_get_stats(&storage);if(!storage.ready)return;
    if(!launcher.loaded){
        launcher.writing=false;launcher.io=ouse_storage_launcher(false,NULL,launcher_complete,NULL)==ESP_OK;return;
    }
    if(launcher.restored){
        bool changed=launcher.count!=core.app_count;
        for(unsigned index=0;!changed && index<core.app_count;++index)changed=strcmp(launcher.ids[index],core.icons[index].id)!=0;
        if(changed && !launcher_capture()){launcher_error();return;}
    }
    if(launcher.dirty && launcher.restored){
        size_t size=7;for(unsigned index=0;index<launcher.count;++index)size+=strlen(launcher.ids[index])+1;
        for(unsigned i=0;i<4;++i)if(launcher.hidden_native&(1u<<i))size+=strlen(launcher_native_ids[i])+2;
        char *value=ouse_psram_calloc(size,1);if(!value){launcher_error();return;}
        strcpy(value,"OUSE1\n");size_t offset=6;
        for(unsigned index=0;index<launcher.count;++index){size_t length=strlen(launcher.ids[index]);memcpy(value+offset,launcher.ids[index],length);offset+=length;value[offset++]='\n';}
        for(unsigned i=0;i<4;++i)if(launcher.hidden_native&(1u<<i))offset+=(size_t)snprintf(value+offset,size-offset,"-%s\n",launcher_native_ids[i]);
        launcher.writing=true;launcher.saving_revision=launcher.revision;
        esp_err_t error=ouse_storage_launcher(true,value,launcher_complete,NULL);free(value);
        launcher.io=error==ESP_OK;if(error!=ESP_OK)launcher_error();
    }
}
