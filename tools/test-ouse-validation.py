"""Check production UI/installer validation handoff and timeout ownership, without a device."""
from pathlib import Path
import os
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/".tools/ouse-kernel-check"
env=dict(os.environ)
for line in subprocess.check_output(["cmd.exe","/d","/c",str(OUT/"environment.cmd")],encoding="mbcs").splitlines():
    if "=" in line and not line.startswith("="):
        key,value=line.split("=",1);env[key]=value
compiler=Path(env["VCToolsInstallDir"])/"bin/Hostx64/x64/cl.exe"
installer=(ROOT/"components/ouse_installer/ouse_installer.c").read_text(encoding="utf-8")
runtime=(ROOT/"components/ouse_runtime/ouse_runtime.c").read_text(encoding="utf-8")
handoff=installer[installer.index("static void ui_call("):installer.index("static int find_package(")]
validation=runtime[runtime.index("esp_err_t ouse_script_validate_begin("):runtime.index("void ouse_runtime_get_diagnostics(")]
fixture=r'''
#include "esp_err.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct {int unused;} ouse_script_package_t;
typedef struct {int unused;} ouse_script_app_t;
typedef struct {int unused;} ouse_app_definition_t;
typedef struct {const char *id;ouse_script_package_t package;ouse_app_definition_t definition;} ouse_bpk_t;
typedef struct ouse_script_validation ouse_script_validation_t;
typedef struct {unsigned refs;bool validation_only;int *validated;esp_err_t validation_result;} instance_t;
static instance_t *job;
static unsigned live,waits,prepared;
static bool inside_ui,timeout,enqueue_failure;
static int ui_signal;
static int *ui_done=&ui_signal;
static struct {int action;ouse_bpk_t *package;const char *id;esp_err_t error;ouse_script_validation_t *validation;} ui;
#define portMAX_DELAY 0xffffffff
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define ESP_ERR_TIMEOUT 0x107
static void release_instance(instance_t *app){assert(app->refs);if(!--app->refs){free(app->validated);free(app);--live;}}
static void complete(void){assert(job);job->validation_result=ESP_OK;*job->validated=1;release_instance(job);job=NULL;}
static int xSemaphoreTake(int *semaphore,unsigned wait){
    assert(!inside_ui);
    if(semaphore==ui_done){assert(wait==portMAX_DELAY && *semaphore);*semaphore=0;return pdTRUE;}
    assert(wait==2000);++waits;if(timeout)return 0;
    complete();assert(*semaphore);*semaphore=0;return pdTRUE;
}
static void xSemaphoreGive(int *semaphore){*semaphore=1;}
static int *xSemaphoreCreateBinary(void){return calloc(1,sizeof(int));}
static esp_err_t ensure_worker(void){assert(inside_ui);return ESP_OK;}
static instance_t *allocate_instance(const ouse_script_package_t *package){assert(package && inside_ui);instance_t *app=calloc(1,sizeof(*app));app->refs=1;++live;return app;}
static bool enqueue_instance(instance_t *app){assert(inside_ui);if(enqueue_failure)return false;assert(!job);job=app;++app->refs;return true;}
static esp_err_t ouse_script_prepare(ouse_script_app_t *state,const ouse_script_package_t *package,ouse_app_definition_t *definition){assert(state&&package&&definition);return ESP_OK;}
static esp_err_t ouse_core_user_check(const char *id){assert(id&&inside_ui);return ESP_OK;}
static esp_err_t ouse_core_user_apply(const ouse_app_definition_t *definition){assert(definition&&inside_ui);return ESP_OK;}
static esp_err_t ouse_core_user_remove(const char *id){assert(id&&inside_ui);return ESP_OK;}
static esp_err_t ouse_app_open(const char *id){assert(id&&inside_ui);return ESP_OK;}
static esp_err_t ouse_core_post_ui(void (*callback)(void *),void *context){assert(!inside_ui);inside_ui=true;callback(context);inside_ui=false;++prepared;return ESP_OK;}
/* VALIDATION_FUNCTIONS */
/* HANDOFF_FUNCTIONS */
int main(void){
    ouse_bpk_t package={.id="user.validation"};
    assert(on_ui(0,&package,NULL)==ESP_OK && waits==1 && prepared==1 && !live && !job);
    timeout=true;assert(on_ui(0,&package,NULL)==ESP_ERR_TIMEOUT && live==1 && job && job->refs==1);
    complete();assert(!live && !job); // late worker result owns only its own lifetime
    timeout=false;enqueue_failure=true;
    assert(on_ui(0,&package,NULL)==ESP_ERR_NO_MEM && !live && !job && waits==2);
    enqueue_failure=false;
    assert(on_ui(1,&package,NULL)==ESP_OK && on_ui(2,NULL,package.id)==ESP_OK && on_ui(3,NULL,package.id)==ESP_OK && waits==2);
    puts("PASS production installer handoff: UI never waits for compilation; native success/timeout/queue failure release ownership safely");
    return 0;
}
'''.replace("/* VALIDATION_FUNCTIONS */",validation).replace("/* HANDOFF_FUNCTIONS */",handoff)
(OUT/"validation-check.c").write_text(fixture,encoding="utf-8")
result=subprocess.run([str(compiler),"/nologo","/std:c11","/O2","/utf-8","/I"+str(OUT),str(OUT/"validation-check.c"),"/Fe"+str(OUT/"validation-check.exe")],cwd=OUT,env=env,capture_output=True,text=True,errors="replace")
assert result.returncode==0,(result.stdout,result.stderr)
subprocess.run([str(OUT/"validation-check.exe")],cwd=OUT,check=True,timeout=15)
