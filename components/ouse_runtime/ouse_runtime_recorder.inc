static JSValue recorder_api(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int operation)
{
    (void)self;instance_t *app=from_js(context);
    if(!app->files_allowed || !app->media_allowed)return operation==0 ? JS_NULL : JS_FALSE;
    if(operation==0){
        ouse_recorder_state_t state={0};if(!app->validation_only)ouse_recorder_get(&state);
        JSValue value=JS_NewObject(context);
        JS_SetPropertyStr(context,value,"active",JS_NewBool(context,state.active));JS_SetPropertyStr(context,value,"stopping",JS_NewBool(context,state.stopping));
        JS_SetPropertyStr(context,value,"busy",JS_NewBool(context,state.busy));JS_SetPropertyStr(context,value,"more",JS_NewBool(context,state.more));
        JS_SetPropertyStr(context,value,"taskId",JS_NewUint32(context,state.task_id));JS_SetPropertyStr(context,value,"revision",JS_NewUint32(context,state.revision));
        JS_SetPropertyStr(context,value,"durationMs",JS_NewUint32(context,state.duration_ms));JS_SetPropertyStr(context,value,"ended",JS_NewFloat64(context,state.ended));
        JS_SetPropertyStr(context,value,"offset",JS_NewUint32(context,state.offset));JS_SetPropertyStr(context,value,"path",JS_NewString(context,state.path));
        JS_SetPropertyStr(context,value,"error",JS_NewString(context,esp_err_to_name(state.error)));JSValue entries=JS_NewArray(context);
        for(unsigned index=0;index<state.count;++index){JSValue entry=JS_NewObject(context);
            JS_SetPropertyStr(context,entry,"path",JS_NewString(context,state.entries[index].path));JS_SetPropertyStr(context,entry,"durationMs",JS_NewUint32(context,state.entries[index].duration_ms));
            JS_SetPropertyStr(context,entry,"ended",JS_NewFloat64(context,state.entries[index].ended));JS_SetPropertyStr(context,entry,"bytes",JS_NewFloat64(context,state.entries[index].bytes));JS_SetPropertyUint32(context,entries,index,entry);}
        JS_SetPropertyStr(context,value,"entries",entries);return value;
    }
    if(app->validation_only)return JS_FALSE;
    if(operation==2)return JS_NewBool(context,ouse_recorder_stop(&app->owner));
    if(operation==1 && ouse_permissions_get(app->id,OUSE_PERMISSION_MICROPHONE)!=OUSE_PERMISSION_ALLOW)return JS_FALSE;
    if(operation==5 && ouse_permissions_get(app->id,OUSE_PERMISSION_DELETE)!=OUSE_PERMISSION_ALLOW)return JS_FALSE;
    char path[128]="",name[80]="";uint32_t offset=0;
    if(operation==3){double number;if(argc!=1 || !JS_IsNumber(argv[0]) || JS_ToFloat64(context,&number,argv[0]) || !isfinite(number) || number<0 || number>1024 || floor(number)!=number)return JS_ThrowTypeError(context,"Expected recording offset 0..1024");offset=number;}
    if(operation>=4 && (argc!=(operation==4 ? 2 : 1) || !copy_string(context,argv[0],path,sizeof(path)) || (operation==4 && !copy_string(context,argv[1],name,sizeof(name)))))return JS_ThrowTypeError(context,"Expected recording path and optional name");
    if(operation==1 && ouse_memory_pressure()==OUSE_MEMORY_CRITICAL)return JS_FALSE;
    uint32_t id=0;model_lock(app);esp_err_t error=app->closed || app->faulted || !app->visible ? ESP_ERR_INVALID_STATE : operation==1 ? ouse_recorder_start(&app->owner,&id) : ouse_recorder_request(&app->owner,operation-3,path,name,offset,&id);model_unlock(app);
    return error==ESP_OK ? JS_NewUint32(context,id) : JS_FALSE;
}
