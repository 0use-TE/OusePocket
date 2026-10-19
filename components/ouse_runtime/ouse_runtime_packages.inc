static JSValue packages_api(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int action)
{
    (void)self;instance_t *app=from_js(context);
    if(!app->packages_allowed||app->closed||app->faulted||app->validation_only)return JS_FALSE;
    if(action==0){
        ouse_package_state_t state;ouse_packages_get(&state);JSValue result=JS_NewObject(context);
        JS_SetPropertyStr(context,result,"taskId",JS_NewUint32(context,state.task));JS_SetPropertyStr(context,result,"busy",JS_NewBool(context,state.busy));
        JS_SetPropertyStr(context,result,"phase",JS_NewString(context,state.phase));JS_SetPropertyStr(context,result,"appId",JS_NewString(context,state.app_id));
        JS_SetPropertyStr(context,result,"received",JS_NewFloat64(context,state.received));JS_SetPropertyStr(context,result,"total",JS_NewFloat64(context,state.total));JS_SetPropertyStr(context,result,"error",JS_NewString(context,esp_err_to_name(state.error)));return result;
    }
    if(action==1){
        int32_t offset=0,limit=12;
        if(argc>2||(argc>0&&JS_ToInt32(context,&offset,argv[0]))||(argc>1&&JS_ToInt32(context,&limit,argv[1]))||offset<0||limit<1||limit>12)return JS_ThrowTypeError(context,"list(offset=0,limit=12)");
        ouse_manager_state_t *snapshot=ouse_psram_calloc(1,sizeof(*snapshot));if(!snapshot)return JS_ThrowOutOfMemory(context);ouse_manager_get(snapshot);
        if(snapshot->error==ESP_ERR_NO_MEM){ouse_manager_release(snapshot);free(snapshot);return JS_ThrowOutOfMemory(context);}
        JSValue result=JS_NewObject(context),items=JS_NewArray(context);unsigned count=0;
        for(unsigned index=(unsigned)offset;index<snapshot->count&&count<(unsigned)limit;++index){ouse_managed_app_t *entry=&snapshot->apps[index];JSValue item=JS_NewObject(context);
            JS_SetPropertyStr(context,item,"id",JS_NewString(context,entry->id));JS_SetPropertyStr(context,item,"name",JS_NewString(context,entry->name));JS_SetPropertyStr(context,item,"versionCode",JS_NewUint32(context,entry->version_code));JS_SetPropertyStr(context,item,"versionName",JS_NewString(context,entry->version_name));JS_SetPropertyStr(context,item,"system",JS_NewBool(context,entry->system));JS_SetPropertyUint32(context,items,count++,item);
            JS_SetPropertyStr(context,item,"description",JS_NewString(context,entry->description));JS_SetPropertyStr(context,item,"author",JS_NewString(context,entry->author));JS_SetPropertyStr(context,item,"releasedAt",JS_NewString(context,entry->released_at));
        }
        JS_SetPropertyStr(context,result,"items",items);JS_SetPropertyStr(context,result,"more",JS_NewBool(context,(unsigned)offset+count<snapshot->count));ouse_manager_release(snapshot);free(snapshot);return result;
    }
    if(!app->worker_visible)return JS_FALSE;
    ouse_package_request_t *request=ouse_psram_calloc(1,sizeof(*request));if(!request)return JS_ThrowOutOfMemory(context);request->owner=app->owner;request->local=action==3;
    bool valid=argc==1;
    if(action==3)valid=valid&&(app->files_allowed||app->sdcard_allowed)&&copy_string(context,argv[0],request->location,sizeof(request->location));
    else if(action==2){
        valid=valid&&app->network_allowed&&JS_IsObject(argv[0])&&net_string(context,argv[0],"url",request->location,sizeof(request->location),"")&&net_string(context,argv[0],"id",request->app_id,sizeof(request->app_id),"")&&net_string(context,argv[0],"sha256",request->sha256,sizeof(request->sha256),"");
        const char *keys[]={"size","versionCode"};uint32_t values[2]={0};
        for(unsigned index=0;valid&&index<2;++index){JSValue value=JS_GetPropertyStr(context,argv[0],keys[index]);double number=0;valid=JS_IsNumber(value)&&JS_ToFloat64(context,&number,value)==0&&number>=1&&number<=2147483647&&number==(uint32_t)number;if(valid)values[index]=(uint32_t)number;JS_FreeValue(context,value);}
        request->size=values[0];request->version_code=values[1];
    }else valid=false;
    uint32_t task=0;esp_err_t error=valid?ouse_packages_submit(request,&task):ESP_ERR_INVALID_ARG;free(request);
    if(!valid)return JS_ThrowTypeError(context,"Expected package URL/id/size/SHA256/version or SD BPK path");
    return error==ESP_OK?JS_NewUint32(context,task):JS_FALSE;
}
