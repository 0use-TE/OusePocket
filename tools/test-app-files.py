"""Real application SD worker: isolation, binary IO, replacement/recovery and cancellation."""
from pathlib import Path
import os,subprocess,shutil
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'.tools/app-files-check';OUT.mkdir(parents=True,exist_ok=True)
base=ROOT/'.tools/ouse-files-check'
assert (base/'sdk.h').exists(),'Run test-ouse-files.py first'
for path in base.rglob('*.h'):
 target=OUT/path.relative_to(base);target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,target)
(OUT/'strings.h').write_text('#include <string.h>\n#define strcasecmp _stricmp\n#define strncasecmp _strnicmp\n')
fixture=r'''
#include "sdk.h"
#define ESP_ERR_TIMEOUT 0x107
#define xTaskCreateWithCaps(f,n,s,a,p,h,c) xTaskCreate(f,n,s,a,p,h)
void *heap_caps_malloc(size_t,unsigned);
#include "SOURCE"
static void *queued;static bool cancelled,card=true;static int leases,held,finished;static jmp_buf stop_worker;
static esp_err_t completed_error;static char *completed_result;static unsigned completions;
void *heap_caps_malloc(size_t n,unsigned caps){return malloc(n);}
void *heap_caps_calloc(size_t n,size_t size,unsigned caps){return calloc(n,size);}
QueueHandle_t xQueueCreate(unsigned n,unsigned size){return (void *)1;}
void vQueueDelete(QueueHandle_t q){}
int xTaskCreate(void (*f)(void *),const char *n,unsigned s,void *a,unsigned p,void *h){return pdPASS;}
int xQueueSend(QueueHandle_t q,const void *p,unsigned t){if(queued)return 0;queued=*(void **)p;return pdTRUE;}
int xQueueReceive(QueueHandle_t q,void *p,unsigned t){if(!queued)longjmp(stop_worker,1);*(void **)p=queued;queued=NULL;return pdTRUE;}
bool ouse_files_lease_begin(void){if(!card)return false;++leases;return true;}
void ouse_files_lease_end(void){assert(leases>0);--leases;}
esp_err_t ouse_task_create(const ouse_task_owner_t *o,ouse_task_kind_t k,const char *n,ouse_task_policy_t p,bool c,uint32_t *id){static uint32_t next=1;*id=next++;return ESP_OK;}
bool ouse_task_start(uint32_t id,bool c){return !cancelled;}
bool ouse_task_cancel_requested(uint32_t id){return cancelled;}
void ouse_task_finish(uint32_t id,esp_err_t e,bool c){++finished;}
void ouse_task_hold_result(uint32_t id,bool hold){held+=hold?1:-1;assert(held>=0);}
int64_t esp_timer_get_time(void){return 0;}
esp_err_t ouse_http_transfer(const ouse_http_request_t *r,int64_t deadline,size_t limit,ouse_http_response_t *response,ouse_http_sink_t output,ouse_http_stop_t stop,void *ctx){response->status=strstr(r->url,"missing")?404:200;return output(ctx,(const unsigned char *)"net",3);}
static DIR directory_handle;static unsigned directory_index;
DIR *opendir(const char *p){directory_index=0;return &directory_handle;}
struct dirent *readdir(DIR *p){static struct dirent e;if(directory_index++)return NULL;strcpy(e.d_name,"save.bin");return &e;}
int closedir(DIR *p){return 0;}
static void completed(void *ctx,uint32_t id,esp_err_t error,char *result){assert(ctx==(void *)9);completed_error=error;completed_result=result;++completions;ouse_task_hold_result(id,false);}
static void pump(void){if(!setjmp(stop_worker))worker(NULL);assert(!leases&&!held);}
static void request(const char *app,int operation,const char *path,unsigned offset,const void *data,size_t bytes,esp_err_t expected){
 ouse_task_owner_t owner={.instance=1};strcpy(owner.app_id,app);uint32_t id;
 assert(ouse_app_files_submit(&owner,operation,path,offset,data,bytes,&id,completed,(void *)9)==ESP_OK);unsigned before=completions;pump();assert(completions==before+1);assert(completed_error==expected);
}
static void dispose(void){free(completed_result);completed_result=NULL;}
int main(void){
 _mkdir("data");char absolute[320];assert(ouse_app_files_init()==ESP_OK);
 const char *invalid[]={"../x","/absolute","a/../b","a//b","a\\b","a:","x.part","x.bak","a/","a. ",".private"};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i)assert(!ouse_app_files_path("user.alpha",invalid[i],absolute,sizeof(absolute)));
 assert(!ouse_app_files_path("user...beta","ok",absolute,sizeof(absolute)));
 request("user.alpha",OUSE_APP_FILE_WRITE,"save.bin",0,"ABCD",4,ESP_OK);assert(!strcmp(completed_result,"4"));dispose();
 request("user.beta",OUSE_APP_FILE_READ,"save.bin",0,NULL,4,ESP_ERR_NOT_FOUND);dispose();
 request("user.alpha",OUSE_APP_FILE_READ,"save.bin",1,NULL,2,ESP_OK);assert(!strcmp(completed_result,"\"4243\""));dispose();
 request("user.alpha",OUSE_APP_FILE_WRITE_CHUNK,"save.bin",2,"xy",2,ESP_OK);dispose();
 request("user.alpha",OUSE_APP_FILE_READ,"save.bin",0,NULL,8,ESP_OK);assert(!strcmp(completed_result,"\"41427879\""));dispose();
 request("user.alpha",OUSE_APP_FILE_WRITE,"save.bin",0,"new",3,ESP_OK);dispose();
 assert(ouse_app_files_path("user.alpha","save.bin",absolute,sizeof(absolute)));char backup[344];snprintf(backup,sizeof(backup),"%s.bak",absolute);assert(!rename(absolute,backup));
 request("user.alpha",OUSE_APP_FILE_READ,"save.bin",0,NULL,8,ESP_OK);assert(!strcmp(completed_result,"\"6e6577\""));dispose();
 request("user.alpha",OUSE_APP_FILE_LIST,"",0,NULL,0,ESP_OK);assert(strstr(completed_result,"save.bin")&&strstr(completed_result,"\"more\":false"));dispose();
 request("user.alpha",OUSE_APP_FILE_DOWNLOAD,"save.bin",0,"http://host/missing",19,ESP_FAIL);dispose();
 request("user.alpha",OUSE_APP_FILE_READ,"save.bin",0,NULL,8,ESP_OK);assert(!strcmp(completed_result,"\"6e6577\""));dispose();
 request("user.alpha",OUSE_APP_FILE_DOWNLOAD,"save.bin",0,"http://host/ok",14,ESP_OK);dispose();
 request("user.alpha",OUSE_APP_FILE_MKDIR,"nested",0,NULL,0,ESP_OK);dispose();
 request("user.alpha",OUSE_APP_FILE_WRITE,"nested",0,"bad",3,ESP_ERR_INVALID_ARG);dispose();
 request("user.alpha",OUSE_APP_FILE_RENAME,"save.bin",0,"nested/save.bin",15,ESP_OK);dispose();
 request("user.alpha",OUSE_APP_FILE_REMOVE,"nested",0,NULL,0,ESP_FAIL);dispose();
 request("user.alpha",OUSE_APP_FILE_READ,"nested/save.bin",0,NULL,8,ESP_OK);assert(!strcmp(completed_result,"\"6e6574\""));dispose();
 card=false;request("user.alpha",OUSE_APP_FILE_READ,"nested/save.bin",0,NULL,4,ESP_ERR_INVALID_STATE);dispose();card=true;
 cancelled=true;request("user.alpha",OUSE_APP_FILE_WRITE,"untouched.bin",0,"bad",3,ESP_ERR_INVALID_STATE);dispose();cancelled=false;
 request("user.alpha",OUSE_APP_FILE_STAT,"untouched.bin",0,NULL,0,ESP_ERR_NOT_FOUND);dispose();
 request("user.alpha",OUSE_APP_FILE_REMOVE,"nested/save.bin",0,NULL,0,ESP_OK);dispose();request("user.alpha",OUSE_APP_FILE_REMOVE,"nested",0,NULL,0,ESP_OK);dispose();
 puts("PASS real private SD worker: two-owner isolation, traversal/reserved paths, partial binary IO, atomic replacement, interrupted replacement recovery, list, HTTP failure preservation, rename/remove, missing SD, cancellation and balanced leases/results");
}
'''.replace('SOURCE',(ROOT/'components/ouse_files/ouse_app_files.c').as_posix())
(OUT/'check.c').write_text(fixture,encoding='utf-8')
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(ROOT/'.tools/ouse-core-check/environment.cmd')],encoding='mbcs').splitlines():
 if '=' in line and not line.startswith('='):
  k,v=line.split('=',1);env[k]=v
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
subprocess.run([str(compiler),'/nologo','/utf-8','/std:c11','/D_CRT_SECURE_NO_WARNINGS','/D_CRT_NONSTDC_NO_WARNINGS','/DOUSE_APP_FILES_ROOT="data/Apps"','/I'+str(OUT),'/I'+str(ROOT/'components/ouse_files/include'),'/I'+str(ROOT/'components/ouse_tasks/include'),'/I'+str(ROOT/'components/ouse_net/include'),str(OUT/'check.c'),'/Fe:'+str(OUT/'check.exe')],cwd=OUT,env=env,check=True)
# Disposable working directory: all test file operations stay in this directory.
import tempfile
with tempfile.TemporaryDirectory(prefix='run-',dir=OUT) as run:subprocess.run([str(OUT/'check.exe')],cwd=run,check=True)
