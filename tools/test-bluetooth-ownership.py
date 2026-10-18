"""Exercise production BLE leases, event bounds and deferred teardown without a radio."""
from pathlib import Path
import os
import subprocess
root=Path(__file__).resolve().parents[1]
output=root/'.tools/bluetooth-ownership-check';output.mkdir(parents=True,exist_ok=True)
native=(root/'components/ouse_ble/ouse_ble_app.inc').read_text(encoding='utf-8')
service=(root/'components/ouse_ble/ouse_ble.c').read_text(encoding='utf-8')
state=native[:native.index('static uint16_t hid_handles')]
ownership=native[native.index('static bool app_owned('):native.index('static int app_service_result(')]
api=native[native.index('esp_err_t ouse_ble_app_open('):native.index('static void app_process(')]
tick=native[native.index('static void app_tick('):]
types=service[service.index('enum {ENABLE'):service.index('static QueueHandle_t')]
parse=service[service.index('static bool address_parse('):service.index('static void error_set(')]
for name,text in {'esp_err.h':'#pragma once\ntypedef int esp_err_t;\n','ouse_tasks.h':'#pragma once\n#include "esp_err.h"\n#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\ntypedef struct {char app_id[48];uint32_t instance;} ouse_task_owner_t;\n'}.items():(output/name).write_text(text,encoding='utf-8')
fixture=r'''
#include "ouse_ble_app.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define EXT_RAM_BSS_ATTR
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define BLE_HS_EUNKNOWN 20
#define BLE_HS_CONN_HANDLE_NONE 65535
#define BLE_ERR_REM_USER_CONN_TERM 19
#define OUSE_TASK_NETWORK 6
#define OUSE_TASK_STOP_ON_CLOSE 1
#define portENTER_CRITICAL(value) ((void)(value))
#define portEXIT_CRITICAL(value) ((void)(value))
typedef struct {uint8_t type,val[6];} ble_addr_t;
typedef struct {int value;} ble_uuid_t;
struct os_mbuf {unsigned size;uint8_t data[100];};
#define OS_MBUF_PKTLEN(buffer) ((buffer)->size)
static int os_mbuf_copydata(struct os_mbuf *buffer,unsigned offset,unsigned size,void *out){memcpy(out,buffer->data+offset,size);return 0;}
static void ble_uuid_to_str(const ble_uuid_t *uuid,char *out){(void)uuid;strcpy(out,"00002902-0000-1000-8000-00805f9b34fb");}
static bool enabled=true,synced=true,initiating,cancelled,quiescing;
static uint16_t connection=BLE_HS_CONN_HANDLE_NONE;
static uint32_t next_task=100,hid_release_at;
static unsigned finishes,releases,disconnects,advertisements,submitted;
static int lock;
static bool ouse_tasks_submission_begin(void){return !quiescing;}
static void ouse_tasks_submission_end(void){}
static int ouse_task_create(const ouse_task_owner_t *owner,int kind,const char *operation,int policy,bool cancellable,uint32_t *id){assert(owner->instance&&kind==6&&policy==1&&cancellable&&!strcmp(operation,"bluetooth.session"));*id=++next_task;return 0;}
static bool ouse_task_start(uint32_t id,bool cancellable){assert(id&&cancellable);return true;}
static void ouse_task_finish(uint32_t id,int error,bool stopped){assert(id);(void)error;(void)stopped;++finishes;}
static bool ouse_task_cancel_requested(uint32_t id){assert(id);return cancelled;}
static bool ouse_tasks_quiescing(void){return quiescing;}
static int ble_gap_adv_stop(void){return 0;}
static int ble_gap_disc_cancel(void){return 0;}
static int ble_gap_conn_cancel(void){return 0;}
static int ble_gap_terminate(uint16_t handle,int reason){assert(handle!=65535&&reason==19);++disconnects;return 0;}
static int advertise(void){++advertisements;return 0;}
static bool before(uint32_t deadline){(void)deadline;return false;}
static void hid_release(void){hid_release_at=0;++releases;}
'''
tests=r'''
int main(void){
 ouse_task_owner_t first={.app_id="user.first",.instance=1},second={.app_id="user.second",.instance=2},spoof={.app_id="user.spoof",.instance=1};uint32_t session=0,other=0;
 assert(!ouse_ble_app_open(&first,false,&session)&&session==101);
 assert(ouse_ble_app_open(&second,false,&other)==ESP_ERR_INVALID_STATE);
 uint8_t data[65]={1,2,3};
 assert(ouse_ble_app_request(&second,session,OUSE_BLE_APP_READ,NULL,0,1,0,NULL,0)==ESP_ERR_INVALID_STATE);
 assert(ouse_ble_app_request(&spoof,session,OUSE_BLE_APP_READ,NULL,0,1,0,NULL,0)==ESP_ERR_INVALID_STATE);
 assert(!ouse_ble_app_request(&first,session,OUSE_BLE_APP_WRITE,NULL,0,1,0,data,64)&&submitted==1);
 assert(ouse_ble_app_request(&first,session,OUSE_BLE_APP_WRITE,NULL,0,1,0,data,65)==ESP_ERR_INVALID_ARG&&submitted==1);
 assert(ouse_ble_app_request(&first,session,OUSE_BLE_APP_KEYBOARD,NULL,0,0,0,data,8)==ESP_ERR_INVALID_STATE);
 application.busy=true;application.cookie=55;assert(app_callback_session(55)==session);application.cookie=66;assert(!app_callback_session(55)&&application.busy);app_done(session,0,0);assert(!application.cookie&&!application.busy);application.count=0;
 for(unsigned index=0;index<20;++index)app_event(session,"read",0,index,0,0,NULL,NULL);
 ouse_ble_event_t event;unsigned dropped=0;assert(!ouse_ble_app_poll(&second,session,&event,&dropped));assert(ouse_ble_app_poll(&first,session,&event,&dropped)&&event.handle==4&&dropped==4);
 application.count=0;application.head=0;struct os_mbuf buffer={.size=100};memset(buffer.data,7,sizeof(buffer.data));app_event(session,"notification",0,9,0,0,NULL,&buffer);
 assert(ouse_ble_app_poll(&first,session,&event,&dropped)&&event.truncated&&event.size==64&&event.data[63]==7);
 ouse_ble_app_close(&second);assert(app_session()==session);ouse_ble_app_close(&first);assert(!app_session()&&application.retiring==session);
 assert(ouse_ble_app_request(&first,session,OUSE_BLE_APP_READ,NULL,0,1,0,NULL,0)==ESP_ERR_INVALID_STATE);
 application.attached=true;connection=7;app_tick();assert(disconnects==1&&application.retiring);
 application.attached=false;connection=65535;app_tick();assert(!application.retiring&&advertisements==1&&releases>=2);
 assert(!ouse_ble_app_open(&second,true,&other));assert(ouse_ble_app_request(&first,session,OUSE_BLE_APP_RELEASE,NULL,0,0,0,NULL,0)==ESP_ERR_INVALID_STATE);
 assert(!ouse_ble_app_request(&second,other,OUSE_BLE_APP_KEYBOARD,NULL,0,0,0,data,8));assert(ouse_ble_app_request(&second,other,OUSE_BLE_APP_READ,NULL,0,1,0,NULL,0)==ESP_ERR_INVALID_STATE);
 cancelled=true;app_tick();assert(!app_session()&&!application.retiring);quiescing=true;assert(ouse_ble_app_open(&first,false,&session)==ESP_ERR_INVALID_STATE);
 puts("PASS production BLE exclusive leases, cross-app/stale-token rejection, command copy bounds, event overflow/truncation, central-vs-HID isolation and cancellation/close release before lease reuse");
}
'''
submit='\nstatic int submit(command_t command){++submitted;assert(command.size<=64);return 0;}\n'
(output/'check.c').write_text(fixture+types+submit+state+parse+ownership+api+tick+tests,encoding='utf-8')
environment=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(root/'.tools/ouse-kernel-check/environment.cmd')],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1);environment[key]=value
compiler=Path(environment['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
subprocess.run([str(compiler),'/nologo','/std:c11','/utf-8','/O2','/MD','/UNDEBUG','/I'+str(output),'/I'+str(root/'components/ouse_ble/include'),str(output/'check.c'),'/Fe'+str(output/'check.exe')],cwd=output,env=environment,check=True)
subprocess.run([str(output/'check.exe')],cwd=output,check=True,timeout=15)
