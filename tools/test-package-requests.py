"""Exercise package admission against the real native owner/task registry."""
from pathlib import Path
import os
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / '.tools/package-request-check'
out.mkdir(parents=True, exist_ok=True)
headers = {
    'esp_err.h': '#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 0x102\n#define ESP_ERR_INVALID_STATE 0x103\n#define ESP_ERR_NO_MEM 0x101\n#define ESP_ERR_NOT_FOUND 0x105\n#define ESP_ERR_NOT_SUPPORTED 0x106\n',
    'esp_timer.h': '#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n',
    'freertos/FreeRTOS.h': '#pragma once\n#include <windows.h>\ntypedef SRWLOCK portMUX_TYPE;\n#define portMUX_INITIALIZER_UNLOCKED SRWLOCK_INIT\n#define portENTER_CRITICAL(lock) AcquireSRWLockExclusive(lock)\n#define portEXIT_CRITICAL(lock) ReleaseSRWLockExclusive(lock)\n#define portMAX_DELAY 0xffffffff\n',
    'freertos/semphr.h': '#pragma once\n#include <stdbool.h>\ntypedef void *SemaphoreHandle_t;\nSemaphoreHandle_t xSemaphoreCreateMutex(void);\nvoid vSemaphoreDelete(SemaphoreHandle_t);\nint xSemaphoreTake(SemaphoreHandle_t,unsigned);\nint xSemaphoreGive(SemaphoreHandle_t);\n',
    'esp_heap_caps.h': '#pragma once\n#include <stdlib.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\nstatic void *heap_caps_calloc(size_t count,size_t size,int caps){(void)caps;return calloc(count,size);}\n',
}
for name,content in headers.items():
    target = out / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content, encoding='utf-8')
fixture = r'''
#include "ouse_packages.h"
#include "freertos/semphr.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int64_t esp_timer_get_time(void){static int64_t now;return ++now;}
SemaphoreHandle_t xSemaphoreCreateMutex(void){return calloc(1,sizeof(int));}
void vSemaphoreDelete(SemaphoreHandle_t mutex){free(mutex);}
int xSemaphoreTake(SemaphoreHandle_t mutex,unsigned timeout){(void)timeout;assert(!*(int *)mutex);*(int *)mutex=1;return 1;}
int xSemaphoreGive(SemaphoreHandle_t mutex){assert(*(int *)mutex);*(int *)mutex=0;return 1;}
int main(void){
 assert(ouse_packages_init()==ESP_OK);
 ouse_package_request_t request={.local=false,.size=100,.version_code=1};
 assert(ouse_task_owner_open("user.store",&request.owner)==ESP_OK);
 strcpy(request.location,"http://192.168.1.100:3000/files/test.bpk");strcpy(request.app_id,"user.test");memset(request.sha256,'a',64);
 uint32_t task=0;assert(ouse_packages_submit(&request,&task)==ESP_OK&&task);
 uint32_t duplicate;assert(ouse_packages_submit(&request,&duplicate)==ESP_ERR_INVALID_STATE);
 ouse_package_request_t taken;assert(ouse_packages_take(&taken)&&taken.task==task&&!ouse_packages_take(&taken));assert(ouse_task_start(task,true));
 ouse_packages_progress("downloading",20,100,"user.test");ouse_package_state_t state;ouse_packages_get(&state);assert(state.busy&&state.received==20);
 assert(ouse_task_cancel(&request.owner,task,"test")==ESP_OK&&ouse_task_cancel_requested(task));ouse_packages_finish(ESP_OK,true);ouse_packages_get(&state);assert(!state.busy&&!strcmp(state.phase,"cancelled"));
 request.size=524289;assert(ouse_packages_submit(&request,&task)==ESP_ERR_INVALID_ARG);request.size=100;
 memset(request.sha256,'a',65);assert(ouse_packages_submit(&request,&task)==ESP_ERR_INVALID_ARG);request.sha256[64]=0;
 memset(request.app_id,'a',sizeof(request.app_id));assert(ouse_packages_submit(&request,&task)==ESP_ERR_INVALID_ARG);strcpy(request.app_id,"user.test");
 strcpy(request.location,"ftp://host/test");assert(ouse_packages_submit(&request,&task)==ESP_ERR_INVALID_ARG);
 request.local=true;strcpy(request.location,"/Ouse/Downloads/test.bpk");assert(ouse_packages_submit(&request,&task)==ESP_OK);ouse_task_owner_close(&request.owner);assert(ouse_packages_take(&taken)&&!ouse_task_start(task,true));ouse_packages_finish(ESP_OK,true);
 assert(ouse_task_owner_open("user.explorer",&request.owner)==ESP_OK);ouse_tasks_quiesce(true);assert(ouse_packages_submit(&request,&task)==ESP_ERR_INVALID_STATE);ouse_tasks_quiesce(false);
 puts("PASS package requests: bounded PSRAM queue, ID/hash/size validation, exclusive admission, cancellation, owner-close and drain");return 0;
}
'''
(out / 'check.c').write_text(fixture, encoding='utf-8')
env = dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(root / '.tools/ouse-kernel-check/environment.cmd')], encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1)
        env[key]=value
compiler = Path(env['VCToolsInstallDir']) / 'bin/Hostx64/x64/cl.exe'
subprocess.run([str(compiler),'/nologo','/std:c11','/O2','/utf-8','/D_CRT_SECURE_NO_WARNINGS','/I'+str(out),'/I'+str(root/'components/ouse_tasks/include'),'/I'+str(root/'components/ouse_app_manager/include'),str(out/'check.c'),str(root/'components/ouse_tasks/ouse_tasks.c'),str(root/'components/ouse_app_manager/ouse_packages.c'),'/Fe'+str(out/'check.exe')],cwd=out,env=env,check=True)
subprocess.run([str(out/'check.exe')],cwd=out,check=True,timeout=10)
