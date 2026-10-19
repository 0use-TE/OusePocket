static bool bluetooth_number(JSContext *context,JSValueConst value,unsigned maximum,unsigned *out)
{
    double number;if(!JS_IsNumber(value) || JS_ToFloat64(context,&number,value) || !isfinite(number) || floor(number)!=number || number<0 || number>maximum)return false;
    *out=(unsigned)number;return true;
}
static JSValue bluetooth_api(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int operation)
{
    (void)self;instance_t *app=from_js(context);
    if(operation==2){ouse_ble_app_close(&app->owner);app->bluetooth_session=0;return JS_TRUE;}
    if(!app->bluetooth_allowed || app->validation_only || app->closed || app->faulted || ouse_permissions_get(app->id,OUSE_PERMISSION_BLUETOOTH)!=OUSE_PERMISSION_ALLOW)return JS_FALSE;
    if(operation==0){
        ouse_ble_state_t state;ouse_ble_get(&state);JSValue result=JS_NewObject(context),peers=JS_NewArray(context);
        JS_SetPropertyStr(context,result,"enabled",JS_NewBool(context,state.enabled));JS_SetPropertyStr(context,result,"ready",JS_NewBool(context,state.ready));
        JS_SetPropertyStr(context,result,"connected",JS_NewBool(context,state.connected));JS_SetPropertyStr(context,result,"encrypted",JS_NewBool(context,state.encrypted));
        JS_SetPropertyStr(context,result,"scanning",JS_NewBool(context,state.scanning));JS_SetPropertyStr(context,result,"error",JS_NewInt32(context,state.error));
        ouse_ble_app_state_t owned;ouse_ble_app_get(&app->owner,&owned);
        JS_SetPropertyStr(context,result,"session",JS_NewUint32(context,owned.session));
        JS_SetPropertyStr(context,result,"busy",JS_NewBool(context,owned.busy));JS_SetPropertyStr(context,result,"closing",JS_NewBool(context,owned.closing));
        JS_SetPropertyStr(context,result,"keyboardReady",JS_NewBool(context,owned.keyboard_ready));JS_SetPropertyStr(context,result,"consumerReady",JS_NewBool(context,owned.consumer_ready));JS_SetPropertyStr(context,result,"mouseReady",JS_NewBool(context,owned.mouse_ready));
        JS_SetPropertyStr(context,result,"authenticated",JS_NewBool(context,state.authenticated));JS_SetPropertyStr(context,result,"bonded",JS_NewBool(context,state.bonded));JS_SetPropertyStr(context,result,"bonds",JS_NewUint32(context,state.bonds));
        for(unsigned index=0;index<state.count;++index){const ouse_ble_peer_t *peer=&state.peers[index];JSValue value=JS_NewObject(context);
            JS_SetPropertyStr(context,value,"name",JS_NewString(context,peer->name));JS_SetPropertyStr(context,value,"address",JS_NewString(context,peer->address));
            JS_SetPropertyStr(context,value,"service",JS_NewString(context,peer->service));
            JS_SetPropertyStr(context,value,"addressType",JS_NewUint32(context,peer->type));JS_SetPropertyStr(context,value,"rssi",JS_NewInt32(context,peer->rssi));JS_SetPropertyUint32(context,peers,index,value);}
        JS_SetPropertyStr(context,result,"peers",peers);return result;
    }
    if(operation==1){
        char mode[12];if(argc!=1 || !copy_string(context,argv[0],mode,sizeof(mode)) || (strcmp(mode,"central") && strcmp(mode,"hid")))return JS_ThrowTypeError(context,"open expects central or hid");
        bool hid=!strcmp(mode,"hid");if(hid && (!app->hid_allowed || !app->worker_visible || ouse_permissions_get(app->id,OUSE_PERMISSION_HID)!=OUSE_PERMISSION_ALLOW))return JS_FALSE;
        uint32_t session=0;esp_err_t error=ouse_ble_app_open(&app->owner,hid,&session);
        if(!error){app->bluetooth_session=session;app->bluetooth_hid=hid;}return error==ESP_OK ? JS_NewUint32(context,session) : JS_FALSE;
    }
    uint32_t session=app->bluetooth_session;if(!session)return JS_FALSE;
    if(operation==3){
        ouse_ble_event_t event;unsigned dropped=0;if(!ouse_ble_app_poll(&app->owner,session,&event,&dropped))return JS_NULL;
        JSValue result=JS_NewObject(context);JS_SetPropertyStr(context,result,"type",JS_NewString(context,event.type));JS_SetPropertyStr(context,result,"uuid",JS_NewString(context,event.uuid));
        JS_SetPropertyStr(context,result,"handle",JS_NewUint32(context,event.handle));JS_SetPropertyStr(context,result,"end",JS_NewUint32(context,event.end));JS_SetPropertyStr(context,result,"properties",JS_NewUint32(context,event.properties));
        JS_SetPropertyStr(context,result,"error",JS_NewInt32(context,event.error));JS_SetPropertyStr(context,result,"dropped",JS_NewUint32(context,dropped));JS_SetPropertyStr(context,result,"truncated",JS_NewBool(context,event.truncated));
        JS_SetPropertyStr(context,result,"data",JS_NewArrayBufferCopy(context,event.data,event.size));return result;
    }
    if(operation>=13 && operation<=16 && (!app->hid_allowed || !app->worker_visible || ouse_permissions_get(app->id,OUSE_PERMISSION_HID)!=OUSE_PERMISSION_ALLOW))return JS_FALSE;
    ouse_ble_app_operation_t command=operation==4 ? OUSE_BLE_APP_SCAN : operation==5 ? OUSE_BLE_APP_CONNECT : operation==6 ? OUSE_BLE_APP_DISCONNECT : operation==7 ? OUSE_BLE_APP_SERVICES : operation==8 ? OUSE_BLE_APP_CHARACTERISTICS : operation==9 ? OUSE_BLE_APP_DESCRIPTORS : operation==10 ? OUSE_BLE_APP_READ : operation==11 ? OUSE_BLE_APP_WRITE : operation==12 ? OUSE_BLE_APP_SUBSCRIBE : operation==13 ? OUSE_BLE_APP_KEYBOARD : operation==14 ? OUSE_BLE_APP_CONSUMER : operation==15 ? OUSE_BLE_APP_MOUSE : operation==17 ? OUSE_BLE_APP_PAIR : OUSE_BLE_APP_RELEASE;
    char address[18]="";unsigned address_type=0,handle=0,end=0;uint8_t bytes[8]={0};const void *data=bytes;size_t size=0;
    if(operation==5){if(argc!=2 || !copy_string(context,argv[0],address,sizeof(address)) || !bluetooth_number(context,argv[1],3,&address_type))return JS_ThrowTypeError(context,"connect expects address, addressType");}
    else if(operation>=8 && operation<=12){
        if(argc<1 || !bluetooth_number(context,argv[0],65535,&handle) || !handle)return JS_ThrowTypeError(context,"Expected nonzero attribute handle");
        if(operation==8 || operation==9){if(argc!=2 || !bluetooth_number(context,argv[1],65535,&end) || end<handle)return JS_ThrowTypeError(context,"Expected handle range");}
        else if(operation==10){if(argc!=1)return JS_ThrowTypeError(context,"read expects handle");}
        else if(operation==11){if(argc!=2)return JS_ThrowTypeError(context,"write expects handle, ArrayBuffer");data=JS_GetArrayBuffer(context,&size,argv[1]);if(!data)return JS_EXCEPTION;if(size>OUSE_BLE_DATA_MAX)return JS_ThrowRangeError(context,"BLE write exceeds 64 bytes");}
        else{char mode[12];if(argc!=2 || !copy_string(context,argv[1],mode,sizeof(mode)) || (strcmp(mode,"notify") && strcmp(mode,"indicate") && strcmp(mode,"off")))return JS_ThrowTypeError(context,"subscribe expects CCCD handle, notify/indicate/off");bytes[0]=!strcmp(mode,"notify") ? 1 : !strcmp(mode,"indicate") ? 2 : 0;size=2;}
    }else if(operation==13){
        unsigned modifiers;if(argc!=2 || !bluetooth_number(context,argv[0],255,&modifiers) || !JS_IsArray(argv[1]))return JS_ThrowTypeError(context,"keyboard expects modifier byte, up to six keys");
        bytes[0]=modifiers;JSValue length=JS_GetPropertyStr(context,argv[1],"length");unsigned count=0;bool valid=bluetooth_number(context,length,6,&count);JS_FreeValue(context,length);
        for(unsigned index=0;valid && index<count;++index){JSValue value=JS_GetPropertyUint32(context,argv[1],index);unsigned key=0;valid=bluetooth_number(context,value,231,&key);JS_FreeValue(context,value);bytes[index+2]=key;}
        if(!valid)return JS_ThrowTypeError(context,"Invalid keyboard report");
        size=8;
    }else if(operation==14){unsigned usage;if(argc!=1 || !bluetooth_number(context,argv[0],1023,&usage))return JS_ThrowTypeError(context,"consumer expects usage 0..1023");bytes[0]=usage&255;bytes[1]=usage>>8;size=2;}
    else if(operation==15){
        unsigned buttons;if(argc!=4 || !bluetooth_number(context,argv[0],31,&buttons))return JS_ThrowTypeError(context,"mouse expects buttons, x, y, wheel");bytes[0]=buttons;
        for(unsigned index=1;index<4;++index){double number;if(!JS_IsNumber(argv[index]) || JS_ToFloat64(context,&number,argv[index]) || !isfinite(number) || floor(number)!=number || number< -127 || number>127)return JS_ThrowTypeError(context,"Mouse movement must be -127..127");bytes[index]=(uint8_t)(int8_t)number;}size=4;
    }else if(operation!=5 && argc)return JS_ThrowTypeError(context,"Unexpected arguments");
    return JS_NewBool(context,ouse_ble_app_request(&app->owner,session,command,address,address_type,handle,end,data,size)==ESP_OK);
}
