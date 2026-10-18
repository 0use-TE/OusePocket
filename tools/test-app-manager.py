"""Compile the real application-management bridge with deterministic RTOS doubles."""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.tools/app-manager-check'
OUT.mkdir(parents=True, exist_ok=True)
vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
installation = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
environment = OUT / 'environment.cmd'
environment.write_text(f'@echo off\ncall "{installation}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n', encoding='mbcs')
env = dict(os.environ)
for line in subprocess.check_output(['cmd.exe', '/d', '/c', str(environment)], encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key, value = line.split('=', 1)
        env[key] = value
compiler = Path(env['VCToolsInstallDir']) / 'bin/Hostx64/x64/cl.exe'
(OUT / 'freertos').mkdir(exist_ok=True)
(OUT / 'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_INVALID_ARG 258\n#define ESP_ERR_INVALID_STATE 259\n#define ESP_ERR_NO_MEM 257\n')
(OUT / 'platform.h').write_text('''#pragma once
#include <stddef.h>
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
#define portMAX_DELAY 0xffffffffu
#define pdTRUE 1
QueueHandle_t xQueueCreate(unsigned,unsigned);
int xQueueSend(QueueHandle_t,const void*,unsigned);
int xQueueReceive(QueueHandle_t,void*,unsigned);
void vQueueDelete(QueueHandle_t);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t,unsigned);
int xSemaphoreGive(SemaphoreHandle_t);
void vSemaphoreDelete(SemaphoreHandle_t);
''')
for header in ('FreeRTOS.h', 'queue.h', 'semphr.h'):
    (OUT / 'freertos' / header).write_text('#include "platform.h"\n')
(OUT / 'esp_heap_caps.h').write_text('#include <stdlib.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\n#define heap_caps_calloc(count,size,caps) calloc(count,size)\n')
source = OUT / 'manager.c'
source.write_text(r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ouse_app_manager.h"
#include "platform.h"
typedef struct {unsigned size;bool full;unsigned char data[128];} queue_t;
QueueHandle_t xQueueCreate(unsigned capacity,unsigned size){assert(capacity==1 && size<=128);queue_t *queue=calloc(1,sizeof(*queue));queue->size=size;return queue;}
int xQueueSend(QueueHandle_t handle,const void *item,unsigned wait){(void)wait;queue_t *queue=handle;if(queue->full)return 0;memcpy(queue->data,item,queue->size);queue->full=true;return 1;}
int xQueueReceive(QueueHandle_t handle,void *item,unsigned wait){(void)wait;queue_t *queue=handle;if(!queue->full)return 0;memcpy(item,queue->data,queue->size);queue->full=false;return 1;}
void vQueueDelete(QueueHandle_t handle){free(handle);}
SemaphoreHandle_t xSemaphoreCreateMutex(void){int *mutex=malloc(sizeof(*mutex));*mutex=1;return mutex;}
int xSemaphoreTake(SemaphoreHandle_t handle,unsigned wait){(void)wait;int *mutex=handle;assert(*mutex==1);*mutex=0;return 1;}
int xSemaphoreGive(SemaphoreHandle_t handle){int *mutex=handle;assert(*mutex==0);*mutex=1;return 1;}
void vSemaphoreDelete(SemaphoreHandle_t handle){free(handle);}
int main(void)
{
    ouse_managed_app_t entries[80]={0};
    ouse_manager_state_t state={0},catalog={.count=2,.capacity=80,.apps=entries,.free_bytes=1234};
    ouse_manager_get(&state);assert(!state.ready && state.error==ESP_ERR_INVALID_STATE);
    assert(ouse_manager_init()==ESP_OK);assert(ouse_manager_init()==ESP_OK);
    strcpy(catalog.apps[0].id,"ouse.settings");catalog.apps[0].system=true;
    strcpy(catalog.apps[1].id,"user.a");catalog.apps[1].package_bytes=1024;catalog.apps[1].data_bytes=33;
    ouse_manager_publish(&catalog);uint32_t token=0;
    assert(ouse_manager_request(OUSE_MANAGER_REFRESH,NULL,&token)==ESP_ERR_INVALID_STATE);
    ouse_manager_provider(true,false,false);
    assert(ouse_manager_request(OUSE_MANAGER_CLEAR,"ouse.settings",&token)==ESP_ERR_INVALID_ARG);
    assert(ouse_manager_request(OUSE_MANAGER_UNINSTALL,"user.missing",&token)==ESP_ERR_INVALID_ARG);
    assert(ouse_manager_request((ouse_manager_action_t)99,NULL,&token)==ESP_ERR_INVALID_ARG);
    char target[48]="user.a";
    assert(ouse_manager_request(OUSE_MANAGER_CLEAR,target,&token)==ESP_OK);strcpy(target,"user.b");
    assert(ouse_manager_pending());ouse_manager_get(&state);assert(state.busy && state.request_id==token);
    assert(ouse_manager_request(OUSE_MANAGER_REFRESH,NULL,&token)==ESP_ERR_INVALID_STATE);
    ouse_manager_request_t request;assert(ouse_manager_take(&request));assert(!strcmp(request.target,"user.a"));
    assert(!ouse_manager_take(&request));ouse_manager_complete(&request,ESP_FAIL);
    ouse_manager_get(&state);assert(!state.busy && state.error==ESP_FAIL && state.completed_id==token);
    assert(state.apps[1].package_bytes==1024 && state.apps[1].data_bytes==33 && state.free_bytes==1234);
    uint32_t revision=state.catalog_revision;
    ouse_manager_provider(true,true,false);assert(ouse_manager_request(OUSE_MANAGER_UNINSTALL,"user.a",&token)==ESP_ERR_INVALID_STATE);
    ouse_manager_provider(true,false,true);assert(ouse_manager_request(OUSE_MANAGER_REFRESH,NULL,&token)==ESP_ERR_INVALID_STATE);
    assert(ouse_manager_request(OUSE_MANAGER_RETRY,NULL,&token)==ESP_OK);
    assert(ouse_manager_take(&request));ouse_manager_complete(&request,ESP_OK);ouse_manager_get(&state);assert(state.catalog_revision==revision);
    catalog.count=80;strcpy(entries[79].id,"user.last");ouse_manager_publish(&catalog);ouse_manager_get(&state);assert(state.count==80 && !strcmp(state.apps[79].id,"user.last"));
    entries[79].id[0]=0;assert(!strcmp(state.apps[79].id,"user.last"));
    catalog.count=81;ouse_manager_publish(&catalog);ouse_manager_get(&state);assert(state.count==80);
    ouse_manager_release(&state);
    puts("PASS manager: protected system app, copied identity, serialized requests, upload/recovery admission, completion and bounded cached catalog");
}
''', encoding='utf-8')
command = [str(compiler), '/nologo', '/std:c11', '/O2', '/MD', '/utf-8', '/I'+str(OUT), '/I'+str(ROOT/'components/ouse_app_manager/include'), str(source), str(ROOT/'components/ouse_app_manager/ouse_app_manager.c'), '/Fe'+str(OUT/'manager.exe')]
result = subprocess.run(command, cwd=OUT, env=env, capture_output=True, text=True)
assert result.returncode == 0, (result.stdout, result.stderr)
subprocess.run([str(OUT/'manager.exe')], cwd=OUT, check=True, timeout=15)
