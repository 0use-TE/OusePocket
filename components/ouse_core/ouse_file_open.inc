typedef struct {
    char defaults[3][48],path[128],chosen[48],source[48],ids[8][48],names[12][96];
    const char *choices[12];
    int actions[12];
    unsigned type,offset,count,next,revision,saving;
    bool settings,loaded,io,writing,dirty,failed;
    atomic_bool completed;
    esp_err_t error;
    char *result;
} file_open_state_t;
static OUSE_PSRAM_BSS file_open_state_t file_open;
static const unsigned file_masks[]={OUSE_FILE_JPEG,OUSE_FILE_WAVE,OUSE_FILE_AVI};
static const char *file_names[]={"JPEG 图片","WAV 音频","AVI 视频"};
static bool file_extension(const char *left,const char *right)
{while(*left && *right){if(tolower((unsigned char)*left++)!=*right++)return false;}return !*left && !*right;}
static void file_picker(void);
static bool file_handler_valid(const char *id,unsigned type)
{
    ouse_app_info_t info;
    return *id && ouse_app_find_info(id,&info)==ESP_OK && (info.file_types&file_masks[type]);
}
static void file_default_changed(void){file_open.dirty=true;file_open.failed=false;++file_open.revision;}
static void file_launch(void)
{
    ouse_app_stats_t stats;if(ouse_app_get_stats(&stats)!=ESP_OK || strcmp(stats.active_id,file_open.source))return;
    ouse_files_state_t card;ouse_files_get(&card);
    if(!card.ready || !card.present){ouse_dialog_show("SD 卡不可用",NULL,NULL);return;}
    if(!file_handler_valid(file_open.chosen,file_open.type) || ouse_app_open_file(file_open.chosen,file_open.path)!=ESP_OK)
        ouse_dialog_show("无法打开文件，请检查应用和文件格式。",NULL,NULL);
}
static void file_remember(unsigned choice)
{
    if(choice>1)return;
    if(choice==1){strcpy(file_open.defaults[file_open.type],file_open.chosen);file_default_changed();}
    file_launch();
}
static void file_picked(unsigned choice)
{
    if(choice>=file_open.count)return;
    int action=file_open.actions[choice];
    if(action==-1){file_open.offset=file_open.next;file_picker();return;}
    if(action==-2){file_open.offset=file_open.offset>=8 ? file_open.offset-8 : 0;file_picker();return;}
    if(action==-3){file_open.defaults[file_open.type][0]=0;file_default_changed();return;}
    if(action<0 || action>=8)return;
    strcpy(file_open.chosen,file_open.ids[action]);
    if(file_open.settings){strcpy(file_open.defaults[file_open.type],file_open.chosen);file_default_changed();return;}
    static const char *choices[]={"仅此次","始终"};
    ouse_dialog_select("打开方式",choices,2,0,file_remember);
}
static void file_picker(void)
{
    file_open.count=0;unsigned matched=0,shown=0;bool more=false;
    for(unsigned index=0;index<core.app_count;++index){
        ouse_app_info_t info;if(ouse_app_get_info(index,&info)!=ESP_OK || !(info.file_types&file_masks[file_open.type]))continue;
        if(matched++<file_open.offset)continue;
        if(shown==8){more=true;break;}
        strcpy(file_open.ids[shown],info.id);
        snprintf(file_open.names[file_open.count],96,"%s%s",info.name,!strcmp(file_open.defaults[file_open.type],info.id) ? " · 默认" : "");
        file_open.actions[file_open.count++]=(int)shown++;
    }
    file_open.next=file_open.offset+shown;
    if(file_open.offset){strcpy(file_open.names[file_open.count],"上一页");file_open.actions[file_open.count++]=-2;}
    if(more){strcpy(file_open.names[file_open.count],"下一页");file_open.actions[file_open.count++]=-1;}
    if(file_open.settings){strcpy(file_open.names[file_open.count],"每次询问");file_open.actions[file_open.count++]=-3;}
    if(!file_open.count){ouse_dialog_show("没有支持此文件格式的应用。",NULL,NULL);return;}
    for(unsigned index=0;index<file_open.count;++index)file_open.choices[index]=file_open.names[index];
    ouse_dialog_select(file_names[file_open.type],file_open.choices,file_open.count,0,file_picked);
}
static void file_type_picked(unsigned type)
{if(type<3){file_open.settings=true;file_open.type=type;file_open.offset=0;file_picker();}}
static void file_settings(lv_event_t *event)
{
    (void)event;
    if(!file_open.loaded){ouse_dialog_show("默认应用正在读取，请稍后重试。",NULL,NULL);return;}
    ouse_dialog_select("默认应用",file_names,3,0,file_type_picked);
}
esp_err_t ouse_core_open_file(const char *source,const char *path)
{
    if(!source || !path || !*path || strnlen(path,128)>=128)return ESP_ERR_INVALID_ARG;
    ouse_app_stats_t stats;if(ouse_app_get_stats(&stats)!=ESP_OK || strcmp(stats.active_id,source))return ESP_ERR_INVALID_STATE;
    char absolute[180];if(!ouse_sd_browser_path(path,absolute,sizeof(absolute)))return ESP_ERR_INVALID_ARG;
    if(ouse_dialog_active() || !file_open.loaded)return ESP_ERR_INVALID_STATE;
    const char *extension=strrchr(path,'.');unsigned type=3;
    if(extension){if(file_extension(extension,".jpg") || file_extension(extension,".jpeg"))type=0;
        else if(file_extension(extension,".wav"))type=1;else if(file_extension(extension,".avi"))type=2;}
    if(type==3){ouse_dialog_show("没有支持此文件格式的应用。",NULL,NULL);return ESP_ERR_NOT_SUPPORTED;}
    file_open.settings=false;file_open.type=type;file_open.offset=0;strcpy(file_open.path,path);snprintf(file_open.source,sizeof(file_open.source),"%s",source);
    char *preferred=file_open.defaults[type];
    if(*preferred && !file_handler_valid(preferred,type)){*preferred=0;file_default_changed();}
    if(*preferred){strcpy(file_open.chosen,preferred);file_launch();return ESP_OK;}
    unsigned count=0;
    for(unsigned index=0;index<core.app_count;++index){ouse_app_info_t info;if(ouse_app_get_info(index,&info)==ESP_OK && (info.file_types&file_masks[type])){++count;strcpy(file_open.chosen,info.id);}}
    if(count==1)file_launch();else file_picker();
    return ESP_OK;
}
static void file_preferences_complete(void *context,uint32_t token,esp_err_t error,char *result)
{(void)context;(void)token;file_open.error=error;file_open.result=result;atomic_store_explicit(&file_open.completed,true,memory_order_release);}
static bool file_preferences_parse(const char *value)
{
    char defaults[3][48]={{0}};
    if(*value){if(strncmp(value,"OUSEF1\n",7))return false;value+=7;
        for(unsigned index=0;index<3;++index){const char *end=strchr(value,'\n');if(!end || end-value>=48)return false;
            memcpy(defaults[index],value,(size_t)(end-value));
            for(const char *letter=defaults[index];*letter;++letter)if(!((*letter>='a'&&*letter<='z') || (*letter>='0'&&*letter<='9') || *letter=='.' || *letter=='_' || *letter=='-'))return false;
            value=end+1;}if(*value)return false;}
    memcpy(file_open.defaults,defaults,sizeof(defaults));return true;
}
static void file_preferences_tick(lv_timer_t *timer)
{
    (void)timer;
    if(atomic_exchange_explicit(&file_open.completed,false,memory_order_acquire)){
        file_open.io=false;
        if(file_open.writing){if(file_open.error==ESP_OK && file_open.saving==file_open.revision)file_open.dirty=false;}
        else {file_open.loaded=true;if(file_open.error==ESP_OK && (!file_open.result || !file_preferences_parse(file_open.result)))file_open.error=ESP_ERR_INVALID_RESPONSE;}
        free(file_open.result);file_open.result=NULL;
        if(file_open.error!=ESP_OK){file_open.failed=true;ouse_dialog_show("默认应用读取或保存失败，请重新设置。",NULL,NULL);}
    }
    if(file_open.io || file_open.failed || ouse_tasks_quiescing())return;
    if(file_open.loaded && launcher.restored)for(unsigned index=0;index<3;++index)if(*file_open.defaults[index] && !file_handler_valid(file_open.defaults[index],index)){file_open.defaults[index][0]=0;file_default_changed();}
    ouse_storage_stats_t storage;ouse_storage_get_stats(&storage);if(!storage.ready)return;
    if(!file_open.loaded){file_open.writing=false;file_open.io=ouse_storage_file_defaults(false,NULL,file_preferences_complete,NULL)==ESP_OK;return;}
    if(file_open.dirty){char value[160];snprintf(value,sizeof(value),"OUSEF1\n%s\n%s\n%s\n",file_open.defaults[0],file_open.defaults[1],file_open.defaults[2]);
        file_open.writing=true;file_open.saving=file_open.revision;file_open.io=ouse_storage_file_defaults(true,value,file_preferences_complete,NULL)==ESP_OK;}
}
