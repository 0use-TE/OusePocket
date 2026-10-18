"""Production channel framing/ownership helpers with SDK event doubles."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.tools/network-channels-check'
OUT.mkdir(parents=True, exist_ok=True)
production = (ROOT/'components/ouse_net/ouse_channels.c').read_text(encoding='utf-8')
helpers = production[production.index('typedef struct {'):production.index('static esp_err_t start_channel(')]
public = production[production.index('bool ouse_channel_get('):]
prelude = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "ouse_channels.h"
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_TIMEOUT 0x107
#define portMAX_DELAY 0xffffffff
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef void *esp_websocket_client_handle_t;
typedef void *esp_mqtt_client_handle_t;
typedef void *esp_ping_handle_t;
typedef void *esp_event_base_t;
typedef struct {bool fin;unsigned char op_code;int payload_len,payload_offset,data_len;char *data_ptr;} esp_websocket_event_data_t;
typedef struct {int msg_id,total_data_len,current_data_offset,topic_len,data_len;char *topic,*data;} mqtt_event_t;
typedef mqtt_event_t *esp_mqtt_event_handle_t;
enum {WEBSOCKET_EVENT_CONNECTED=1,WEBSOCKET_EVENT_DISCONNECTED,WEBSOCKET_EVENT_CLOSED,WEBSOCKET_EVENT_ERROR,WEBSOCKET_EVENT_DATA,
 MQTT_EVENT_CONNECTED=11,MQTT_EVENT_DISCONNECTED,MQTT_EVENT_ERROR,MQTT_EVENT_PUBLISHED,MQTT_EVENT_SUBSCRIBED,MQTT_EVENT_DATA,
 ESP_PING_PROF_TIMEGAP,ESP_PING_PROF_REPLY};
static int held;
static void xSemaphoreTake(void *handle,unsigned wait){(void)handle;(void)wait;assert(!held);held=1;}
static void xSemaphoreGive(void *handle){(void)handle;assert(held);held=0;}
static int esp_ping_get_profile(void *handle,int profile,void *value,size_t size){(void)handle;(void)profile;assert(size==4);*(uint32_t *)value=42;return ESP_OK;}
'''
fixture = r'''
int main(void){
 channels=calloc(OUSE_CHANNEL_MAX,sizeof(*channels));assert(channels);channel_t *channel=&channels[0];
 channel->owner=(ouse_task_owner_t){.app_id="user.test",.instance=1};channel->state.id=42;
 ouse_task_owner_t foreign={.app_id="user.test",.instance=2};ouse_channel_state_t state;
 assert(!ouse_channel_get(&foreign,42,&state));assert(ouse_channel_get(&channel->owner,42,&state));
 esp_websocket_event_data_t frame={.fin=true,.op_code=1,.payload_len=5,.data_len=2,.data_ptr="he"};
 websocket_event(channel,NULL,WEBSOCKET_EVENT_DATA,&frame);assert(!channel->count);
 frame.payload_offset=2;frame.data_len=3;frame.data_ptr="llo";websocket_event(channel,NULL,WEBSOCKET_EVENT_DATA,&frame);
 ouse_channel_event_t event;assert(!ouse_channel_poll(&foreign,42,&event));assert(ouse_channel_poll(&channel->owner,42,&event));assert(event.size==5&&!memcmp(event.data,"hello",5)&&!event.binary);
 frame=(esp_websocket_event_data_t){.fin=false,.op_code=2,.payload_len=3,.data_len=3,.data_ptr="abc"};websocket_event(channel,NULL,WEBSOCKET_EVENT_DATA,&frame);
 frame.fin=true;frame.op_code=0;frame.data_ptr="def";websocket_event(channel,NULL,WEBSOCKET_EVENT_DATA,&frame);
 assert(ouse_channel_poll(&channel->owner,42,&event)&&event.binary&&event.size==6&&!memcmp(event.data,"abcdef",6));
 frame=(esp_websocket_event_data_t){.fin=true,.op_code=1,.payload_len=2049,.data_len=2049,.data_ptr="not-read"};websocket_event(channel,NULL,WEBSOCKET_EVENT_DATA,&frame);assert(channel->closing&&channel->state.error==ESP_ERR_INVALID_SIZE);
 channel->closing=false;channel->count=channel->head=0;
 mqtt_event_t message={.total_data_len=5,.topic="test/a",.topic_len=6,.data_len=2,.data="he"};mqtt_event(channel,NULL,MQTT_EVENT_DATA,&message);
 message.current_data_offset=2;message.topic_len=0;message.data_len=3;message.data="llo";mqtt_event(channel,NULL,MQTT_EVENT_DATA,&message);
 assert(ouse_channel_poll(&channel->owner,42,&event)&&event.size==5&&!memcmp(event.data,"hello",5)&&!strcmp(event.topic,"test/a"));
 message.current_data_offset=0;message.total_data_len=2049;message.data_len=1;mqtt_event(channel,NULL,MQTT_EVENT_DATA,&message);assert(channel->closing);
 channel->count=channel->head=0;channel->closing=false;
 for(unsigned index=0;index<OUSE_CHANNEL_EVENTS+1;++index){lock();push_locked(channel,"data",NULL,"x",1,false,0);unlock();}
 assert(channel->count==OUSE_CHANNEL_EVENTS&&channel->state.dropped==1);
 assert(!ouse_channel_close(&foreign,42));assert(!channel->closing);ouse_channels_close_owner(&channel->owner);assert(channel->closing&&channel->cancelled);
 puts("PASS channels: WS chunks/continuations, MQTT topic/chunk assembly, oversized rejection, bounded event loss and close/reopen owner isolation");
}
'''
(OUT/'check.c').write_text(prelude+helpers+public+fixture)
env = dict(os.environ)
CACHE = ROOT/'.tools/ouse-kernel-check'
for line in subprocess.check_output(['cmd.exe','/d','/c',str(CACHE/'environment.cmd')],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1)
        env[key]=value
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
command=[str(compiler),'/nologo','/std:c11','/O2','/MD','/utf-8']
command+=['/I'+str(path) for path in [CACHE,ROOT/'components/ouse_net/include',ROOT/'components/ouse_tasks/include']]
result=subprocess.run(command+[str(OUT/'check.c'),'/Fe'+str(OUT/'check.exe')],cwd=OUT,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace')
assert result.returncode==0,(result.stdout,result.stderr)
subprocess.run([str(OUT/'check.exe')],cwd=OUT,check=True,timeout=15)
overlay=ROOT/'build/ouse_security/esp_websocket_client.c'
assert 'if (result < 0 || WS_HTTP_REDIRECT(result))' in overlay.read_text(encoding='utf-8')
assert 'if (result < 0 || WS_HTTP_REDIRECT(result))' not in (ROOT/'managed_components/espressif__esp_websocket_client/esp_websocket_client.c').read_text(encoding='utf-8')
print('PASS pinned build overlay rejects redirects without changing managed sources')
