"""Production profile worker with deterministic NVS, RTOS and KDF boundaries."""
from pathlib import Path
import os,subprocess
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/identity-check';OUT.mkdir(parents=True,exist_ok=True)
def put(name,text):
    p=OUT/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text,encoding='utf-8')
put('esp_err.h','''#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_TIMEOUT 3
#define ESP_ERR_NO_MEM 4
#define ESP_ERR_NVS_NOT_FOUND 5
''')
put('ouse_tasks.h','''#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {const char *app_id;} ouse_task_owner_t;
#define OUSE_TASK_SETTINGS 1
#define OUSE_TASK_FINISH_ON_CLOSE 1
bool ouse_tasks_submission_begin(void);
void ouse_tasks_submission_end(void);
int ouse_task_create(const ouse_task_owner_t *,int,const char *,int,bool,uint32_t *);
int ouse_task_start(uint32_t,bool);
void ouse_task_finish(uint32_t,int,bool);
''')
put('freertos/FreeRTOS.h','''#pragma once
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY (~0u)
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
''')
put('freertos/task.h','''#include "FreeRTOS.h"
int xTaskCreate(void (*fn)(void *),const char *,unsigned,void *,int,void *);
''')
put('freertos/queue.h','''#include <stddef.h>
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned,size_t);
int xQueueSend(QueueHandle_t,const void *,unsigned);
int xQueueReceive(QueueHandle_t,void *,unsigned);
void vQueueDelete(QueueHandle_t);
''')
put('esp_timer.h','#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
put('esp_random.h','#include <stddef.h>\nvoid esp_fill_random(void *,size_t);\n')
put('nvs.h','''#include <stddef.h>
typedef int nvs_handle_t;
#define NVS_READWRITE 1
#define NVS_READONLY 0
int nvs_open(const char *,int,nvs_handle_t *);
int nvs_get_blob(nvs_handle_t,const char *,void *,size_t *);
int nvs_set_blob(nvs_handle_t,const char *,const void *,size_t);
int nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
''')
put('psa/crypto.h','''#include <stddef.h>
#include <stdint.h>
typedef int psa_status_t;
typedef struct {uint8_t salt[16],pin[6];unsigned cost;} psa_key_derivation_operation_t;
#define PSA_KEY_DERIVATION_OPERATION_INIT {0}
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 1
#define PSA_ALG_PBKDF2_HMAC(x) (x)
#define PSA_KEY_DERIVATION_INPUT_COST 1
#define PSA_KEY_DERIVATION_INPUT_SALT 2
#define PSA_KEY_DERIVATION_INPUT_PASSWORD 3
int psa_crypto_init(void);
int psa_key_derivation_setup(psa_key_derivation_operation_t *,int);
int psa_key_derivation_input_integer(psa_key_derivation_operation_t *,int,unsigned);
int psa_key_derivation_input_bytes(psa_key_derivation_operation_t *,int,const uint8_t *,size_t);
int psa_key_derivation_output_bytes(psa_key_derivation_operation_t *,uint8_t *,size_t);
int psa_key_derivation_abort(psa_key_derivation_operation_t *);
''')
put('test.c','#include "'+(ROOT/'components/ouse_system/ouse_identity.c').as_posix()+'"\n'+r'''
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
static jmp_buf stop;
static void *queued;
static record_t disk,staged;
static bool exists,write_fail,quiesce;
static int64_t now;
static unsigned finished,created;
int64_t esp_timer_get_time(void){return now;}
void esp_fill_random(void *p,size_t n){static unsigned seed;memset(p,++seed,n);}
bool ouse_tasks_submission_begin(void){return !quiesce;}
void ouse_tasks_submission_end(void){}
int ouse_task_create(const ouse_task_owner_t *o,int k,const char *s,int p,bool b,uint32_t *id){(void)o;(void)k;(void)s;(void)p;(void)b;*id=++created;return 0;}
int ouse_task_start(uint32_t id,bool b){assert(id);(void)b;return 0;}
void ouse_task_finish(uint32_t id,int e,bool b){assert(id);(void)e;(void)b;++finished;}
QueueHandle_t xQueueCreate(unsigned n,size_t s){assert(n==1&&s==sizeof(void *));return &queued;}
void vQueueDelete(QueueHandle_t q){(void)q;}
int xQueueSend(QueueHandle_t q,const void *p,unsigned t){(void)q;(void)t;assert(!queued);queued=*(void **)p;return 1;}
int xQueueReceive(QueueHandle_t q,void *p,unsigned t){(void)q;(void)t;if(!queued)longjmp(stop,1);*(void **)p=queued;queued=NULL;return 1;}
int xTaskCreate(void (*f)(void *),const char *s,unsigned n,void *p,int pri,void *h){(void)f;(void)s;(void)n;(void)p;(void)pri;(void)h;return 1;}
int nvs_open(const char *s,int mode,nvs_handle_t *h){(void)s;*h=1;return exists||mode?0:ESP_ERR_NVS_NOT_FOUND;}
int nvs_get_blob(nvs_handle_t h,const char *s,void *p,size_t *n){(void)h;(void)s;memcpy(p,&disk,sizeof(disk));*n=sizeof(disk);return 0;}
int nvs_set_blob(nvs_handle_t h,const char *s,const void *p,size_t n){(void)h;(void)s;assert(n==sizeof(staged));memcpy(&staged,p,n);return write_fail?ESP_FAIL:0;}
int nvs_commit(nvs_handle_t h){(void)h;if(write_fail)return ESP_FAIL;disk=staged;exists=true;return 0;}
void nvs_close(nvs_handle_t h){(void)h;}
int psa_crypto_init(void){return 0;}
int psa_key_derivation_setup(psa_key_derivation_operation_t *p,int alg){(void)p;assert(alg==PSA_ALG_SHA_256);return 0;}
int psa_key_derivation_input_integer(psa_key_derivation_operation_t *p,int type,unsigned cost){assert(type==1&&cost==10000);p->cost=cost;return 0;}
int psa_key_derivation_input_bytes(psa_key_derivation_operation_t *p,int type,const uint8_t *s,size_t n){assert((type==2&&n==16)||(type==3&&n==6));memcpy(type==2?p->salt:p->pin,s,n);return 0;}
int psa_key_derivation_output_bytes(psa_key_derivation_operation_t *p,uint8_t *s,size_t n){assert(p->cost==10000&&n==32);for(unsigned i=0;i<32;++i)s[i]=p->salt[i%16]^p->pin[i%6];return 0;}
int psa_key_derivation_abort(psa_key_derivation_operation_t *p){memset(p,0,sizeof(*p));return 0;}
static void run(void){if(!setjmp(stop))worker(NULL);assert(!state.pending && finished==created);}
static void reboot(void){assert(!queued);jobs=NULL;memset(&state,0,sizeof(state));retry_at=0;assert(ouse_identity_init()==0);}
int main(void){
 assert(ouse_identity_valid_pin("000000")&&!ouse_identity_valid_pin("12345")&&!ouse_identity_valid_pin("1234567")&&!ouse_identity_valid_pin("12345x"));
 assert(ouse_identity_valid_name("Pocket")&&ouse_identity_valid_name("口袋")&&!ouse_identity_valid_name("")&&!ouse_identity_valid_name(" Pocket")&&!ouse_identity_valid_name("bad\nname")&&!ouse_identity_valid_name("\xc0\x80"));
 assert(ouse_identity_init()==0);uint32_t id=0;
 assert(ouse_identity_step(4,&id)==0);run();assert(!state.setup_done && state.error!=0);
 write_fail=true;assert(ouse_identity_name("Pocket",&id)==0);assert(ouse_identity_name("Busy",&id)!=0);run();assert(!state.name[0]);
 write_fail=false;assert(ouse_identity_name("Pocket",&id)==0);run();assert(!strcmp(state.name,"Pocket"));
 assert(ouse_identity_pin("","123456",&id)==0);run();assert(state.pin_enabled);
 uint8_t original[32];memcpy(original,disk.hash,32);
 for(unsigned i=0;i<5;++i){assert(ouse_identity_verify("000000",&id)==0);run();assert(state.error!=0);}
 reboot();ouse_identity_state_t snapshot;ouse_identity_get(&snapshot);assert(snapshot.retry_seconds==30);
 assert(ouse_identity_verify("123456",&id)==0);run();assert(state.error==ESP_ERR_TIMEOUT);
 now+=31000000;assert(ouse_identity_verify("123456",&id)==0);run();assert(state.verified && disk.failures==0);
 write_fail=true;assert(ouse_identity_pin("123456","654321",&id)==0);run();assert(state.error!=0&&!memcmp(original,disk.hash,32));write_fail=false;
 assert(ouse_identity_pin("111111","",&id)==0);run();assert(state.pin_enabled && state.error!=0);
 assert(ouse_identity_pin("123456","654321",&id)==0);run();assert(state.error==0&&memcmp(original,disk.hash,32));
 assert(ouse_identity_step(4,&id)==0);run();reboot();assert(state.setup_done&&state.pin_enabled&&!strcmp(state.name,"Pocket"));
 assert(ouse_identity_pin("654321","",&id)==0);run();assert(!state.pin_enabled);
 quiesce=true;assert(ouse_identity_name("Blocked",&id)!=0);quiesce=false;
 disk.version=999;jobs=NULL;assert(ouse_identity_init()!=0);
 puts("PASS production identity: validation, async serialization, atomic persistence failure, old PIN, throttle/reboot, clear PIN, corrupt record, task drain; KDF boundary mocked");
}
''')
vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
vs=subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
put('env.cmd',f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n')
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(OUT/'env.cmd')],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        k,v=line.split('=',1);env[k]=v
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
subprocess.run([str(compiler),'/nologo','/std:c11','/W4','/I'+str(OUT),'/I'+str(ROOT/'components/ouse_system/include'),str(OUT/'test.c'),'/Fe:'+str(OUT/'test.exe')],cwd=OUT,env=env,check=True)
subprocess.run([str(OUT/'test.exe')],check=True)
