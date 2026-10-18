static JSValue camera_api(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int operation)
{
    (void)self;instance_t *app=from_js(context);bool photos=(operation>=4 && operation<=6) || operation==9 || operation==10;
    if(photos ? !app->photos_allowed : !app->camera_allowed)return operation==0 || operation==4 ? JS_NULL : JS_FALSE;
    if(operation==0 || operation==4){
        ouse_camera_state_t state={0};if(!app->validation_only)(void)ouse_camera_get(&app->owner,&state);
        JSValue value=JS_NewObject(context);
        JS_SetPropertyStr(context,value,"running",JS_NewBool(context,state.running));
        JS_SetPropertyStr(context,value,"recording",JS_NewBool(context,state.recording));
        JS_SetPropertyStr(context,value,"playing",JS_NewBool(context,state.playing));
        JS_SetPropertyStr(context,value,"recordedMs",JS_NewUint32(context,state.recorded_ms));
        JS_SetPropertyStr(context,value,"busy",JS_NewBool(context,state.busy));
        JS_SetPropertyStr(context,value,"inUse",JS_NewBool(context,state.in_use));
        JS_SetPropertyStr(context,value,"recoveryPending",JS_NewBool(context,state.recovery_pending));
        JS_SetPropertyStr(context,value,"error",JS_NewString(context,esp_err_to_name(state.error)));
        JS_SetPropertyStr(context,value,"taskId",JS_NewUint32(context,state.task_id));
        JS_SetPropertyStr(context,value,"revision",JS_NewUint32(context,state.revision));
        JS_SetPropertyStr(context,value,"path",JS_NewString(context,state.path));
        JS_SetPropertyStr(context,value,"offset",JS_NewUint32(context,state.offset));
        JS_SetPropertyStr(context,value,"more",JS_NewBool(context,state.more));
        JSValue entries=JS_NewArray(context);
        for(unsigned index=0;index<state.count;++index){JSValue entry=JS_NewObject(context);
            JS_SetPropertyStr(context,entry,"path",JS_NewString(context,state.entries[index].path));
            JS_SetPropertyStr(context,entry,"bytes",JS_NewFloat64(context,state.entries[index].bytes));
            JS_SetPropertyStr(context,entry,"video",JS_NewBool(context,state.entries[index].video));
            JS_SetPropertyStr(context,entry,"modified",JS_NewFloat64(context,state.entries[index].modified));
            char image[16];snprintf(image,sizeof(image),"@thumb%u",index);JS_SetPropertyStr(context,entry,"image",JS_NewString(context,image));
            JS_SetPropertyUint32(context,entries,index,entry);
        }
        JS_SetPropertyStr(context,value,"entries",entries);return value;
    }
    if(app->validation_only)return JS_FALSE;
    if(operation==2){ouse_camera_stop(&app->owner);return JS_TRUE;}
    if(operation==9){ouse_photos_stop(&app->owner);return JS_TRUE;}
    if((operation==1 || operation==6 || operation==7) && ouse_memory_pressure()==OUSE_MEMORY_CRITICAL)return JS_FALSE;
    if(!photos && ouse_permissions_get(app->id,OUSE_PERMISSION_CAMERA)!=OUSE_PERMISSION_ALLOW)return JS_FALSE;
    if(operation==10 && ouse_permissions_get(app->id,OUSE_PERMISSION_DELETE)!=OUSE_PERMISSION_ALLOW)return JS_FALSE;
    if((operation==3 || operation==7 || operation==8) && !app->files_allowed)return JS_FALSE;
    char path[128]="";uint32_t offset=0;
    if(operation==5){double number;if(argc!=1 || !JS_IsNumber(argv[0]) || JS_ToFloat64(context,&number,argv[0]) || !isfinite(number) || number<0 || number>4096 || floor(number)!=number)return JS_ThrowTypeError(context,"photos.list expects an offset from 0 to 4096");offset=(uint32_t)number;}
    if((operation==6 || operation==10) && (argc!=1 || !copy_string(context,argv[0],path,sizeof(path))))return JS_ThrowTypeError(context,"Expected a Pictures media path");
    ouse_camera_operation_t action=operation==1 ? OUSE_CAMERA_START : operation==3 ? OUSE_CAMERA_CAPTURE : operation==5 ? OUSE_PHOTOS_LIST : operation==7 ? OUSE_CAMERA_RECORD : operation==8 ? OUSE_CAMERA_RECORD_STOP : operation==10 ? OUSE_PHOTOS_REMOVE : OUSE_PHOTOS_OPEN;
    uint32_t id=0;model_lock(app);esp_err_t error=app->closed || app->faulted || !app->visible ? ESP_ERR_INVALID_STATE : ouse_camera_request(&app->owner,action,path,offset,&id);model_unlock(app);
    return error==ESP_OK ? JS_NewUint32(context,id) : JS_FALSE;
}
static const char *const native_image_names[OUSE_CAMERA_IMAGES]={"@camera","@photo","@thumb0","@thumb1","@thumb2","@thumb3","@thumb4","@thumb5","@thumb6","@thumb7","@thumb8","@thumb9","@thumb10","@thumb11"};
static void native_images_pump(instance_t *app)
{
    for(unsigned image=0;image<OUSE_CAMERA_IMAGES;++image){lv_image_dsc_t *descriptor=&app->native_images[image];
        if(!descriptor->data || !ouse_camera_copy(&app->owner,image,&app->native_image_versions[image],(void *)descriptor->data,descriptor->data_size))continue;
        lv_image_cache_drop(descriptor);
        for(unsigned index=0;index<app->node_count;++index){node_t *node=&app->nodes[index];model_lock(app);bool matching=node->model.type==IMAGE && !strcmp(node->model.image,native_image_names[image]);model_unlock(app);if(matching && node->object)lv_obj_invalidate(node->object);}
    }
}
