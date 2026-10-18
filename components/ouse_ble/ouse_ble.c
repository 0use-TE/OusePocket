#include "ouse_ble.h"
#include "ouse_ble_app.h"
#include "ouse_tasks.h"
#include "esp_hosted.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/ble_gatt.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

enum {ENABLE,SCAN,DISCOVER,CONNECT,DISCONNECT,FORGET,CONFIRM,APPLICATION};
typedef struct {int operation;bool enabled,temporary,plain;uint32_t pair_id,session;ble_addr_t address;ouse_ble_app_operation_t app_operation;uint16_t handle,end;unsigned size;uint8_t data[OUSE_BLE_DATA_MAX];} command_t;
static QueueHandle_t commands;
static esp_err_t (*radio_prepare)(void);
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static ouse_ble_state_t state;
static bool initialized;
static atomic_bool synced,enabled,initiating,secure_connect,plain_connection;
static uint8_t own_type;
static atomic_uint_least16_t connection=BLE_HS_CONN_HANDLE_NONE;
static atomic_uint_least32_t pair_until,prompt_until,connection_since;
static uint32_t next_pair_id;
static char echo[33]="OusePocket";
static int gap_event(struct ble_gap_event *event,void *context);
static void app_gap_event(struct ble_gap_event *event);
static void app_process(command_t *command);
static void app_tick(void);
static int hid_register(void);
static bool app_central_active(void);
static void hid_release(void);
extern void ble_store_config_init(void);
static void changed(void){++state.revision;}
static uint32_t clock_ms(void){return (uint32_t)(esp_timer_get_time()/1000);}
static bool before(uint32_t deadline){return deadline && (int32_t)(deadline-clock_ms())>0;}
static void address_text(const ble_addr_t *address,char *out)
{snprintf(out,18,"%02X:%02X:%02X:%02X:%02X:%02X",address->val[5],address->val[4],address->val[3],address->val[2],address->val[1],address->val[0]);}
static bool address_parse(const char *text,uint8_t type,ble_addr_t *out)
{
    unsigned values[6];int consumed=0;
    if(!text || strlen(text)!=17 || type>3 || sscanf(text,"%2x:%2x:%2x:%2x:%2x:%2x%n",&values[0],&values[1],&values[2],&values[3],&values[4],&values[5],&consumed)!=6 || consumed!=17)return false;
    out->type=type;for(unsigned index=0;index<6;++index)out->val[5-index]=(uint8_t)values[index];return true;
}
static void error_set(int error)
{portENTER_CRITICAL(&lock);state.error=error;changed();portEXIT_CRITICAL(&lock);}
void ouse_ble_get(ouse_ble_state_t *out)
{if(out){portENTER_CRITICAL(&lock);*out=state;portEXIT_CRITICAL(&lock);}}
static esp_err_t submit(command_t command)
{
    if(!commands || !ouse_tasks_submission_begin())return ESP_ERR_INVALID_STATE;
    bool accepted=xQueueSend(commands,&command,0)==pdTRUE;ouse_tasks_submission_end();
    return accepted ? ESP_OK : ESP_ERR_NO_MEM;
}
esp_err_t ouse_ble_enable(bool value){return submit((command_t){.operation=ENABLE,.enabled=value});}
esp_err_t ouse_ble_scan(void){return submit((command_t){.operation=SCAN});}
esp_err_t ouse_ble_discoverable(void){return submit((command_t){.operation=DISCOVER});}
esp_err_t ouse_ble_disconnect(void){return submit((command_t){.operation=DISCONNECT});}
esp_err_t ouse_ble_confirm(uint32_t pair_id,bool accept){return submit((command_t){.operation=CONFIRM,.pair_id=pair_id,.enabled=accept});}
esp_err_t ouse_ble_connect(const char *address,uint8_t type)
{command_t command={.operation=CONNECT};if(!address_parse(address,type,&command.address))return ESP_ERR_INVALID_ARG;return submit(command);}
esp_err_t ouse_ble_forget(const char *address,uint8_t type)
{command_t command={.operation=FORGET};if(!address_parse(address,type,&command.address))return ESP_ERR_INVALID_ARG;return submit(command);}
static void bonds_refresh(void)
{
    ble_addr_t addresses[4];int count=0;
    if(ble_store_util_bonded_peers(addresses,&count,4))return;
    portENTER_CRITICAL(&lock);
    bool previous[OUSE_BLE_PEERS_MAX];unsigned old_count=state.count,old_bonds=state.bonds;
    for(unsigned index=0;index<state.count;++index)previous[index]=state.peers[index].bonded;
    for(unsigned index=0;index<state.count;++index)state.peers[index].bonded=false;
    state.bonds=(unsigned)count;
    for(int index=0;index<count;++index){
        char text[18];address_text(&addresses[index],text);unsigned slot=0;
        while(slot<state.count && (strcmp(state.peers[slot].address,text) || state.peers[slot].type!=addresses[index].type))++slot;
        if(slot==state.count && slot<OUSE_BLE_PEERS_MAX){memset(&state.peers[slot],0,sizeof(state.peers[slot]));++state.count;}
        if(slot<OUSE_BLE_PEERS_MAX){strcpy(state.peers[slot].address,text);state.peers[slot].type=addresses[index].type;state.peers[slot].bonded=true;}
    }
    bool different=old_count!=state.count || old_bonds!=state.bonds;
    for(unsigned index=0;!different && index<old_count;++index)different=previous[index]!=state.peers[index].bonded;
    if(different)changed();
    portEXIT_CRITICAL(&lock);
}
static int advertise(void)
{
    if(!enabled || !synced || connection!=BLE_HS_CONN_HANDLE_NONE || ble_gap_adv_active() || app_central_active())return 0;
    static const ble_uuid16_t hid_uuid=BLE_UUID16_INIT(0x1812);
    struct ble_hs_adv_fields fields={.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP,.name=(uint8_t *)"OusePocket",.name_len=10,.name_is_complete=1,.uuids16=(ble_uuid16_t *)&hid_uuid,.num_uuids16=1,.uuids16_is_complete=0,.appearance=0x03c1,.appearance_is_present=1};
    int error=ble_gap_adv_set_fields(&fields);
    if(!error){struct ble_gap_adv_params parameters={.conn_mode=BLE_GAP_CONN_MODE_UND,.disc_mode=BLE_GAP_DISC_MODE_GEN};error=ble_gap_adv_start(own_type,NULL,BLE_HS_FOREVER,&parameters,gap_event,NULL);}
    portENTER_CRITICAL(&lock);state.advertising=!error;state.error=error;changed();portEXIT_CRITICAL(&lock);return error;
}
static void connected_refresh(uint16_t handle)
{
    struct ble_gap_conn_desc description;if(ble_gap_conn_find(handle,&description))return;
    char peer[18];address_text(&description.peer_id_addr,peer);
    portENTER_CRITICAL(&lock);state.connected=true;state.encrypted=description.sec_state.encrypted;state.bonded=description.sec_state.bonded;
    strcpy(state.peer,peer);changed();portEXIT_CRITICAL(&lock);
}
static void discovered(const struct ble_gap_disc_desc *description)
{
    struct ble_hs_adv_fields fields={0};(void)ble_hs_adv_parse_fields(&fields,description->data,description->length_data);
    char address[18],name[49]={0},service[37]={0};address_text(&description->addr,address);
    if(fields.num_uuids128)ble_uuid_to_str(&fields.uuids128[0].u,service);else if(fields.num_uuids16)ble_uuid_to_str(&fields.uuids16[0].u,service);
    size_t size=fields.name_len;if(size>48)size=48;
    if(fields.name && size){for(size_t index=0;index<size;++index)name[index]=fields.name[index]>=32 && fields.name[index]<127 ? (char)fields.name[index] : '?';}
    portENTER_CRITICAL(&lock);unsigned slot=0;
    while(slot<state.count && (strcmp(state.peers[slot].address,address) || state.peers[slot].type!=description->addr.type))++slot;
    if(slot==state.count && slot<OUSE_BLE_PEERS_MAX){memset(&state.peers[slot],0,sizeof(state.peers[slot]));++state.count;}
    if(slot<OUSE_BLE_PEERS_MAX){
        ouse_ble_peer_t *peer=&state.peers[slot];bool update=(name[0] && strcmp(peer->name,name)) || (service[0] && strcmp(peer->service,service)) || abs(peer->rssi-description->rssi)>=5 || !peer->address[0];
        strcpy(peer->address,address);peer->type=description->addr.type;peer->rssi=description->rssi;
        if(name[0])strcpy(peer->name,name);
        if(service[0])strcpy(peer->service,service);
        if(update)changed();
    }
    portEXIT_CRITICAL(&lock);
}
static int gap_event(struct ble_gap_event *event,void *context)
{
    (void)context;
    switch(event->type){
    case BLE_GAP_EVENT_CONNECT:
        portENTER_CRITICAL(&lock);state.advertising=false;state.error=event->connect.status;changed();portEXIT_CRITICAL(&lock);
        if(!event->connect.status){bool outgoing=atomic_exchange(&initiating,false);plain_connection=outgoing && !secure_connect;connection=event->connect.conn_handle;connection_since=clock_ms();connected_refresh(connection);if(outgoing && secure_connect)(void)ble_gap_security_initiate(connection);}
        else{initiating=false;(void)advertise();}
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        connection=BLE_HS_CONN_HANDLE_NONE;prompt_until=0;
        portENTER_CRITICAL(&lock);state.connected=false;state.encrypted=false;state.bonded=false;state.pair_pending=false;state.peer[0]=0;changed();portEXIT_CRITICAL(&lock);
        (void)advertise();break;
    case BLE_GAP_EVENT_DISC:discovered(&event->disc);break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        portENTER_CRITICAL(&lock);state.scanning=false;changed();portEXIT_CRITICAL(&lock);break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        portENTER_CRITICAL(&lock);state.advertising=false;changed();portEXIT_CRITICAL(&lock);break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        portENTER_CRITICAL(&lock);state.pair_pending=false;changed();portEXIT_CRITICAL(&lock);
        connected_refresh(event->enc_change.conn_handle);error_set(event->enc_change.status);break;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if(event->passkey.params.action==BLE_SM_IOACT_NUMCMP && before(pair_until)){
            prompt_until=clock_ms()+30000;
            portENTER_CRITICAL(&lock);state.pair_pending=true;state.pair_id=++next_pair_id;state.number=event->passkey.params.numcmp;changed();portEXIT_CRITICAL(&lock);
        }else{(void)ble_gap_terminate(event->passkey.conn_handle,BLE_ERR_REM_USER_CONN_TERM);}
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:return BLE_GAP_REPEAT_PAIRING_IGNORE;
    default:break;
    }
    app_gap_event(event);
    return 0;
}
static int access_link(uint16_t handle,uint16_t attribute,struct ble_gatt_access_ctxt *context,void *argument)
{
    (void)handle;(void)attribute;(void)argument;
    if(context->op==BLE_GATT_ACCESS_OP_READ_CHR)return os_mbuf_append(context->om,echo,strlen(echo)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
    if(context->op==BLE_GATT_ACCESS_OP_WRITE_CHR){
        uint16_t length=OS_MBUF_PKTLEN(context->om);if(length>32)return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if(os_mbuf_copydata(context->om,0,length,echo))return BLE_ATT_ERR_UNLIKELY;
        echo[length]=0;return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}
static const ble_uuid128_t service_uuid=BLE_UUID128_INIT(0x01,0x9d,0xc0,0xf8,0xa4,0x8f,0x21,0x9a,0x6f,0x4c,0x8e,0x3a,0,0x10,0x6b,0x7c);
static const ble_uuid128_t link_uuid=BLE_UUID128_INIT(0x01,0x9d,0xc0,0xf8,0xa4,0x8f,0x21,0x9a,0x6f,0x4c,0x8e,0x3a,1,0x10,0x6b,0x7c);
static const struct ble_gatt_chr_def characteristics[]={
    {.uuid=&link_uuid.u,.access_cb=access_link,.flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_WRITE_ENC|BLE_GATT_CHR_F_READ_AUTHEN|BLE_GATT_CHR_F_WRITE_AUTHEN}, {0}};
static const struct ble_gatt_svc_def services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&service_uuid.u,.characteristics=characteristics}, {0}};
static void reset(int reason)
{synced=false;portENTER_CRITICAL(&lock);state.ready=false;state.error=reason;state.starting=false;changed();portEXIT_CRITICAL(&lock);}
static void sync_host(void)
{
    int error=ble_hs_util_ensure_addr(0);if(!error)error=ble_hs_id_infer_auto(0,&own_type);
    ble_addr_t address={.type=own_type};if(!error)error=ble_hs_id_copy_addr(own_type,address.val,NULL);
    char text[18];address_text(&address,text);synced=!error;
    portENTER_CRITICAL(&lock);state.ready=!error;state.starting=false;state.error=error;strcpy(state.address,text);changed();portEXIT_CRITICAL(&lock);
    if(!error)(void)advertise();
}
static void host_task(void *unused){(void)unused;nimble_port_run();nimble_port_freertos_deinit();}
static int initialize(void)
{
    if(initialized)return 0;
    if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<48*1024 || heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<12*1024)return ESP_ERR_NO_MEM;
    int error=radio_prepare ? radio_prepare() : ESP_ERR_INVALID_STATE;if(error)return error;
    error=nimble_port_init();if(error)return error;
    ble_hs_cfg.sync_cb=sync_host;ble_hs_cfg.reset_cb=reset;ble_hs_cfg.store_status_cb=ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap=BLE_SM_IO_CAP_DISP_YES_NO;ble_hs_cfg.sm_bonding=1;ble_hs_cfg.sm_mitm=1;ble_hs_cfg.sm_sc=1;ble_hs_cfg.sm_sc_only=1;
    ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();ble_svc_gatt_init();error=ble_svc_gap_device_name_set("OusePocket");
    if(!error)error=ble_gatts_count_cfg(services);
    if(!error)error=ble_gatts_add_svcs(services);
    if(!error)error=hid_register();
    if(error){(void)nimble_port_deinit();return error;}
    ble_store_config_init();initialized=true;nimble_port_freertos_init(host_task);return 0;
}
static void save_enabled(bool value)
{
    nvs_handle_t handle;int error=nvs_open("ouse_ble",NVS_READWRITE,&handle);
    if(!error){error=nvs_set_u8(handle,"enabled",value);if(!error)error=nvs_commit(handle);nvs_close(handle);}
    if(error)error_set(error);
}
static void process(command_t *command)
{
    if(command->operation==APPLICATION){app_process(command);return;}
    int error=0;
    if(command->operation==ENABLE){
        enabled=command->enabled;pair_until=enabled && !command->temporary ? clock_ms()+120000 : 0;
        portENTER_CRITICAL(&lock);state.enabled=enabled;state.starting=enabled&&!synced;state.error=0;changed();portEXIT_CRITICAL(&lock);
        if(enabled){error=initialize();if(!error && synced)error=advertise();}
        else if(initialized){
            hid_release();
            (void)ble_gap_disc_cancel();(void)ble_gap_adv_stop();(void)ble_gap_conn_cancel();
            if(connection!=BLE_HS_CONN_HANDLE_NONE)(void)ble_gap_terminate(connection,BLE_ERR_REM_USER_CONN_TERM);
            portENTER_CRITICAL(&lock);state.scanning=false;state.advertising=false;state.pair_pending=false;changed();portEXIT_CRITICAL(&lock);
        }
        if(error){portENTER_CRITICAL(&lock);state.starting=false;state.enabled=false;changed();portEXIT_CRITICAL(&lock);enabled=false;}
        if(!command->temporary)save_enabled(enabled);
    }else if(!enabled || !synced)error=BLE_HS_ENOTSYNCED;
    else if(command->operation==SCAN){
        if(!ble_gap_disc_active()){
            portENTER_CRITICAL(&lock);state.count=0;changed();portEXIT_CRITICAL(&lock);bonds_refresh();
            struct ble_gap_disc_params parameters={.passive=0,.itvl=0xa0,.window=0x50,.filter_duplicates=1};
            error=ble_gap_disc(own_type,10000,&parameters,gap_event,NULL);
            portENTER_CRITICAL(&lock);state.scanning=!error;changed();portEXIT_CRITICAL(&lock);
        }
    }else if(command->operation==DISCOVER){pair_until=clock_ms()+120000;error=advertise();}
    else if(command->operation==CONNECT){
        if(connection!=BLE_HS_CONN_HANDLE_NONE)error=BLE_HS_EALREADY;
        else{(void)ble_gap_disc_cancel();(void)ble_gap_adv_stop();pair_until=clock_ms()+120000;initiating=true;secure_connect=!command->plain;
            error=ble_gap_connect(own_type,&command->address,10000,NULL,gap_event,NULL);if(error)initiating=false;}
    }else if(command->operation==DISCONNECT){
        hid_release();
        (void)ble_gap_conn_cancel();if(connection!=BLE_HS_CONN_HANDLE_NONE)error=ble_gap_terminate(connection,BLE_ERR_REM_USER_CONN_TERM);
    }else if(command->operation==FORGET){
        hid_release();(void)ble_gap_disc_cancel();(void)ble_gap_adv_stop();
        error=ble_gap_unpair(&command->address);bonds_refresh();if(connection==BLE_HS_CONN_HANDLE_NONE)(void)advertise();
    }else if(command->operation==CONFIRM){
        portENTER_CRITICAL(&lock);bool valid=state.pair_pending && state.pair_id==command->pair_id;if(valid){state.pair_pending=false;changed();}portEXIT_CRITICAL(&lock);
        if(valid){struct ble_sm_io input={.action=BLE_SM_IOACT_NUMCMP,.numcmp_accept=command->enabled && before(prompt_until)};error=ble_sm_inject_io(connection,&input);prompt_until=0;}
        else error=BLE_HS_EINVAL;
    }
    error_set(error);
}
static void worker(void *unused)
{
    (void)unused;nvs_handle_t handle;uint8_t saved=0;
    if(nvs_open("ouse_ble",NVS_READONLY,&handle)==ESP_OK){(void)nvs_get_u8(handle,"enabled",&saved);nvs_close(handle);}
    if(saved==1){command_t command={.operation=ENABLE,.enabled=true,.temporary=true};process(&command);}
    int64_t refresh_at=0;
    for(;;){
        command_t command;if(xQueueReceive(commands,&command,pdMS_TO_TICKS(200))==pdTRUE)process(&command);
        int64_t now=esp_timer_get_time();
        if(initialized && synced && now>=refresh_at){bonds_refresh();if(connection!=BLE_HS_CONN_HANDLE_NONE)connected_refresh(connection);refresh_at=now+2000000;}
        ouse_ble_state_t snapshot;ouse_ble_get(&snapshot);
        if(snapshot.pair_pending && (!before(prompt_until) || ouse_tasks_quiescing())){command_t rejection={.operation=CONFIRM,.pair_id=snapshot.pair_id};process(&rejection);}
        if(connection!=BLE_HS_CONN_HANDLE_NONE && !plain_connection && !snapshot.encrypted && !snapshot.pair_pending && (uint32_t)(clock_ms()-connection_since)>30000)
            (void)ble_gap_terminate(connection,BLE_ERR_REM_USER_CONN_TERM);
        if(ouse_tasks_quiescing() && enabled){command_t disable={.operation=ENABLE,.temporary=true};process(&disable);}
        app_tick();
    }
}
#include "ouse_ble_app.inc"
esp_err_t ouse_ble_start(esp_err_t (*prepare_radio)(void))
{
    if(!prepare_radio)return ESP_ERR_INVALID_ARG;
    if(commands)return ESP_ERR_INVALID_STATE;
    radio_prepare=prepare_radio;
    commands=xQueueCreate(6,sizeof(command_t));if(!commands)return ESP_ERR_NO_MEM;
    if(xTaskCreate(worker,"ouse_ble",6144,NULL,2,NULL)!=pdPASS){vQueueDelete(commands);commands=NULL;return ESP_ERR_NO_MEM;}
    portENTER_CRITICAL(&lock);state.available=true;changed();portEXIT_CRITICAL(&lock);return ESP_OK;
}
