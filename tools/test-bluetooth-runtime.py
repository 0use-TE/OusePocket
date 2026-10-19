"""Run production Bluetooth bindings with real QuickJS and bounded native doubles."""
from pathlib import Path
import os
import subprocess

root=Path(__file__).resolve().parents[1]
output=root/'.tools/bluetooth-runtime-check'
output.mkdir(parents=True,exist_ok=True)
runtime=(root/'components/ouse_runtime/ouse_runtime.c').read_text(encoding='utf-8')
binding=runtime[runtime.index('    JSValue bluetooth='):runtime.index('    JSValue camera=')]
implementation=(root/'components/ouse_runtime/ouse_runtime_bluetooth.inc').read_text(encoding='utf-8')
(output/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n',encoding='utf-8')
(output/'ouse_tasks.h').write_text('#pragma once\n#include "esp_err.h"\n#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\ntypedef struct {char app_id[48];uint32_t instance;} ouse_task_owner_t;\n',encoding='utf-8')
fixture=r'''
#include "quickjs.h"
#include "ouse_ble.h"
#include "ouse_ble_app.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
enum {OUSE_PERMISSION_BLUETOOTH,OUSE_PERMISSION_HID,OUSE_PERMISSION_ALLOW=1};
typedef struct {bool bluetooth_allowed,hid_allowed,validation_only,closed,faulted,worker_visible,bluetooth_hid;uint32_t bluetooth_session;char id[48];ouse_task_owner_t owner;} instance_t;
static bool granted[2];static unsigned opens,requests,closes;static uint8_t sent[64];static size_t sent_size;static int sent_operation;static uint16_t sent_handle;
static instance_t *from_js(JSContext *context){return JS_GetContextOpaque(context);}
static bool copy_string(JSContext *context,JSValueConst value,char *out,size_t capacity){if(!JS_IsString(value))return false;const char *text=JS_ToCString(context,value);if(!text)return false;bool valid=strlen(text)<capacity;if(valid)strcpy(out,text);JS_FreeCString(context,text);return valid;}
static int ouse_permissions_get(const char *id,int permission){(void)id;return granted[permission];}
void ouse_ble_get(ouse_ble_state_t *out){memset(out,0,sizeof(*out));out->enabled=out->ready=true;}
void ouse_ble_app_get(const ouse_task_owner_t *owner,ouse_ble_app_state_t *out){(void)owner;memset(out,0,sizeof(*out));}
esp_err_t ouse_ble_app_open(const ouse_task_owner_t *owner,bool hid,uint32_t *session){assert(owner->instance==42);(void)hid;*session=++opens;return 0;}
void ouse_ble_app_close(const ouse_task_owner_t *owner){assert(owner->instance==42);++closes;}
bool ouse_ble_app_poll(const ouse_task_owner_t *owner,uint32_t session,ouse_ble_event_t *out,unsigned *dropped){(void)owner;assert(session);memset(out,0,sizeof(*out));strcpy(out->type,"notification");out->size=2;out->data[0]=7;out->data[1]=8;*dropped=3;return true;}
esp_err_t ouse_ble_app_request(const ouse_task_owner_t *owner,uint32_t session,ouse_ble_app_operation_t operation,const char *address,uint8_t type,uint16_t handle,uint16_t end,const void *data,size_t size){assert(owner->instance==42&&session);(void)address;(void)type;(void)end;++requests;sent_operation=operation;sent_handle=handle;sent_size=size;memcpy(sent,data,size);return 0;}
'''
tests=r'''
static void evaluate(JSContext *context,const char *script){JSValue value=JS_Eval(context,script,strlen(script),"bluetooth-test.js",JS_EVAL_TYPE_GLOBAL);if(JS_IsException(value)){JSValue error=JS_GetException(context);const char *text=JS_ToCString(context,error);fprintf(stderr,"%s\n",text);JS_FreeCString(context,text);JS_FreeValue(context,error);assert(0);}assert(JS_ToBool(context,value)>0);JS_FreeValue(context,value);}
int main(void){
 JSRuntime *runtime=JS_NewRuntime();JSContext *context=JS_NewContext(runtime);instance_t app={.id="user.ble",.owner={.instance=42}};JS_SetContextOpaque(context,&app);
 JSValue api=JS_NewObject(context);
 BINDINGS
 JSValue global=JS_GetGlobalObject(context);JS_SetPropertyStr(context,global,"Ouse",api);JS_FreeValue(context,global);
 evaluate(context,"Ouse.bluetooth.open('hid')===false");assert(!opens);
 app.bluetooth_allowed=true;evaluate(context,"Ouse.bluetooth.open('central')===false");assert(!opens);
 granted[0]=true;app.worker_visible=true;evaluate(context,"Ouse.bluetooth.open('hid')===false");assert(!opens);
 app.hid_allowed=true;granted[1]=true;app.worker_visible=false;evaluate(context,"Ouse.bluetooth.open('hid')===false");assert(!opens);
 app.worker_visible=true;evaluate(context,"Ouse.bluetooth.open('hid')===1");evaluate(context,"Ouse.bluetooth.keyboard(2,[104])");assert(sent_operation==OUSE_BLE_APP_KEYBOARD&&sent_size==8&&sent[0]==2&&sent[1]==0&&sent[2]==104&&sent[7]==0);
 evaluate(context,"Ouse.bluetooth.mouse(3,-127,127,-1)");assert(sent_operation==OUSE_BLE_APP_MOUSE&&sent[0]==3&&sent[1]==129&&sent[2]==127&&sent[3]==255);
 evaluate(context,"Ouse.bluetooth.consumer(233)");assert(sent_operation==OUSE_BLE_APP_CONSUMER&&sent_size==2&&sent[0]==233&&!sent[1]);
 evaluate(context,"['keyboard','consumer'].every(name=>{try{Ouse.bluetooth[name](NaN,[]);return false}catch(error){return error instanceof TypeError}})");
 evaluate(context,"(()=>{try{Ouse.bluetooth.keyboard(0,[1,2,3,4,5,6,7]);return false}catch(error){return true}})()");
 evaluate(context,"(()=>{try{Ouse.bluetooth.mouse(0,-128,0,0);return false}catch(error){return true}})()");
 evaluate(context,"(()=>{try{Ouse.bluetooth.write(1,new ArrayBuffer(65));return false}catch(error){return error instanceof RangeError}})()");
 evaluate(context,"(()=>{try{Ouse.bluetooth.read(1.5);return false}catch(error){return true}})()");
 evaluate(context,"Ouse.bluetooth.subscribe(22,'indicate')");assert(sent_handle==22&&sent_size==2&&sent[0]==2&&!sent[1]);
 evaluate(context,"(()=>{const event=Ouse.bluetooth.poll();return event.dropped===3&&new Uint8Array(event.data)[1]===8})()");
 unsigned before=requests;granted[1]=false;evaluate(context,"Ouse.bluetooth.keyboard(0,[104])===false");assert(requests==before);
 app.closed=true;evaluate(context,"Ouse.bluetooth.scan()===false");evaluate(context,"Ouse.bluetooth.close()");assert(closes==1&&!app.bluetooth_session);
 JS_FreeContext(context);JS_FreeRuntime(runtime);puts("PASS real QuickJS BLE capability/permission/foreground gates, binary GATT input, CCCD encoding, HID keyboard/consumer/mouse packing, invalid reports and close-after-revocation");
}
'''
(output/'check.c').write_text(fixture+implementation+tests.replace('BINDINGS',binding),encoding='utf-8')
environment=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(root/'.tools/ouse-kernel-check/environment.cmd')],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1);environment[key]=value
compiler=Path(environment['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
subprocess.run([str(compiler),'/nologo','/std:c11','/utf-8','/O2','/MD','/UNDEBUG','/D_CRT_SECURE_NO_WARNINGS','/I'+str(output),'/I'+str(root/'components/ouse_ble/include'),'/I'+str(root/'managed_components/espressif__quickjs-ng/quickjs-ng'),str(output/'check.c'),str(root/'.tools/ouse-runtime-check/quickjs-native.lib'),'/Fe'+str(output/'check.exe')],cwd=output,env=environment,check=True)
subprocess.run([str(output/'check.exe')],cwd=output,check=True,timeout=20)
