#include "ouse_channels.h"
#include "esp_websocket_client.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "ping/ping_sock.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

typedef struct {
    ouse_task_owner_t owner;
    ouse_channel_kind_t kind;
    ouse_channel_config_t config;
    ouse_channel_state_t state;
    ouse_channel_event_t events[OUSE_CHANNEL_EVENTS];
    unsigned head,count;
    bool starting,closing,cancelled,binary,receiving,ping_done,ping_started;
    esp_websocket_client_handle_t websocket;
    esp_mqtt_client_handle_t mqtt;
    esp_ping_handle_t ping;
    int64_t deadline;
    size_t received,expected,frame_received;
    char topic[129];
    unsigned char incoming[OUSE_CHANNEL_BYTES];
} channel_t;
typedef struct {uint32_t id;bool subscribe,binary,retain;int qos;size_t size;char topic[129];unsigned char data[OUSE_CHANNEL_BYTES];} command_t;
static channel_t *channels;
static SemaphoreHandle_t mutex;
static QueueHandle_t commands;
static bool same(const ouse_task_owner_t *first,const ouse_task_owner_t *second)
{return first && second && first->instance==second->instance && !strcmp(first->app_id,second->app_id);}
static void lock(void){xSemaphoreTake(mutex,portMAX_DELAY);}
static void unlock(void){xSemaphoreGive(mutex);}
static channel_t *find(const ouse_task_owner_t *owner,uint32_t id)
{for(unsigned index=0;index<OUSE_CHANNEL_MAX;++index)if(channels[index].state.id==id && same(owner,&channels[index].owner))return &channels[index];return NULL;}
static void push_locked(channel_t *channel,const char *type,const char *topic,const void *data,size_t size,bool binary,double value)
{
    if(channel->count==OUSE_CHANNEL_EVENTS){++channel->state.dropped;return;}
    ouse_channel_event_t *event=&channel->events[(channel->head+channel->count++)%OUSE_CHANNEL_EVENTS];
    memset(event,0,sizeof(*event));snprintf(event->type,sizeof(event->type),"%s",type);
    if(topic)snprintf(event->topic,sizeof(event->topic),"%s",topic);
    if(size)memcpy(event->data,data,size);
    event->size=size;event->binary=binary;event->value=value;
}
static void failed_locked(channel_t *channel,esp_err_t error)
{channel->state.error=error;channel->closing=true;push_locked(channel,"error",NULL,NULL,0,false,error);}
static void websocket_event(void *opaque,esp_event_base_t base,int32_t id,void *data)
{
    (void)base;channel_t *channel=opaque;esp_websocket_event_data_t *event=data;
    lock();
    if(id==WEBSOCKET_EVENT_CONNECTED){channel->state.connected=true;push_locked(channel,"connected",NULL,NULL,0,false,0);}
    else if(id==WEBSOCKET_EVENT_DISCONNECTED || id==WEBSOCKET_EVENT_CLOSED){channel->state.connected=false;channel->closing=true;push_locked(channel,"disconnected",NULL,NULL,0,false,0);}
    else if(id==WEBSOCKET_EVENT_ERROR)failed_locked(channel,ESP_FAIL);
    else if(id==WEBSOCKET_EVENT_DATA && event && event->op_code<=2){
        if(event->payload_offset==0){channel->frame_received=0;if(event->op_code){channel->received=0;channel->binary=event->op_code==2;channel->receiving=true;}}
        if(!channel->receiving || event->data_len<0 || event->payload_offset<0 || event->payload_len<0 || event->payload_offset>event->payload_len || event->data_len>event->payload_len-event->payload_offset || (size_t)event->payload_offset!=channel->frame_received || (size_t)event->data_len>OUSE_CHANNEL_BYTES-channel->received){failed_locked(channel,ESP_ERR_INVALID_SIZE);}
        else{
            if(event->data_len)memcpy(channel->incoming+channel->received,event->data_ptr,event->data_len);
            channel->received+=event->data_len;channel->frame_received+=event->data_len;
            if(event->fin && event->payload_offset+event->data_len==event->payload_len){push_locked(channel,"data",NULL,channel->incoming,channel->received,channel->binary,0);channel->receiving=false;}
        }
    }
    unlock();
}
static void mqtt_event(void *opaque,esp_event_base_t base,int32_t id,void *data)
{
    (void)base;channel_t *channel=opaque;esp_mqtt_event_handle_t event=data;lock();
    if(id==MQTT_EVENT_CONNECTED){channel->state.connected=true;push_locked(channel,"connected",NULL,NULL,0,false,0);}
    else if(id==MQTT_EVENT_DISCONNECTED){channel->state.connected=false;channel->closing=true;push_locked(channel,"disconnected",NULL,NULL,0,false,0);}
    else if(id==MQTT_EVENT_ERROR)failed_locked(channel,ESP_FAIL);
    else if(id==MQTT_EVENT_PUBLISHED || id==MQTT_EVENT_SUBSCRIBED)push_locked(channel,id==MQTT_EVENT_PUBLISHED ? "published" : "subscribed",NULL,NULL,0,false,event->msg_id);
    else if(id==MQTT_EVENT_DATA){
        if(event->current_data_offset==0){
            channel->received=0;channel->expected=event->total_data_len;channel->receiving=true;channel->topic[0]=0;
            if(event->topic_len<0 || event->topic_len>=sizeof(channel->topic))channel->receiving=false;
            else {memcpy(channel->topic,event->topic,event->topic_len);channel->topic[event->topic_len]=0;}
        }
        if(!channel->receiving || event->data_len<0 || event->current_data_offset<0 || (size_t)event->current_data_offset!=channel->received || channel->expected>OUSE_CHANNEL_BYTES || (size_t)event->data_len>OUSE_CHANNEL_BYTES-channel->received)failed_locked(channel,ESP_ERR_INVALID_SIZE);
        else{
            if(event->data_len)memcpy(channel->incoming+channel->received,event->data,event->data_len);
            channel->received+=event->data_len;
            if(channel->received==channel->expected){push_locked(channel,"data",channel->topic,channel->incoming,channel->received,true,0);channel->receiving=false;}
        }
    }
    unlock();
}
static void ping_success(esp_ping_handle_t handle,void *opaque)
{
    channel_t *channel=opaque;uint32_t elapsed=0;esp_ping_get_profile(handle,ESP_PING_PROF_TIMEGAP,&elapsed,sizeof(elapsed));
    lock();push_locked(channel,"reply",NULL,NULL,0,false,elapsed);unlock();
}
static void ping_timeout(esp_ping_handle_t handle,void *opaque)
{(void)handle;lock();push_locked(opaque,"timeout",NULL,NULL,0,false,0);unlock();}
static void ping_end(esp_ping_handle_t handle,void *opaque)
{
    uint32_t received=0;esp_ping_get_profile(handle,ESP_PING_PROF_REPLY,&received,sizeof(received));
    channel_t *channel=opaque;lock();channel->ping_done=true;channel->closing=true;if(!received)channel->state.error=ESP_ERR_TIMEOUT;push_locked(channel,"complete",NULL,NULL,0,false,received);unlock();
}
static esp_err_t start_channel(channel_t *channel)
{
    if(!ouse_task_start(channel->state.id,true))return ESP_ERR_INVALID_STATE;
    if(channel->kind==OUSE_CHANNEL_PING){
        struct addrinfo hints={.ai_family=AF_INET,.ai_socktype=SOCK_RAW},*result=NULL;
        if(getaddrinfo(channel->config.url,NULL,&hints,&result)!=0 || !result)return ESP_ERR_NOT_FOUND;
        esp_ping_config_t config=ESP_PING_DEFAULT_CONFIG();
        ip_addr_set_ip4_u32(&config.target_addr,((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr);freeaddrinfo(result);
        config.count=3;config.interval_ms=500;config.timeout_ms=1000;config.task_stack_size=4096;
        esp_ping_callbacks_t callbacks={.cb_args=channel,.on_ping_success=ping_success,.on_ping_timeout=ping_timeout,.on_ping_end=ping_end};
        esp_err_t error=esp_ping_new_session(&config,&callbacks,&channel->ping);
        if(error==ESP_OK){error=esp_ping_start(channel->ping);channel->ping_started=error==ESP_OK;}
        return error;
    }
    bool secure=channel->kind==OUSE_CHANNEL_MQTT ? !strncmp(channel->config.url,"mqtts://",8) : !strncmp(channel->config.url,"wss://",6);
    if(secure && time(NULL)<1704067200LL)return ESP_ERR_INVALID_STATE;
    if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<(secure ? 49152 : 24576))return ESP_ERR_NO_MEM;
    if(channel->kind==OUSE_CHANNEL_WEBSOCKET){
        esp_websocket_client_config_t config={.uri=channel->config.url,.disable_auto_reconnect=true,.task_stack=6144,.buffer_size=1024,.network_timeout_ms=5000,.crt_bundle_attach=secure ? esp_crt_bundle_attach : NULL,.ping_interval_sec=20};
        channel->websocket=esp_websocket_client_init(&config);if(!channel->websocket)return ESP_ERR_NO_MEM;
        esp_err_t error=esp_websocket_register_events(channel->websocket,WEBSOCKET_EVENT_ANY,websocket_event,channel);
        return error==ESP_OK ? esp_websocket_client_start(channel->websocket) : error;
    }
    esp_mqtt_client_config_t config={.broker.address.uri=channel->config.url,.broker.verification.crt_bundle_attach=secure ? esp_crt_bundle_attach : NULL,
        .credentials.username=channel->config.username[0] ? channel->config.username : NULL,.credentials.authentication.password=channel->config.password[0] ? channel->config.password : NULL,
        .credentials.client_id=channel->config.client_id[0] ? channel->config.client_id : NULL,.network.disable_auto_reconnect=true,.network.timeout_ms=5000,
        .task.stack_size=6144,.buffer.size=1024,.outbox.limit=8192};
    channel->mqtt=esp_mqtt_client_init(&config);if(!channel->mqtt)return ESP_ERR_NO_MEM;
    esp_err_t error=esp_mqtt_client_register_event(channel->mqtt,ESP_EVENT_ANY_ID,mqtt_event,channel);
    return error==ESP_OK ? esp_mqtt_client_start(channel->mqtt) : error;
}
static void finish_channel(channel_t *channel)
{
    if(channel->websocket){esp_websocket_client_stop(channel->websocket);if(esp_websocket_client_destroy(channel->websocket)!=ESP_OK)return;channel->websocket=NULL;}
    if(channel->mqtt){esp_mqtt_client_stop(channel->mqtt);if(esp_mqtt_client_destroy(channel->mqtt)!=ESP_OK)return;channel->mqtt=NULL;}
    if(channel->ping){
        esp_ping_stop(channel->ping);lock();bool waiting=channel->ping_started && !channel->ping_done;unlock();if(waiting)return;
        esp_ping_delete_session(channel->ping);channel->ping=NULL;
    }
    lock();channel->closing=channel->starting=false;channel->state.connected=false;channel->state.closed=true;
    memset(channel->config.password,0,sizeof(channel->config.password));esp_err_t error=channel->state.error;bool cancelled=channel->cancelled;uint32_t id=channel->state.id;unlock();
    ouse_task_finish(id,error,cancelled);
}
static void worker(void *unused)
{
    (void)unused;
    for(;;){
        for(unsigned index=0;index<OUSE_CHANNEL_MAX;++index){channel_t *channel=&channels[index];
            lock();bool active=channel->state.id && !channel->state.closed;
            if(active && (ouse_task_cancel_requested(channel->state.id) || ouse_tasks_quiescing())){channel->closing=true;channel->cancelled=true;}
            bool closing=active && channel->closing,starting=active && channel->starting;unlock();
            if(closing){finish_channel(channel);continue;}
            if(starting){
                esp_err_t error=start_channel(channel);lock();channel->starting=false;if(error!=ESP_OK)failed_locked(channel,error);unlock();
            }
            lock();if(active && !channel->state.connected && esp_timer_get_time()>=channel->deadline)failed_locked(channel,ESP_ERR_TIMEOUT);unlock();
        }
        command_t *command=NULL;
        if(xQueueReceive(commands,&command,pdMS_TO_TICKS(50))!=pdTRUE)continue;
        channel_t *channel=NULL;
        lock();for(unsigned index=0;index<OUSE_CHANNEL_MAX;++index)if(channels[index].state.id==command->id && channels[index].state.connected && !channels[index].closing){channel=&channels[index];break;}unlock();
        if(channel){int result=-1;
            if(channel->mqtt)result=command->subscribe ? esp_mqtt_client_subscribe(channel->mqtt,command->topic,command->qos) : esp_mqtt_client_enqueue(channel->mqtt,command->topic,(const char *)command->data,command->size,command->qos,command->retain,true);
            else if(channel->websocket)result=command->binary ? esp_websocket_client_send_bin(channel->websocket,(const char *)command->data,command->size,pdMS_TO_TICKS(1000)) : esp_websocket_client_send_text(channel->websocket,(const char *)command->data,command->size,pdMS_TO_TICKS(1000));
            if(result<0){lock();push_locked(channel,"error",NULL,NULL,0,false,ESP_FAIL);unlock();}
        }
        free(command);
    }
}
esp_err_t ouse_channels_init(void)
{
    if(channels)return ESP_OK;
    channels=heap_caps_calloc(OUSE_CHANNEL_MAX,sizeof(*channels),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);mutex=xSemaphoreCreateMutex();commands=xQueueCreate(8,sizeof(command_t *));
    if(channels && mutex && commands && xTaskCreateWithCaps(worker,"ouse_channels",8192,NULL,2,NULL,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS)return ESP_OK;
    free(channels);channels=NULL;if(mutex)vSemaphoreDelete(mutex);mutex=NULL;if(commands)vQueueDelete(commands);commands=NULL;return ESP_ERR_NO_MEM;
}
esp_err_t ouse_channel_open(const ouse_task_owner_t *owner,ouse_channel_kind_t kind,const ouse_channel_config_t *config,uint32_t *id)
{
    if(!channels || !owner || !owner->instance || !config || !id || (unsigned)kind>OUSE_CHANNEL_PING || ouse_tasks_quiescing())return ESP_ERR_INVALID_STATE;
    size_t length=strnlen(config->url,sizeof(config->url));if(!length || length==sizeof(config->url))return ESP_ERR_INVALID_ARG;
    if(strnlen(config->username,sizeof(config->username))==sizeof(config->username) || strnlen(config->password,sizeof(config->password))==sizeof(config->password) || strnlen(config->client_id,sizeof(config->client_id))==sizeof(config->client_id))return ESP_ERR_INVALID_ARG;
    if(kind==OUSE_CHANNEL_WEBSOCKET ? strncmp(config->url,"ws://",5) && strncmp(config->url,"wss://",6) : kind==OUSE_CHANNEL_MQTT ? strncmp(config->url,"mqtt://",7) && strncmp(config->url,"mqtts://",8) : length>253 || strpbrk(config->url,"/ :@\r\n"))return ESP_ERR_INVALID_ARG;
    if(kind!=OUSE_CHANNEL_PING){const char *authority=strstr(config->url,"://")+3;const char *end=strchr(authority,'/');if(!end)end=authority+strlen(authority);if(end==authority || memchr(authority,'@',end-authority) || strpbrk(config->url,"\r\n "))return ESP_ERR_INVALID_ARG;}
    lock();channel_t *channel=NULL;
    for(unsigned index=0;index<OUSE_CHANNEL_MAX;++index)if(!channels[index].state.id || channels[index].state.closed){channel=&channels[index];break;}
    esp_err_t error=channel ? ouse_task_create(owner,OUSE_TASK_NETWORK,kind==OUSE_CHANNEL_MQTT ? "mqtt" : kind==OUSE_CHANNEL_WEBSOCKET ? "websocket" : "ping",OUSE_TASK_STOP_ON_CLOSE,true,id) : ESP_ERR_NO_MEM;
    if(error==ESP_OK){memset(channel,0,sizeof(*channel));channel->owner=*owner;channel->kind=kind;channel->config=*config;channel->state.id=*id;channel->starting=true;channel->deadline=esp_timer_get_time()+15000000;}
    unlock();return error;
}
static esp_err_t enqueue(const ouse_task_owner_t *owner,uint32_t id,const char *topic,const void *data,size_t size,bool binary,int qos,bool retain,bool subscribe)
{
    if(!channels || !id || size>OUSE_CHANNEL_BYTES || (size && !data) || !topic || strlen(topic)>128 || qos<0 || qos>1)return ESP_ERR_INVALID_ARG;
    lock();channel_t *channel=find(owner,id);bool accepted=channel && channel->state.connected && !channel->closing && channel->kind!=OUSE_CHANNEL_PING;
    if(channel && channel->kind==OUSE_CHANNEL_MQTT && (!topic[0] || (!subscribe && strpbrk(topic,"+#"))))accepted=false;
    if(subscribe && channel && channel->kind!=OUSE_CHANNEL_MQTT)accepted=false;
    unlock();
    if(!accepted)return ESP_ERR_INVALID_STATE;
    command_t *command=heap_caps_calloc(1,sizeof(*command),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!command)return ESP_ERR_NO_MEM;
    *command=(command_t){.id=id,.size=size,.binary=binary,.qos=qos,.retain=retain,.subscribe=subscribe};strcpy(command->topic,topic);if(size)memcpy(command->data,data,size);
    if(xQueueSend(commands,&command,0)==pdTRUE)return ESP_OK;
    free(command);return ESP_ERR_NO_MEM;
}
esp_err_t ouse_channel_send(const ouse_task_owner_t *owner,uint32_t id,const char *topic,const void *data,size_t size,bool binary,int qos,bool retain)
{return enqueue(owner,id,topic,data,size,binary,qos,retain,false);}
esp_err_t ouse_channel_subscribe(const ouse_task_owner_t *owner,uint32_t id,const char *topic,int qos)
{return enqueue(owner,id,topic,NULL,0,false,qos,false,true);}
bool ouse_channel_get(const ouse_task_owner_t *owner,uint32_t id,ouse_channel_state_t *state)
{if(!channels || !state)return false;lock();channel_t *channel=find(owner,id);if(channel)*state=channel->state;unlock();return channel!=NULL;}
bool ouse_channel_poll(const ouse_task_owner_t *owner,uint32_t id,ouse_channel_event_t *event)
{
    if(!channels || !event)return false;
    lock();channel_t *channel=find(owner,id);bool found=channel && channel->count;
    if(found){*event=channel->events[channel->head];channel->head=(channel->head+1)%OUSE_CHANNEL_EVENTS;--channel->count;}unlock();return found;
}
bool ouse_channel_close(const ouse_task_owner_t *owner,uint32_t id)
{if(!channels)return false;lock();channel_t *channel=find(owner,id);if(channel && !channel->state.closed){channel->closing=true;channel->cancelled=true;}unlock();return channel!=NULL;}
void ouse_channels_close_owner(const ouse_task_owner_t *owner)
{if(!channels)return;lock();for(unsigned index=0;index<OUSE_CHANNEL_MAX;++index)if(same(owner,&channels[index].owner) && !channels[index].state.closed){channels[index].closing=true;channels[index].cancelled=true;}unlock();}
