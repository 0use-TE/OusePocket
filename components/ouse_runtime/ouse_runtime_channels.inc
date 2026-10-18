static JSValue channel_api(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int operation)
{
    (void)self;instance_t *app=from_js(context);
    if(!app->network_allowed || app->validation_only || app->closed || app->faulted)return JS_FALSE;
    if(operation<=2){
        ouse_channel_config_t *config=ouse_psram_calloc(1,sizeof(*config));if(!config)return JS_ThrowOutOfMemory(context);
        bool valid=argc==1;
        if(operation==2)valid=valid && copy_string(context,argv[0],config->url,sizeof(config->url));
        else valid=valid && JS_IsObject(argv[0]) && net_string(context,argv[0],"url",config->url,sizeof(config->url),"") &&
            net_string(context,argv[0],"username",config->username,sizeof(config->username),"") && net_string(context,argv[0],"password",config->password,sizeof(config->password),"") &&
            net_string(context,argv[0],"clientId",config->client_id,sizeof(config->client_id),"");
        if(!valid){memset(config->password,0,sizeof(config->password));free(config);return JS_ThrowTypeError(context,"Expected connection options or ping hostname");}
        uint32_t id=0;model_lock(app);esp_err_t error=app->closed ? ESP_ERR_INVALID_STATE : ouse_channel_open(&app->owner,operation,config,&id);model_unlock(app);
        memset(config->password,0,sizeof(config->password));free(config);return error==ESP_OK ? JS_NewUint32(context,id) : JS_FALSE;
    }
    double exact=0;
    if(argc<1 || !JS_IsNumber(argv[0]) || JS_ToFloat64(context,&exact,argv[0]) || !isfinite(exact) || exact<1 || exact>UINT32_MAX || floor(exact)!=exact)return JS_ThrowTypeError(context,"Expected channel ID");
    uint32_t id=exact;
    if(operation==3){
        ouse_channel_state_t state;if(argc!=1 || !ouse_channel_get(&app->owner,id,&state))return JS_NULL;
        JSValue value=JS_NewObject(context);JS_SetPropertyStr(context,value,"id",JS_NewUint32(context,state.id));
        JS_SetPropertyStr(context,value,"connected",JS_NewBool(context,state.connected));JS_SetPropertyStr(context,value,"closed",JS_NewBool(context,state.closed));
        JS_SetPropertyStr(context,value,"error",JS_NewString(context,esp_err_to_name(state.error)));JS_SetPropertyStr(context,value,"dropped",JS_NewUint32(context,state.dropped));return value;
    }
    if(operation==4){
        if(argc!=1)return JS_ThrowTypeError(context,"poll expects channel ID");
        ouse_channel_event_t *event=ouse_psram_calloc(1,sizeof(*event));if(!event)return JS_ThrowOutOfMemory(context);
        if(!ouse_channel_poll(&app->owner,id,event)){free(event);return JS_NULL;}
        JSValue value=JS_NewObject(context);JS_SetPropertyStr(context,value,"type",JS_NewString(context,event->type));JS_SetPropertyStr(context,value,"topic",JS_NewString(context,event->topic));
        JS_SetPropertyStr(context,value,"data",event->binary ? JS_NewArrayBufferCopy(context,event->data,event->size) : JS_NewStringLen(context,(const char *)event->data,event->size));
        JS_SetPropertyStr(context,value,"binary",JS_NewBool(context,event->binary));JS_SetPropertyStr(context,value,"value",JS_NewFloat64(context,event->value));free(event);return value;
    }
    if(operation==5)return argc==1 ? JS_NewBool(context,ouse_channel_close(&app->owner,id)) : JS_FALSE;
    bool subscribe=operation==8,mqtt=operation>=7;char topic[129]="";int qos=0;bool retain=false;
    if(argc!=(subscribe ? 3 : mqtt ? 5 : 2))return JS_ThrowTypeError(context,"Expected ID, topic/data and QoS options");
    if(mqtt && !copy_string(context,argv[1],topic,sizeof(topic)))return JS_ThrowTypeError(context,"Expected topic <=128 bytes");
    if(mqtt){
        JSValueConst argument=argv[subscribe ? 2 : 3];double quality;
        if(!JS_IsNumber(argument) || JS_ToFloat64(context,&quality,argument) || (quality!=0 && quality!=1))return JS_ThrowTypeError(context,"QoS must be 0 or 1");
        qos=quality;
        if(!subscribe){if(!JS_IsBool(argv[4]))return JS_ThrowTypeError(context,"Expected retain boolean");retain=JS_ToBool(context,argv[4]);}
    }
    if(subscribe)return JS_NewBool(context,ouse_channel_subscribe(&app->owner,id,topic,qos)==ESP_OK);
    JSValueConst argument=argv[mqtt ? 2 : 1];size_t size=0;bool binary=!JS_IsString(argument);const char *text=NULL;
    const void *data=binary ? (const void *)JS_GetArrayBuffer(context,&size,argument) : (const void *)(text=JS_ToCStringLen(context,&size,argument));
    if(!data && (size || !binary || JS_HasException(context)))return JS_EXCEPTION;
    esp_err_t error=ouse_channel_send(&app->owner,id,topic,data,size,binary,qos,retain);
    if(text)JS_FreeCString(context,text);
    return JS_NewBool(context,error==ESP_OK);
}
