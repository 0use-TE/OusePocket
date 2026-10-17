// SPDX-License-Identifier: MIT
// Included by the single JS worker. Native completion queues carry plain data.
static void net_job_release(net_job_t *job)
{
    instance_t *app=job->app;ouse_task_hold_result(job->token,false);
    if(app && app->context){JS_FreeValue(app->context,job->resolve);JS_FreeValue(app->context,job->reject);}
    ouse_net_result_free(job->result);free(job);--net_job_count;if(app)release_instance(app);
}
static void cancel_net_jobs(instance_t *app)
{
    for(unsigned i=0;i<MAX_NET_PENDING;++i){net_job_t *job=app->network[i];if(!job)continue;
        app->network[i]=NULL;job->slot=MAX_NET_PENDING;
        if(app->context){JS_FreeValue(app->context,job->resolve);JS_FreeValue(app->context,job->reject);}
        job->resolve=job->reject=JS_UNDEFINED;job->app=NULL;release_instance(app);
        if(job->completed)net_job_release(job);
    }
}
static void net_completed(void *context,uint32_t token,ouse_net_result_t *result)
{
    net_job_t *job=context;(void)token;job->result=result;
    BaseType_t queued=xQueueSend(net_completions,&job,0);configASSERT(queued==pdTRUE);(void)queued;wake_worker();
}
static void drain_net_completions(void)
{
    net_job_t *job;
    while(xQueueReceive(net_completions,&job,0)==pdTRUE){
        job->completed=true;
        if(!job->app){net_job_release(job);continue;}
        instance_t *app=job->app;model_lock(app);bool discard=app->closed || app->faulted;model_unlock(app);
        if(discard){if(job->slot<MAX_NET_PENDING)app->network[job->slot]=NULL;net_job_release(job);}
    }
}
static bool net_string(JSContext *ctx,JSValueConst object,const char *key,char *out,size_t size,const char *fallback)
{
    JSValue v=JS_GetPropertyStr(ctx,object,key);
    bool ok=JS_IsUndefined(v) && fallback;
    if(ok)snprintf(out,size,"%s",fallback);else ok=copy_string(ctx,v,out,size);
    JS_FreeValue(ctx,v);return ok;
}
static JSValue net_request(JSContext *context,JSValueConst self,int argc,JSValueConst *argv)
{
    (void)self;instance_t *app=from_js(context);JSValue handlers[2];
    JSValue promise=JS_NewPromiseCapability(context,handlers);if(JS_IsException(promise))return promise;
    esp_err_t error=ESP_ERR_INVALID_ARG;
    const char *body_text=NULL;size_t body_size=0;
    char url[OUSE_NET_URL_MAX+1],method[8],response_type[8];
    ouse_http_header_t *headers=NULL;unsigned header_count=0;
    unsigned timeout=10000;
    if(!app->network_allowed || app->validation_only){error=ESP_ERR_NOT_SUPPORTED;goto invalid;}
    if(argc!=1 || !JS_IsObject(argv[0]) || JS_IsNull(argv[0]))goto invalid;
    if(!net_string(context,argv[0],"url",url,sizeof(url),NULL) ||
       !net_string(context,argv[0],"method",method,sizeof(method),"GET") ||
       !net_string(context,argv[0],"responseType",response_type,sizeof(response_type),"text") ||
       (strcmp(response_type,"text") && strcmp(response_type,"bytes")))goto invalid;
    JSValue v=JS_GetPropertyStr(context,argv[0],"timeoutMs");double exact=10000;
    bool valid=JS_IsUndefined(v) || (JS_IsNumber(v) && JS_ToFloat64(context,&exact,v)==0 && isfinite(exact) && floor(exact)==exact && exact>=1000 && exact<=30000);
    JS_FreeValue(context,v);if(!valid)goto invalid;timeout=exact;
    v=JS_GetPropertyStr(context,argv[0],"body");
    if(!JS_IsUndefined(v)){
        if(!JS_IsString(v)){JS_FreeValue(context,v);goto invalid;}
        body_text=JS_ToCStringLen(context,&body_size,v);
        if(!body_text || body_size>OUSE_NET_BODY_MAX){JS_FreeValue(context,v);goto invalid;}
    }
    JS_FreeValue(context,v);
    v=JS_GetPropertyStr(context,argv[0],"headers");
    if(!JS_IsUndefined(v)){
        JSPropertyEnum *keys=NULL;uint32_t count=0;
        valid=JS_IsObject(v) && !JS_IsArray(v) && !JS_IsNull(v) &&
            JS_GetOwnPropertyNames(context,&keys,&count,v,JS_GPN_STRING_MASK|JS_GPN_ENUM_ONLY)==0 && count<=OUSE_NET_HEADERS_MAX;
        if(valid && count){headers=calloc(count,sizeof(*headers));valid=headers!=NULL;}
        for(uint32_t i=0;valid && i<count;++i){
            JSValue key=JS_AtomToValue(context,keys[i].atom),val=JS_GetProperty(context,v,keys[i].atom);
            valid=copy_string(context,key,headers[i].name,sizeof(headers[i].name)) && copy_string(context,val,headers[i].value,sizeof(headers[i].value));
            JS_FreeValue(context,key);JS_FreeValue(context,val);
        }
        for(uint32_t i=0;i<count;++i)JS_FreeAtom(context,keys[i].atom);
        js_free(context,keys);header_count=count;
        JS_FreeValue(context,v);if(!valid)goto invalid;
    }else JS_FreeValue(context,v);
    // Property getters can reenter this API. Reserve only after all parsing.
    unsigned slot=0;while(slot<MAX_NET_PENDING && app->network[slot])++slot;
    if(slot==MAX_NET_PENDING || net_job_count>=MAX_NET_JOBS){error=ESP_ERR_NO_MEM;goto invalid;}
    net_job_t *job=calloc(1,sizeof(*job));if(!job){error=ESP_ERR_NO_MEM;goto invalid;}
    job->app=app;job->slot=slot;job->resolve=handlers[0];job->reject=handlers[1];job->bytes=!strcmp(response_type,"bytes");
    ouse_http_request_t request={.url=url,.method=method,.body=(const unsigned char *)body_text,.body_size=body_size,
        .headers=headers,.header_count=header_count,.timeout_ms=timeout};
    ++net_job_count;atomic_fetch_add(&app->references,1);
    model_lock(app);error=app->closed || app->faulted ? ESP_ERR_INVALID_STATE : ESP_OK;
    if(error==ESP_OK){app->network[slot]=job;error=ouse_net_submit(&app->owner,&request,&job->token,net_completed,job);}
    if(error!=ESP_OK)app->network[slot]=NULL;
    model_unlock(app);
    if(body_text)JS_FreeCString(context,body_text);
    free(headers);
    if(job->token)JS_DefinePropertyValueStr(context,promise,"taskId",JS_NewUint32(context,job->token),JS_PROP_ENUMERABLE);
    if(error!=ESP_OK){
        // Keep local Promise handlers until the normal rejection helper runs.
        job->resolve=job->reject=JS_UNDEFINED;net_job_release(job);
        return rejected_storage_promise(context,promise,handlers,storage_error(context,"Error",esp_err_to_name(error),esp_err_to_name(error)));
    }
    return promise;
invalid:
    if(body_text)JS_FreeCString(context,body_text);
    free(headers);
    // Preserve an exception thrown by a property getter/conversion.
    if(JS_HasException(context))return rejected_storage_promise(context,promise,handlers,JS_GetException(context));
    return rejected_storage_promise(context,promise,handlers,storage_error(context,"Error",esp_err_to_name(error),esp_err_to_name(error)));
}
static void process_net_results(instance_t *app)
{
    for(unsigned i=0;i<MAX_NET_PENDING && !app->faulted;++i){net_job_t *job=app->network[i];
        if(!job || !job->completed || ouse_budget_slice_done(&app->budget))continue;
        model_lock(app);bool runnable=!app->closed && !app->faulted && app->navigation_stage==NAV_IDLE && (app->visible || app->background_active);model_unlock(app);
        if(!runnable)return;
        ouse_net_result_t *result=job->result;bool reject=result->cancelled || result->error!=ESP_OK;
        JSValue value;
        if(reject)value=storage_error(app->context,result->cancelled ? "AbortError" : "Error",result->cancelled ? "Request cancelled" : esp_err_to_name(result->error),result->cancelled ? "CANCELLED" : esp_err_to_name(result->error));
        else{
            value=JS_NewObject(app->context);
            JS_SetPropertyStr(app->context,value,"status",JS_NewInt32(app->context,result->response.status));
            JS_SetPropertyStr(app->context,value,"ok",JS_NewBool(app->context,result->response.status>=200 && result->response.status<300));
            JS_SetPropertyStr(app->context,value,"bodyBytes",JS_NewFloat64(app->context,result->response.body_size));
            JSValue headers=JS_NewArray(app->context);
            for(unsigned h=0;h<result->response.header_count;++h){JSValue pair=JS_NewArray(app->context);
                JS_SetPropertyUint32(app->context,pair,0,JS_NewString(app->context,result->response.headers[h].name));
                JS_SetPropertyUint32(app->context,pair,1,JS_NewString(app->context,result->response.headers[h].value));
                JS_SetPropertyUint32(app->context,headers,h,pair);
            }
            JS_SetPropertyStr(app->context,value,"headers",headers);
            JS_SetPropertyStr(app->context,value,"body",job->bytes ? JS_NewArrayBufferCopy(app->context,result->body,result->response.body_size) : JS_NewStringLen(app->context,(const char *)result->body,result->response.body_size));
        }
        if(JS_HasException(app->context)){JS_FreeValue(app->context,value);value=JS_GetException(app->context);reject=true;}
        if(ouse_budget_slice_done(&app->budget)){JS_FreeValue(app->context,value);return;}
        app->network[i]=NULL;job->slot=MAX_NET_PENDING;
        JSValue settled=call_js(app,"network",reject ? "reject" : "resolve",reject ? job->reject : job->resolve,JS_UNDEFINED,1,&value);
        JS_FreeValue(app->context,value);net_job_release(job);if(!result_ok(app,settled))return;
    }
}
static bool service_number(JSContext *context,JSValueConst object,const char *key,double *out,double fallback,double min,double max)
{
    JSValue v=JS_GetPropertyStr(context,object,key);double n=fallback;
    bool valid=JS_IsUndefined(v) || (JS_IsNumber(v) && JS_ToFloat64(context,&n,v)==0 && isfinite(n) && floor(n)==n && n>=min && n<=max);
    JS_FreeValue(context,v);if(valid)*out=n;return valid;
}
static JSValue clock_get(JSContext *context,JSValueConst self,int argc,JSValueConst *argv)
{
    (void)self;(void)argc;(void)argv;ouse_clock_state_t state;ouse_clock_get(&state);JSValue value=JS_NewObject(context);
    JS_SetPropertyStr(context,value,"utcMs",JS_NewFloat64(context,state.utc_ms));
    JS_SetPropertyStr(context,value,"valid",JS_NewBool(context,state.valid));
    JS_SetPropertyStr(context,value,"syncedThisBoot",JS_NewBool(context,state.synced_this_boot));
    JS_SetPropertyStr(context,value,"lastSyncUptimeMs",JS_NewFloat64(context,state.last_sync_uptime_ms));
    JS_SetPropertyStr(context,value,"source",JS_NewString(context,"system-wall-clock"));return value;
}
static JSValue alarms_api(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int operation)
{
    (void)self;instance_t *app=from_js(context);
    if(operation==4){
        JSValue array=JS_NewArray(context);if(!app->alarms_allowed)return array;
        ouse_alarm_snapshot_t alarms[OUSE_ALARM_APP_LIMIT];unsigned count=ouse_alarm_list(app->id,alarms,OUSE_ALARM_APP_LIMIT);
        for(unsigned i=0;i<count;++i){const ouse_alarm_snapshot_t *a=&alarms[i];JSValue item=JS_NewObject(context);
#define AN(name,v) JS_SetPropertyStr(context,item,name,JS_NewFloat64(context,(double)(v)))
#define AS(name,v) JS_SetPropertyStr(context,item,name,JS_NewString(context,v))
            AS("id",a->spec.id);AS("title",a->spec.title);AS("payload",a->spec.payload);
            AN("atMs",a->spec.at_utc*1000);AN("nextMs",a->next_utc*1000);AN("lastMs",a->last_utc*1000);
            AN("weekdays",a->spec.weekdays);AN("minuteOfDay",a->spec.minute_of_day);AN("utcOffsetMinutes",a->spec.utc_offset_minutes);
            JS_SetPropertyStr(context,item,"enabled",JS_NewBool(context,a->enabled));
            JS_SetPropertyStr(context,item,"ringing",JS_NewBool(context,a->ringing));
            JS_SetPropertyStr(context,item,"missed",JS_NewBool(context,a->missed));JS_SetPropertyUint32(context,array,i,item);
#undef AN
#undef AS
        }
        return array;
    }
    if(!app->alarms_allowed || app->validation_only)return JS_FALSE;
    ouse_alarm_spec_t spec={0};unsigned snooze=5;
    if(operation==0){
        if(argc!=1 || !JS_IsObject(argv[0]) || JS_IsNull(argv[0]))return JS_ThrowTypeError(context,"alarms.set expects options");
        double at,weekdays,minute,offset;
        if(!net_string(context,argv[0],"id",spec.id,sizeof(spec.id),NULL) ||
           !net_string(context,argv[0],"title",spec.title,sizeof(spec.title),NULL) ||
           !net_string(context,argv[0],"payload",spec.payload,sizeof(spec.payload),"") ||
           !service_number(context,argv[0],"atMs",&at,0,OUSE_CLOCK_MIN_UTC*1000.0,OUSE_CLOCK_MAX_UTC*1000.0-1) ||
           !service_number(context,argv[0],"weekdays",&weekdays,0,0,127) ||
           !service_number(context,argv[0],"minuteOfDay",&minute,0,0,1439) ||
           !service_number(context,argv[0],"utcOffsetMinutes",&offset,0,-720,840)){
            if(JS_HasException(context))return JS_EXCEPTION;
            return JS_ThrowTypeError(context,"Invalid alarm options");
        }
        spec.at_utc=(int64_t)(at/1000);spec.weekdays=weekdays;spec.minute_of_day=minute;spec.utc_offset_minutes=offset;
        if(!ouse_alarm_spec_valid(&spec))return JS_ThrowRangeError(context,"Invalid alarm ID or schedule");
    }else{
        if(argc!=(operation==3 ? 2 : 1) || !copy_string(context,argv[0],spec.id,sizeof(spec.id)))return JS_ThrowTypeError(context,"Expected alarm ID");
        if(operation==3){double exact;
            if(!JS_IsNumber(argv[1]) || JS_ToFloat64(context,&exact,argv[1])<0 || !isfinite(exact) || floor(exact)!=exact || exact<1 || exact>60)return JS_ThrowRangeError(context,"Snooze minutes: 1..60");
            snooze=exact;
        }
    }
    uint32_t id=0;model_lock(app);
    esp_err_t error=app->closed || app->faulted ? ESP_ERR_INVALID_STATE : ouse_alarm_submit(&app->owner,(ouse_alarm_action_t)operation,&spec,snooze,&id);
    model_unlock(app);return error==ESP_OK ? JS_NewUint32(context,id) : JS_FALSE;
}
static JSValue media_api(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int operation)
{
    (void)self;instance_t *app=from_js(context);
    if(operation==4){
        ouse_media_state_t state;
        if(!app->media_allowed || !ouse_media_get(app->id,&state))return JS_NULL;
        JSValue value=JS_NewObject(context);
#define MN(name,v) JS_SetPropertyStr(context,value,name,JS_NewFloat64(context,(double)(v)))
#define MB(name,v) JS_SetPropertyStr(context,value,name,JS_NewBool(context,v))
        MN("taskId",state.task_id);MN("revision",state.revision);MN("positionMs",state.position_ms);MN("durationMs",state.duration_ms);MN("bufferedBytes",state.buffered_bytes);
        MB("active",state.active);MB("paused",state.paused);MB("interrupted",state.interrupted);MB("buffering",state.buffering);
        MN("errorCode",state.error);JS_SetPropertyStr(context,value,"error",JS_NewString(context,esp_err_to_name(state.error)));
        const char *status=!state.active ? state.error!=ESP_OK ? "failed" : "ended" : state.interrupted ? "interrupted" : state.paused ? "paused" : state.buffering ? "buffering" : "playing";
        JS_SetPropertyStr(context,value,"state",JS_NewString(context,status));
#undef MN
#undef MB
        return value;
    }
    if(!app->media_allowed || app->validation_only)return JS_FALSE;
    if(operation){
        if(argc)return JS_ThrowTypeError(context,"Media control expects no arguments");
        return JS_NewBool(context,(operation==3 ? ouse_media_stop(app->id) : ouse_media_pause(app->id,operation==1))==ESP_OK);
    }
    char asset[32],url[OUSE_NET_URL_MAX+1];
    if(argc!=1 || !JS_IsObject(argv[0]) || JS_IsNull(argv[0]) ||
       !net_string(context,argv[0],"asset",asset,sizeof(asset),"") || !net_string(context,argv[0],"url",url,sizeof(url),"")){
        if(JS_HasException(context))return JS_EXCEPTION;
        return JS_ThrowTypeError(context,"media.play expects {asset} or {url}");
    }
    if((!asset[0] && !url[0]) || (asset[0] && url[0]))return JS_ThrowTypeError(context,"Choose exactly one media source");
    if(url[0] && !app->network_allowed)return JS_FALSE;
    const void *data=NULL;size_t size=0;
    if(asset[0]){
        for(unsigned i=0;i<app->asset_count;++i)if(!strcmp(app->assets[i].id,asset) && app->assets[i].type==OUSE_SCRIPT_ASSET_BINARY){data=app->assets[i].data;size=app->assets[i].size;break;}
        if(!data || !size)return JS_FALSE;
    }
    uint32_t id=0;model_lock(app);
    esp_err_t error=app->closed || app->faulted ? ESP_ERR_INVALID_STATE : ouse_media_play(&app->owner,url[0] ? url : NULL,data,size,&id);
    model_unlock(app);return error==ESP_OK ? JS_NewUint32(context,id) : JS_FALSE;
}
static JSValue system_cpu(JSContext *context,JSValueConst self,int argc,JSValueConst *argv)
{
    (void)self;(void)argc;(void)argv;ouse_cpu_metrics_t metrics;ouse_cpu_metrics_get(&metrics);
    JSValue result=JS_NewObject(context),cores=JS_NewArray(context);
    JS_SetPropertyStr(context,result,"method",JS_NewString(context,"freertos-own-core-idle-runtime-estimate"));
    JS_SetPropertyStr(context,result,"periodMs",JS_NewInt32(context,OUSE_CPU_SAMPLE_PERIOD_MS));
    JS_SetPropertyStr(context,result,"samplerStackBytesPerCore",JS_NewInt32(context,OUSE_CPU_SAMPLER_STACK_BYTES));
    for(unsigned i=0;i<metrics.core_count;++i){const ouse_cpu_core_t *sample=&metrics.cores[i];JSValue core=JS_NewObject(context);
        JS_SetPropertyStr(context,core,"available",JS_NewBool(context,sample->available));
        JS_SetPropertyStr(context,core,"valid",JS_NewBool(context,sample->valid));
        JS_SetPropertyStr(context,core,"busyPercent",sample->valid ? JS_NewFloat64(context,sample->busy_permille/10.0) : JS_NULL);
        JS_SetPropertyStr(context,core,"windowMs",JS_NewFloat64(context,sample->window_us/1000.0));
        JS_SetPropertyStr(context,core,"sampledUptimeMs",JS_NewFloat64(context,sample->sampled_us/1000.0));
        JS_SetPropertyUint32(context,cores,i,core);
    }
    JS_SetPropertyStr(context,result,"cores",cores);return result;
}
static JSValue system_capabilities(JSContext *context,JSValueConst self,int argc,JSValueConst *argv)
{
    (void)self;(void)argc;(void)argv;instance_t *app=from_js(context);
    JSValue object=JS_NewObject(context),network=JS_NewObject(context),granted=JS_NewObject(context);
    JS_SetPropertyStr(context,object,"coreVersion",JS_NewString(context,ouse_core_version()));
    JS_SetPropertyStr(context,object,"cpuMetrics",JS_TRUE);
    JS_SetPropertyStr(context,network,"available",JS_NewBool(context,ouse_net_available()));
    JS_SetPropertyStr(context,network,"requestBodyMax",JS_NewInt32(context,OUSE_NET_BODY_MAX));
    JS_SetPropertyStr(context,network,"responseBodyMax",JS_NewInt32(context,OUSE_NET_RESPONSE_MAX));
    JS_SetPropertyStr(context,network,"pendingPerInstance",JS_NewInt32(context,MAX_NET_PENDING));
    JS_SetPropertyStr(context,network,"headersMax",JS_NewInt32(context,OUSE_NET_HEADERS_MAX));
    JS_SetPropertyStr(context,network,"redirects",JS_FALSE);
    JS_SetPropertyStr(context,object,"network",network);
    ouse_alarm_stats_t stats;ouse_alarm_get_stats(&stats);JSValue alarms=JS_NewObject(context);
    JS_SetPropertyStr(context,alarms,"available",JS_NewBool(context,stats.ready));
    JS_SetPropertyStr(context,alarms,"persistent",JS_TRUE);
    JS_SetPropertyStr(context,alarms,"perAppMax",JS_NewInt32(context,OUSE_ALARM_APP_LIMIT));
    JS_SetPropertyStr(context,alarms,"totalMax",JS_NewInt32(context,OUSE_ALARM_CAPACITY));
    JS_SetPropertyStr(context,alarms,"powerOffWake",JS_FALSE);
    JS_SetPropertyStr(context,alarms,"automaticDst",JS_FALSE);
    JS_SetPropertyStr(context,object,"alarms",alarms);
    JSValue media=JS_NewObject(context),formats=JS_NewArray(context);
    JS_SetPropertyUint32(context,formats,0,JS_NewString(context,"wav-pcm16"));
    JS_SetPropertyStr(context,media,"formats",formats);
    JS_SetPropertyStr(context,media,"available",JS_NewBool(context,ouse_media_available()));
    JS_SetPropertyStr(context,media,"outputRateHz",JS_NewInt32(context,16000));
    JS_SetPropertyStr(context,media,"outputChannels",JS_NewInt32(context,1));
    JS_SetPropertyStr(context,media,"bufferBytes",JS_NewInt32(context,OUSE_MEDIA_BUFFER_BYTES));
    JS_SetPropertyStr(context,media,"seek",JS_FALSE);
    JS_SetPropertyStr(context,object,"media",media);
    JS_SetPropertyStr(context,granted,"network",JS_NewBool(context,app->network_allowed));
    JS_SetPropertyStr(context,granted,"background",JS_NewBool(context,app->background_allowed));
    JS_SetPropertyStr(context,granted,"notifications",JS_NewBool(context,app->notifications_allowed));
    JS_SetPropertyStr(context,granted,"alarms",JS_NewBool(context,app->alarms_allowed));
    JS_SetPropertyStr(context,granted,"media",JS_NewBool(context,app->media_allowed));
    JS_SetPropertyStr(context,object,"granted",granted);return object;
}
