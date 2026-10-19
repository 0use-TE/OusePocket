static EXT_RAM_BSS_ATTR struct {
    ouse_task_owner_t owner;
    uint32_t session,retiring,cookie;
    bool hid,busy,attached;
    unsigned head,count,dropped;
    ouse_ble_event_t events[OUSE_BLE_EVENTS_MAX];
} application;
static uint16_t hid_handles[3];
static uint8_t hid_reports[3][8];
static const uint8_t hid_lengths[]={8,2,4};
static bool hid_subscribed[3],hid_suspended;
static atomic_uint_least32_t hid_release_at;
static uint32_t next_cookie;
static const uint8_t hid_map[]={
    0x05,0x01,0x09,0x06,0xa1,0x01,0x85,0x01,0x05,0x07,0x19,0xe0,0x29,0xe7,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,
    0x95,0x01,0x75,0x08,0x81,0x01,0x95,0x06,0x75,0x08,0x15,0x00,0x26,0xe7,0x00,0x05,0x07,0x19,0x00,0x29,0xe7,0x81,0x00,0xc0,
    0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x02,0x15,0x00,0x26,0xff,0x03,0x19,0x00,0x2a,0xff,0x03,0x75,0x10,0x95,0x01,0x81,0x00,0xc0,
    0x05,0x01,0x09,0x02,0xa1,0x01,0x85,0x03,0x09,0x01,0xa1,0x00,0x05,0x09,0x19,0x01,0x29,0x05,0x15,0x00,0x25,0x01,0x95,0x05,0x75,0x01,0x81,0x02,
    0x95,0x01,0x75,0x03,0x81,0x01,0x05,0x01,0x09,0x30,0x09,0x31,0x09,0x38,0x15,0x81,0x25,0x7f,0x75,0x08,0x95,0x03,0x81,0x06,0xc0,0xc0};
static bool app_owned(const ouse_task_owner_t *owner,uint32_t session)
{return session && owner && owner->instance && application.session==session && application.owner.instance==owner->instance && !strcmp(application.owner.app_id,owner->app_id);}
static uint32_t app_session(void)
{portENTER_CRITICAL(&lock);uint32_t value=application.session;portEXIT_CRITICAL(&lock);return value;}
static bool app_central_active(void)
{portENTER_CRITICAL(&lock);bool value=(application.session || application.retiring) && !application.hid;portEXIT_CRITICAL(&lock);return value;}
static uint32_t app_callback_session(uint32_t cookie)
{portENTER_CRITICAL(&lock);uint32_t session=cookie && application.cookie==cookie && application.busy ? application.session : 0;portEXIT_CRITICAL(&lock);return session;}
static void app_event(uint32_t session,const char *type,int error,uint16_t handle,uint16_t end,unsigned properties,const ble_uuid_t *uuid,struct os_mbuf *buffer)
{
    ouse_ble_event_t event={.error=error,.handle=handle,.end=end,.properties=properties};snprintf(event.type,sizeof(event.type),"%s",type);
    if(uuid)ble_uuid_to_str(uuid,event.uuid);
    if(buffer){unsigned size=OS_MBUF_PKTLEN(buffer);event.truncated=size>sizeof(event.data);event.size=size>sizeof(event.data) ? sizeof(event.data) : size;if(os_mbuf_copydata(buffer,0,event.size,event.data)){event.error=BLE_HS_EUNKNOWN;event.size=0;}}
    portENTER_CRITICAL(&lock);
    if(session && application.session==session){
        if(application.count==OUSE_BLE_EVENTS_MAX){++application.dropped;application.head=(application.head+1)%OUSE_BLE_EVENTS_MAX;--application.count;}
        application.events[(application.head+application.count++)%OUSE_BLE_EVENTS_MAX]=event;
    }
    portEXIT_CRITICAL(&lock);
}
static void app_done(uint32_t session,int error,uint16_t handle)
{
    portENTER_CRITICAL(&lock);if(session && application.session==session){application.busy=false;application.cookie=0;}portEXIT_CRITICAL(&lock);
    app_event(session,"complete",error,handle,0,0,NULL,NULL);
}
static int app_service_result(uint16_t handle,const struct ble_gatt_error *error,const struct ble_gatt_svc *service,void *context)
{
    (void)handle;uint32_t session=app_callback_session((uint32_t)(uintptr_t)context);if(!session)return 0;
    if(error->status)app_done(session,error->status==BLE_HS_EDONE ? 0 : error->status,0);
    else app_event(session,"service",0,service->start_handle,service->end_handle,0,&service->uuid.u,NULL);
    return 0;
}
static int app_characteristic_result(uint16_t handle,const struct ble_gatt_error *error,const struct ble_gatt_chr *characteristic,void *context)
{
    (void)handle;uint32_t session=app_callback_session((uint32_t)(uintptr_t)context);if(!session)return 0;
    if(error->status)app_done(session,error->status==BLE_HS_EDONE ? 0 : error->status,0);
    else app_event(session,"characteristic",0,characteristic->val_handle,characteristic->def_handle,characteristic->properties,&characteristic->uuid.u,NULL);
    return 0;
}
static int app_descriptor_result(uint16_t handle,const struct ble_gatt_error *error,uint16_t characteristic,const struct ble_gatt_dsc *descriptor,void *context)
{
    (void)handle;(void)characteristic;uint32_t session=app_callback_session((uint32_t)(uintptr_t)context);if(!session)return 0;
    if(error->status)app_done(session,error->status==BLE_HS_EDONE ? 0 : error->status,0);
    else app_event(session,"descriptor",0,descriptor->handle,0,0,&descriptor->uuid.u,NULL);
    return 0;
}
static int app_read_result(uint16_t handle,const struct ble_gatt_error *error,struct ble_gatt_attr *attribute,void *context)
{
    (void)handle;uint32_t session=app_callback_session((uint32_t)(uintptr_t)context);if(!session)return 0;
    if(!error->status)app_event(session,"read",0,attribute->handle,0,0,NULL,attribute->om);
    app_done(session,error->status,attribute ? attribute->handle : 0);return 0;
}
static int app_write_result(uint16_t handle,const struct ble_gatt_error *error,struct ble_gatt_attr *attribute,void *context)
{(void)handle;uint32_t session=app_callback_session((uint32_t)(uintptr_t)context);if(session)app_done(session,error->status,attribute ? attribute->handle : 0);return 0;}
static bool hid_secure(void)
{
    struct ble_gap_conn_desc description;
    return connection!=BLE_HS_CONN_HANDLE_NONE && !ble_gap_conn_find(connection,&description) && description.sec_state.encrypted && description.sec_state.authenticated;
}
static int hid_send(unsigned report)
{
    uint8_t bytes[8];portENTER_CRITICAL(&lock);bool ready=hid_subscribed[report] && !hid_suspended;memcpy(bytes,hid_reports[report],sizeof(bytes));portEXIT_CRITICAL(&lock);
    if(!hid_secure() || !ready)return BLE_HS_ENOTCONN;
    struct os_mbuf *buffer=ble_hs_mbuf_from_flat(bytes,hid_lengths[report]);
    return buffer ? ble_gatts_notify_custom(connection,hid_handles[report],buffer) : BLE_HS_ENOMEM;
}
static void hid_release(void)
{
    hid_release_at=0;
    for(unsigned index=0;index<3;++index){portENTER_CRITICAL(&lock);bool held=false;for(unsigned offset=0;offset<hid_lengths[index];++offset)held|=hid_reports[index][offset]!=0;memset(hid_reports[index],0,sizeof(hid_reports[index]));portEXIT_CRITICAL(&lock);if(held)(void)hid_send(index);}
}
static int hid_access(uint16_t connection_handle,uint16_t attribute,struct ble_gatt_access_ctxt *context,void *argument)
{
    (void)connection_handle;(void)attribute;unsigned item=(unsigned)(uintptr_t)argument;
    if(context->op==BLE_GATT_ACCESS_OP_WRITE_CHR){
        uint8_t value;if(item!=5 || OS_MBUF_PKTLEN(context->om)!=1 || os_mbuf_copydata(context->om,0,1,&value) || value>1)return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        hid_release();portENTER_CRITICAL(&lock);hid_suspended=value==0;portEXIT_CRITICAL(&lock);return 0;
    }
    if(item<3){uint8_t bytes[8];portENTER_CRITICAL(&lock);memcpy(bytes,hid_reports[item],sizeof(bytes));portEXIT_CRITICAL(&lock);return os_mbuf_append(context->om,bytes,hid_lengths[item]) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;}
    const uint8_t info[]={0x11,0x01,0x00,0x02},pnp[]={0x01,0xe5,0x02,0x28,0x00,0x00,0x01};
    const void *bytes=item==3 ? (const void *)hid_map : item==4 ? (const void *)info : (const void *)pnp;
    size_t size=item==3 ? sizeof(hid_map) : item==4 ? sizeof(info) : sizeof(pnp);
    return os_mbuf_append(context->om,bytes,size) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}
static int hid_reference(uint16_t connection_handle,uint16_t attribute,struct ble_gatt_access_ctxt *context,void *argument)
{
    (void)connection_handle;(void)attribute;uint8_t reference[]={(uint8_t)(uintptr_t)argument,1};
    return os_mbuf_append(context->om,reference,sizeof(reference)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}
#define HID_READ (BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_READ_AUTHEN)
#define HID_WRITE (BLE_GATT_CHR_F_WRITE_NO_RSP|BLE_GATT_CHR_F_WRITE_ENC|BLE_GATT_CHR_F_WRITE_AUTHEN)
#define HID_REFERENCE(identifier) (struct ble_gatt_dsc_def[]){{.uuid=BLE_UUID16_DECLARE(0x2908),.att_flags=BLE_ATT_F_READ|BLE_ATT_F_READ_ENC|BLE_ATT_F_READ_AUTHEN,.access_cb=hid_reference,.arg=(void *)(uintptr_t)(identifier)},{0}}
static const struct ble_gatt_chr_def hid_characteristics[]={
    {.uuid=BLE_UUID16_DECLARE(0x2a4a),.access_cb=hid_access,.arg=(void *)4,.flags=HID_READ},
    {.uuid=BLE_UUID16_DECLARE(0x2a4b),.access_cb=hid_access,.arg=(void *)3,.flags=HID_READ},
    {.uuid=BLE_UUID16_DECLARE(0x2a4c),.access_cb=hid_access,.arg=(void *)5,.flags=HID_WRITE},
    {.uuid=BLE_UUID16_DECLARE(0x2a4d),.access_cb=hid_access,.flags=HID_READ|BLE_GATT_CHR_F_NOTIFY,.val_handle=&hid_handles[0],.descriptors=HID_REFERENCE(1)},
    {.uuid=BLE_UUID16_DECLARE(0x2a4d),.access_cb=hid_access,.arg=(void *)1,.flags=HID_READ|BLE_GATT_CHR_F_NOTIFY,.val_handle=&hid_handles[1],.descriptors=HID_REFERENCE(2)},
    {.uuid=BLE_UUID16_DECLARE(0x2a4d),.access_cb=hid_access,.arg=(void *)2,.flags=HID_READ|BLE_GATT_CHR_F_NOTIFY,.val_handle=&hid_handles[2],.descriptors=HID_REFERENCE(3)},{0}};
static const struct ble_gatt_chr_def identity_characteristics[]={
    {.uuid=BLE_UUID16_DECLARE(0x2a50),.access_cb=hid_access,.arg=(void *)6,.flags=BLE_GATT_CHR_F_READ},{0}};
static const struct ble_gatt_svc_def hid_services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=BLE_UUID16_DECLARE(0x1812),.characteristics=hid_characteristics},
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=BLE_UUID16_DECLARE(0x180a),.characteristics=identity_characteristics},{0}};
static int hid_register(void)
{int error=ble_gatts_count_cfg(hid_services);return error ? error : ble_gatts_add_svcs(hid_services);}
static void app_gap_event(struct ble_gap_event *event)
{
    uint32_t session=app_session();
    if(event->type==BLE_GAP_EVENT_DISCONNECT){
        portENTER_CRITICAL(&lock);memset(hid_subscribed,0,sizeof(hid_subscribed));memset(hid_reports,0,sizeof(hid_reports));hid_suspended=false;hid_release_at=0;portEXIT_CRITICAL(&lock);
        portENTER_CRITICAL(&lock);application.attached=false;application.busy=false;application.cookie=0;portEXIT_CRITICAL(&lock);
        app_event(session,"disconnected",event->disconnect.reason,0,0,0,NULL,NULL);
    }else if(event->type==BLE_GAP_EVENT_CONNECT && !event->connect.status){
        portENTER_CRITICAL(&lock);bool allowed=application.hid || application.busy;application.attached=session && allowed;application.busy=false;portEXIT_CRITICAL(&lock);
        if(session && !allowed)(void)ble_gap_terminate(connection,BLE_ERR_REM_USER_CONN_TERM);
        else app_event(session,"connected",0,0,0,0,NULL,NULL);
    }else if(event->type==BLE_GAP_EVENT_CONNECT)app_done(session,event->connect.status,0);
    else if(event->type==BLE_GAP_EVENT_ENC_CHANGE)app_event(session,"security",event->enc_change.status,0,0,0,NULL,NULL);
    else if(event->type==BLE_GAP_EVENT_NOTIFY_RX)app_event(session,"notification",0,event->notify_rx.attr_handle,0,0,NULL,event->notify_rx.om);
    else if(event->type==BLE_GAP_EVENT_SUBSCRIBE){for(unsigned index=0;index<3;++index)if(event->subscribe.attr_handle==hid_handles[index]){portENTER_CRITICAL(&lock);hid_subscribed[index]=event->subscribe.cur_notify;portEXIT_CRITICAL(&lock);app_event(session,"subscribed",0,hid_handles[index],0,index+1,NULL,NULL);}}
}
esp_err_t ouse_ble_app_open(const ouse_task_owner_t *owner,bool hid,uint32_t *session)
{
    if(!owner || !owner->instance || !session || !enabled || !synced || !ouse_tasks_submission_begin())return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&lock);bool existing=app_owned(owner,application.session) && application.hid==hid;
    if(existing)*session=application.session;
    bool occupied=application.session || application.retiring;portEXIT_CRITICAL(&lock);
    if(existing || occupied){ouse_tasks_submission_end();return existing ? ESP_OK : ESP_ERR_INVALID_STATE;}
    uint32_t id=0;esp_err_t error=ouse_task_create(owner,OUSE_TASK_NETWORK,"bluetooth.session",OUSE_TASK_STOP_ON_CLOSE,true,&id);
    if(!error && !ouse_task_start(id,true))error=ESP_ERR_INVALID_STATE;
    if(!error){
        portENTER_CRITICAL(&lock);
        if(application.session || application.retiring)error=ESP_ERR_INVALID_STATE;
        else{memset(&application,0,sizeof(application));application.owner=*owner;application.session=id;application.hid=hid;application.attached=connection!=BLE_HS_CONN_HANDLE_NONE;*session=id;}
        portEXIT_CRITICAL(&lock);
    }
    if(!error && !hid){(void)ble_gap_adv_stop();if(connection!=BLE_HS_CONN_HANDLE_NONE)(void)ble_gap_terminate(connection,BLE_ERR_REM_USER_CONN_TERM);}
    if(error && id)ouse_task_finish(id,error,false);
    ouse_tasks_submission_end();return error;
}
void ouse_ble_app_get(const ouse_task_owner_t *owner,ouse_ble_app_state_t *out)
{
    if(!out)return;
    portENTER_CRITICAL(&lock);bool owned=app_owned(owner,application.session);
    bool ready=owned && application.hid && state.connected && state.encrypted && state.authenticated && !hid_suspended;
    *out=(ouse_ble_app_state_t){.session=owned ? application.session : 0,.busy=(application.session || application.retiring) && !owned,.closing=application.retiring!=0,
        .keyboard_ready=ready && hid_subscribed[0],.consumer_ready=ready && hid_subscribed[1],.mouse_ready=ready && hid_subscribed[2]};
    portEXIT_CRITICAL(&lock);
}
esp_err_t ouse_ble_app_request(const ouse_task_owner_t *owner,uint32_t session,ouse_ble_app_operation_t operation,const char *address,uint8_t type,uint16_t handle,uint16_t end,const void *data,size_t size)
{
    if((unsigned)operation>OUSE_BLE_APP_PAIR || size>OUSE_BLE_DATA_MAX || (size && !data))return ESP_ERR_INVALID_ARG;
    command_t command={.operation=APPLICATION,.session=session,.app_operation=operation,.handle=handle,.end=end,.size=size};
    if(operation==OUSE_BLE_APP_CONNECT && !address_parse(address,type,&command.address))return ESP_ERR_INVALID_ARG;
    if(size)memcpy(command.data,data,size);
    portENTER_CRITICAL(&lock);bool valid=app_owned(owner,session) && (application.hid ? (operation>=OUSE_BLE_APP_KEYBOARD && operation<=OUSE_BLE_APP_RELEASE) || operation==OUSE_BLE_APP_DISCONNECT : operation<OUSE_BLE_APP_KEYBOARD || operation==OUSE_BLE_APP_PAIR);portEXIT_CRITICAL(&lock);
    return valid ? submit(command) : ESP_ERR_INVALID_STATE;
}
bool ouse_ble_app_poll(const ouse_task_owner_t *owner,uint32_t session,ouse_ble_event_t *out,unsigned *dropped)
{
    if(!out || !dropped)return false;
    portENTER_CRITICAL(&lock);bool found=app_owned(owner,session) && application.count;
    if(found){*out=application.events[application.head];application.head=(application.head+1)%OUSE_BLE_EVENTS_MAX;--application.count;*dropped=application.dropped;}
    portEXIT_CRITICAL(&lock);return found;
}
void ouse_ble_app_close(const ouse_task_owner_t *owner)
{
    portENTER_CRITICAL(&lock);
    if(app_owned(owner,application.session)){application.retiring=application.session;application.session=0;application.count=0;}
    portEXIT_CRITICAL(&lock);
}
static void app_process(command_t *command)
{
    portENTER_CRITICAL(&lock);bool valid=application.session==command->session && command->session;bool hid=application.hid,busy=application.busy,attached=application.attached;portEXIT_CRITICAL(&lock);
    if(!valid || ouse_task_cancel_requested(command->session))return;
    int error=0;unsigned operation=command->app_operation;
    if(busy && operation!=OUSE_BLE_APP_DISCONNECT){app_event(command->session,"complete",BLE_HS_EBUSY,command->handle,0,0,NULL,NULL);return;}
    if(!enabled || !synced)error=BLE_HS_ENOTSYNCED;
    else if(operation==OUSE_BLE_APP_SCAN){command_t scan={.operation=SCAN};process(&scan);}
    else if(operation==OUSE_BLE_APP_CONNECT){
        if(busy || connection!=BLE_HS_CONN_HANDLE_NONE)error=BLE_HS_EBUSY;
        else{portENTER_CRITICAL(&lock);application.busy=true;portEXIT_CRITICAL(&lock);command_t connect={.operation=CONNECT,.address=command->address,.plain=true};process(&connect);ouse_ble_state_t snapshot;ouse_ble_get(&snapshot);error=snapshot.error;if(!error)return;}
    }else if(operation==OUSE_BLE_APP_DISCONNECT){command_t disconnect={.operation=DISCONNECT};process(&disconnect);}
    else if(hid){
        if(operation==OUSE_BLE_APP_RELEASE)hid_release();
        else if(!hid_secure())error=BLE_HS_ENOTCONN;
        else{unsigned report=operation-OUSE_BLE_APP_KEYBOARD;unsigned length=report<3 ? hid_lengths[report] : 0;
            if(!length || command->size!=length)error=BLE_HS_EINVAL;
            else{portENTER_CRITICAL(&lock);memcpy(hid_reports[report],command->data,length);portEXIT_CRITICAL(&lock);error=hid_send(report);if(!error)hid_release_at=clock_ms()+2000;else{portENTER_CRITICAL(&lock);memset(hid_reports[report],0,8);portEXIT_CRITICAL(&lock);}}}
    }else if(!attached || connection==BLE_HS_CONN_HANDLE_NONE)error=BLE_HS_ENOTCONN;
    else if(busy)error=BLE_HS_EBUSY;
    else if(operation==OUSE_BLE_APP_PAIR){pair_until=clock_ms()+120000;plain_connection=false;error=ble_gap_security_initiate(connection);}
    else{
        portENTER_CRITICAL(&lock);application.busy=true;application.cookie=++next_cookie;if(!application.cookie)application.cookie=++next_cookie;void *context=(void *)(uintptr_t)application.cookie;portEXIT_CRITICAL(&lock);
        if(operation==OUSE_BLE_APP_SERVICES)error=ble_gattc_disc_all_svcs(connection,app_service_result,context);
        else if(operation==OUSE_BLE_APP_CHARACTERISTICS && command->handle && command->end>=command->handle)error=ble_gattc_disc_all_chrs(connection,command->handle,command->end,app_characteristic_result,context);
        else if(operation==OUSE_BLE_APP_DESCRIPTORS && command->handle && command->end>=command->handle)error=ble_gattc_disc_all_dscs(connection,command->handle,command->end,app_descriptor_result,context);
        else if(operation==OUSE_BLE_APP_READ && command->handle)error=ble_gattc_read(connection,command->handle,app_read_result,context);
        else if((operation==OUSE_BLE_APP_WRITE || operation==OUSE_BLE_APP_SUBSCRIBE) && command->handle)error=ble_gattc_write_flat(connection,command->handle,command->data,command->size,app_write_result,context);
        else error=BLE_HS_EINVAL;
        if(!error)return;
    }
    app_done(command->session,error,command->handle);
}
static void app_tick(void)
{
    uint32_t session=app_session();
    if(session && (!enabled || ouse_task_cancel_requested(session) || ouse_tasks_quiescing())){portENTER_CRITICAL(&lock);if(application.session==session){application.retiring=session;application.session=0;}portEXIT_CRITICAL(&lock);}
    portENTER_CRITICAL(&lock);uint32_t retiring=application.retiring;bool attached=application.attached;portEXIT_CRITICAL(&lock);
    if(retiring){
        hid_release();(void)ble_gap_disc_cancel();(void)ble_gap_conn_cancel();
        if(initiating)return;
        if(attached && connection!=BLE_HS_CONN_HANDLE_NONE){(void)ble_gap_terminate(connection,BLE_ERR_REM_USER_CONN_TERM);return;}
        portENTER_CRITICAL(&lock);memset(&application,0,sizeof(application));portEXIT_CRITICAL(&lock);ouse_task_finish(retiring,ESP_OK,true);(void)advertise();
    }
    if(hid_release_at && !before(hid_release_at))hid_release();
}
