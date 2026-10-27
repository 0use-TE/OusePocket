"""Exercise the production permission store and prompt ownership without hardware."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.tools/ouse-permissions-check'
OUT.mkdir(parents=True, exist_ok=True)
sdk = r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define portMAX_DELAY 0xffffffff
typedef int nvs_handle_t;
typedef void *SemaphoreHandle_t;
static void *heap_caps_calloc(size_t count,size_t size,int caps){(void)caps;return calloc(count,size);}
static SemaphoreHandle_t xSemaphoreCreateMutex(void){return (void *)1;}
static int held;
static void xSemaphoreTake(void *handle,unsigned wait){(void)handle;(void)wait;assert(!held);held=1;}
static void xSemaphoreGive(void *handle){(void)handle;assert(held);held=0;}
static unsigned char persisted[128];static size_t persisted_size;static char persisted_key[16];static bool disk_failure;
static int nvs_open(const char *name,int mode,int *handle){assert(!strcmp(name,"ouse_perms"));(void)mode;*handle=1;return disk_failure ? ESP_FAIL : ESP_OK;}
static int nvs_get_blob(int handle,const char *key,void *out,size_t *size){(void)handle;if(!persisted_size||strcmp(key,persisted_key))return ESP_ERR_NVS_NOT_FOUND;assert(*size>=persisted_size);memcpy(out,persisted,persisted_size);*size=persisted_size;return ESP_OK;}
static int nvs_set_blob(int handle,const char *key,const void *value,size_t size){(void)handle;assert(size<=sizeof(persisted));memcpy(persisted,value,size);persisted_size=size;strcpy(persisted_key,key);return ESP_OK;}
static int nvs_erase_key(int handle,const char *key){(void)handle;if(strcmp(key,persisted_key))return ESP_ERR_NVS_NOT_FOUND;persisted_size=0;return ESP_OK;}
static int nvs_commit(int handle){(void)handle;return ESP_OK;}
static void nvs_close(int handle){(void)handle;}
'''
for name in ['esp_err.h', 'nvs.h', 'esp_heap_caps.h', 'freertos/FreeRTOS.h', 'freertos/semphr.h']:
    path = OUT / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('#include "sdk.h"\n')
(OUT / 'sdk.h').write_text(sdk)
source = r'''
#include "sdk.h"
#include "PERMISSIONS_SOURCE"
#include <stdio.h>
int main(void){
 assert(ouse_permissions_get("user.camera",OUSE_PERMISSION_CAMERA)==OUSE_PERMISSION_DENY);
 saved_t legacy={.app_id="user.legacy"};legacy.values[0]=OUSE_PERMISSION_ALLOW;legacy.values[1]=OUSE_PERMISSION_DENY;legacy.values[2]=OUSE_PERMISSION_ALLOW;
 memcpy(persisted,&legacy,51);persisted_size=51;key_for(legacy.app_id,persisted_key);
 assert(ouse_permissions_init()==ESP_OK);
 assert(ouse_permissions_get("user.legacy",OUSE_PERMISSION_MICROPHONE)==OUSE_PERMISSION_ALLOW);
 assert(ouse_permissions_get("user.legacy",OUSE_PERMISSION_CAMERA)==OUSE_PERMISSION_DENY);
 assert(ouse_permissions_get("user.legacy",OUSE_PERMISSION_BLUETOOTH)==OUSE_PERMISSION_ASK);
 assert(ouse_permissions_get("user.legacy",OUSE_PERMISSION_HID)==OUSE_PERMISSION_ASK);
 assert(ouse_permissions_set("user.legacy",OUSE_PERMISSION_BLUETOOTH,OUSE_PERMISSION_ALLOW)==ESP_OK&&persisted_size==sizeof(saved_t));
 persisted_size=0;revision=0;
 assert(ouse_permissions_init()==ESP_OK);
 assert(ouse_permissions_get("user.camera",OUSE_PERMISSION_CAMERA)==OUSE_PERMISSION_ASK);
 assert(ouse_permissions_set("ouse.settings",OUSE_PERMISSION_CAMERA,OUSE_PERMISSION_ALLOW)==ESP_ERR_INVALID_ARG);
 assert(ouse_permissions_set("user.camera",OUSE_PERMISSION_CAMERA,OUSE_PERMISSION_ALLOW)==ESP_OK);
 assert(ouse_permissions_get("user.camera",OUSE_PERMISSION_CAMERA)==OUSE_PERMISSION_ALLOW);
 assert(ouse_permissions_get("user.camera",OUSE_PERMISSION_MICROPHONE)==OUSE_PERMISSION_ASK);
 assert(ouse_permissions_get("user.other",OUSE_PERMISSION_CAMERA)==OUSE_PERMISSION_ASK);
 assert(ouse_permissions_set("user.camera",OUSE_PERMISSION_CAMERA,OUSE_PERMISSION_DENY)==ESP_OK);
 assert(ouse_permissions_revision()==2);
 disk_failure=true;assert(ouse_permissions_set("user.camera",OUSE_PERMISSION_CAMERA,OUSE_PERMISSION_ALLOW)==ESP_FAIL);
 assert(ouse_permissions_get("user.camera",OUSE_PERMISSION_CAMERA)==OUSE_PERMISSION_DENY);disk_failure=false;
 assert(ouse_permissions_purge("user.camera")==ESP_OK);assert(!persisted_size);
 assert(ouse_permissions_get("user.camera",OUSE_PERMISSION_CAMERA)==OUSE_PERMISSION_ASK);
 assert(ouse_permissions_request("user.camera",5,OUSE_PERMISSION_CAMERA)==ESP_OK);
 assert(ouse_permissions_request("user.camera",5,OUSE_PERMISSION_CAMERA)==ESP_OK);assert(prompt_count==1);
 ouse_permission_prompt_t prompt;assert(ouse_permissions_next(&prompt));assert(prompt.instance==5 && !strcmp(prompt.app_id,"user.camera"));
 assert(ouse_permissions_prompt_current(5));ouse_permissions_cancel(5);assert(!ouse_permissions_prompt_current(5));
 for(unsigned index=1;index<=8;++index)assert(ouse_permissions_request("user.camera",index,OUSE_PERMISSION_CAMERA)==ESP_OK);
 assert(ouse_permissions_request("user.camera",9,OUSE_PERMISSION_CAMERA)==ESP_ERR_NO_MEM);
 ouse_permissions_cancel(4);assert(ouse_permissions_request("user.camera",9,OUSE_PERMISSION_CAMERA)==ESP_OK);
 puts("PASS permissions: default ask, isolated grants, revocation, persistence failure, purge, prompt dedup/bounds and closed-instance invalidation");
}
'''.replace('PERMISSIONS_SOURCE', (ROOT / 'components/ouse_system/ouse_permissions.c').as_posix())
(OUT / 'check.c').write_text(source)
runtime=(ROOT/'components/ouse_runtime/ouse_runtime.c').read_text(encoding='utf-8')
enforcer=runtime[runtime.index('static void enforce_permissions('):runtime.index('static void publish_viewport_size(')]
assert 'enforce_permissions(app);' in runtime[runtime.index('static void ui_pump('):runtime.index('// Worker functions below')]
test_types=r'''
typedef struct {unsigned instance;} test_owner_t;
typedef struct {uint32_t permission_revision,permission_checked_at,microphone_task,bluetooth_session;bool bluetooth_hid;char id[48];test_owner_t owner;} instance_t;
static unsigned camera_stops,recorder_stops,capture_cancels;
static uint32_t tick;
static uint32_t lv_tick_get(void){return tick;}
static void ouse_ble_app_close(const test_owner_t *owner){assert(owner->instance==42);}
static void model_lock(instance_t *app){(void)app;}
static void model_unlock(instance_t *app){(void)app;}
static void ouse_camera_stop(const test_owner_t *owner){assert(owner->instance==42);++camera_stops;}
static bool ouse_recorder_stop(const test_owner_t *owner){assert(owner->instance==42);++recorder_stops;return true;}
static int ouse_task_cancel(const test_owner_t *owner,uint32_t task,const char *reason){assert(owner->instance==42&&task==123&&!strcmp(reason,"permission_revoked"));++capture_cancels;return ESP_OK;}
'''
checks=r'''
 assert(ouse_permissions_set("user.camera",OUSE_PERMISSION_CAMERA,OUSE_PERMISSION_ALLOW)==ESP_OK);
 assert(ouse_permissions_set("user.camera",OUSE_PERMISSION_MICROPHONE,OUSE_PERMISSION_ALLOW)==ESP_OK);
 instance_t app={.id="user.camera",.owner={.instance=42},.microphone_task=123};enforce_permissions(&app);
 assert(!camera_stops&&!recorder_stops&&!capture_cancels);
 assert(ouse_permissions_set("user.camera",OUSE_PERMISSION_MICROPHONE,OUSE_PERMISSION_DENY)==ESP_OK);enforce_permissions(&app);
 assert(!camera_stops&&recorder_stops==1&&capture_cancels==1);enforce_permissions(&app);assert(recorder_stops==1);
 app.microphone_task=123;tick=100;enforce_permissions(&app);assert(recorder_stops==2&&capture_cancels==2&&!app.microphone_task);
 assert(ouse_permissions_set("user.camera",OUSE_PERMISSION_CAMERA,OUSE_PERMISSION_DENY)==ESP_OK);enforce_permissions(&app);assert(camera_stops==1);
 puts("PASS idle/background permission enforcement runs on the UI pump, stops only owned camera/recorder/capture and does not require a runnable JS worker");
'''
source=source.replace('int main(void){',test_types+enforcer+'\nint main(void){').replace(' puts("PASS permissions:',checks+'\n puts("PASS permissions:')
(OUT / 'check.c').write_text(source)
env = dict(os.environ)
setup = ROOT / '.tools/ouse-kernel-check/environment.cmd'
for line in subprocess.check_output(['cmd.exe', '/d', '/c', str(setup)], encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key, value = line.split('=', 1)
        env[key] = value
compiler = Path(env['VCToolsInstallDir']) / 'bin/Hostx64/x64/cl.exe'
result = subprocess.run([str(compiler), '/nologo', '/std:c11', '/O2', '/MD', '/utf-8', '/I'+str(OUT),
    '/I'+str(ROOT/'components/ouse_system/include'), str(OUT/'check.c'), '/Fe'+str(OUT/'check.exe')], cwd=OUT, env=env, capture_output=True, text=True,encoding='utf-8',errors='replace')
assert result.returncode == 0, (result.stdout, result.stderr)
subprocess.run([str(OUT/'check.exe')], cwd=OUT, check=True, timeout=15)
