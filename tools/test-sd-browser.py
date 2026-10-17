"""Compile the actual asynchronous SD browser with bounded SDK/filesystem doubles."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".tools/ouse-sd-browser-check"
OUT.mkdir(parents=True, exist_ok=True)
base = ROOT / ".tools/ouse-files-check/sdk.h"
assert base.exists(), "Run test-ouse-files.py first"
sdk = base.read_text() + r'''
#include <sys/stat.h>
#define ESP_ERR_TIMEOUT 0x107
int fake_stat(const char *,struct stat *);
#define stat(path,info) fake_stat(path,info)
int xTaskCreateWithCaps(void (*)(void *),const char *,unsigned,void *,unsigned,void *,unsigned);
'''
(OUT / "sdk.h").write_text(sdk)
for name in ["esp_err.h", "sdmmc_cmd.h", "esp_heap_caps.h", "esp_timer.h", "dirent.h",
             "freertos/FreeRTOS.h", "freertos/task.h", "freertos/idf_additions.h", "freertos/queue.h", "freertos/semphr.h"]:
    target = OUT / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text('#include "sdk.h"\n')
fixture = r'''
#include "sdk.h"
#include "ouse_files.h"
static void (*worker)(void *);
static void *jobs[4];static unsigned queued,guard_depth,next_id=1,finished;
static bool cancellation[128],ready=true,missing,disk_error;
static jmp_buf idle;static DIR directory;
static int64_t now,clock_step;
static const char *names[]={".","..","Outside","Ouse",".hidden","file1","file2","file3","file4","file5","file6","file7","file8"};
void *heap_caps_calloc(size_t count,size_t bytes,unsigned caps){assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));return calloc(count,bytes);}
SemaphoreHandle_t xSemaphoreCreateMutex(void){return (void *)1;}
int xSemaphoreTake(SemaphoreHandle_t handle,unsigned wait){(void)handle;(void)wait;assert(!guard_depth);++guard_depth;return 1;}
void xSemaphoreGive(SemaphoreHandle_t handle){(void)handle;assert(guard_depth==1);--guard_depth;}
void vSemaphoreDelete(SemaphoreHandle_t handle){(void)handle;}
QueueHandle_t xQueueCreate(unsigned count,unsigned bytes){assert(count==4 && bytes==sizeof(void *));return (void *)2;}
int xQueueSend(QueueHandle_t handle,const void *item,unsigned wait){(void)handle;(void)wait;if(queued==4)return 0;jobs[queued++]=*(void *const *)item;return pdTRUE;}
int xQueueReceive(QueueHandle_t handle,void *item,unsigned wait){(void)handle;(void)wait;if(!queued)longjmp(idle,1);*(void **)item=jobs[0];memmove(jobs,jobs+1,(--queued)*sizeof(void *));return pdTRUE;}
void vQueueDelete(QueueHandle_t handle){(void)handle;}
int xTaskCreateWithCaps(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle,unsigned caps){(void)name;(void)arg;(void)priority;(void)handle;assert(stack==6144 && caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));worker=fn;return pdPASS;}
void ouse_files_get(ouse_files_state_t *out){memset(out,0,sizeof(*out));out->ready=ready;}
int64_t esp_timer_get_time(void){now+=clock_step;return now;}
DIR *opendir(const char *path){assert(!guard_depth && !strncmp(path,"/sdcard/",8));if(missing){errno=ENOENT;return NULL;}directory.offset=0;return &directory;}
struct dirent *readdir(DIR *handle){static struct dirent entry;if(disk_error){errno=EIO;return NULL;}if(handle->offset==13)return NULL;strcpy(entry.d_name,names[handle->offset++]);return &entry;}
int closedir(DIR *handle){(void)handle;return 0;}
int fake_stat(const char *path,struct stat *out){assert(!guard_depth);memset(out,0,sizeof(*out));out->st_mode=strstr(path,"Outside") || strstr(path,"Ouse") ? _S_IFDIR : _S_IFREG;out->st_size=123;return 0;}
esp_err_t ouse_task_create(const ouse_task_owner_t *owner,ouse_task_kind_t kind,const char *operation,ouse_task_policy_t policy,bool cancellable,uint32_t *id){assert(owner->instance && kind==OUSE_TASK_STORAGE && !strcmp(operation,"sd.browse") && policy==OUSE_TASK_STOP_ON_CLOSE && cancellable);*id=next_id++;return ESP_OK;}
bool ouse_task_start(uint32_t id,bool cancellable){assert(cancellable);return !cancellation[id];}
bool ouse_task_cancel_requested(uint32_t id){return cancellation[id];}
void ouse_task_finish(uint32_t id,esp_err_t error,bool cancelled){(void)id;(void)error;(void)cancelled;++finished;}
static void pump(void){if(!setjmp(idle))worker(NULL);}
int main(void){
 char path[1100];const char *bad[]={"","..","/..","/../foo","/a/../b","/a//b","/a/","/a\\b","/C:/file","/.. /file","/a./file"};
 for(unsigned index=0;index<sizeof(bad)/sizeof(bad[0]);++index)assert(!ouse_sd_browser_path(bad[index],path,sizeof(path)));
 assert(ouse_sd_browser_path("/",path,sizeof(path)) && !strcmp(path,"/sdcard/"));
 assert(ouse_sd_browser_path("/.hidden/child",path,sizeof(path)));
 assert(ouse_sd_browser_init()==ESP_OK && ouse_sd_browser_init()==ESP_OK);
 ouse_task_owner_t owner={.app_id="user.explorer",.instance=1},other={.app_id="user.explorer",.instance=2};uint32_t id;
 ouse_sd_directory_t *result=calloc(1,sizeof(*result));
 assert(ouse_sd_browser_request(&owner,"/",0,&id)==ESP_OK);
 uint32_t active=id;assert(ouse_sd_browser_result(&owner,id,result) && result->pending);
 assert(!ouse_sd_browser_result(&other,id,result));
 assert(ouse_sd_browser_request(&owner,"/Ouse",0,&id)==ESP_ERR_INVALID_STATE);
 pump();assert(ouse_sd_browser_result(&owner,active,result) && !result->pending && result->error==ESP_OK && result->count==8 && result->more);
 assert(!strcmp(result->entries[0].path,"/Outside") && result->entries[0].directory);
 assert(!strcmp(result->entries[2].name,".hidden"));
 assert(ouse_sd_browser_request(&owner,"/Outside",8,&id)==ESP_OK);pump();assert(ouse_sd_browser_result(&owner,id,result) && result->count==3 && !result->more && !strcmp(result->path,"/Outside"));
 missing=true;assert(ouse_sd_browser_request(&owner,"/missing",0,&id)==ESP_OK);pump();assert(ouse_sd_browser_result(&owner,id,result) && result->error==ESP_ERR_NOT_FOUND);missing=false;
 ready=false;assert(ouse_sd_browser_request(&owner,"/",0,&id)==ESP_OK);pump();assert(ouse_sd_browser_result(&owner,id,result) && result->error==ESP_ERR_INVALID_STATE);ready=true;
 clock_step=6000000;assert(ouse_sd_browser_request(&owner,"/",0,&id)==ESP_OK);pump();assert(ouse_sd_browser_result(&owner,id,result) && result->error==ESP_ERR_TIMEOUT);clock_step=0;
 disk_error=true;assert(ouse_sd_browser_request(&owner,"/",0,&id)==ESP_OK);pump();assert(ouse_sd_browser_result(&owner,id,result) && result->error==ESP_FAIL);disk_error=false;
 assert(ouse_sd_browser_request(&owner,"/",0,&id)==ESP_OK);cancellation[id]=true;pump();assert(ouse_sd_browser_result(&owner,id,result) && !result->pending);
 assert(ouse_sd_browser_request(&owner,"/",0,&id)==ESP_OK);ouse_sd_browser_forget(&owner);pump();assert(!ouse_sd_browser_result(&owner,id,result));
 assert(ouse_sd_browser_request(&other,"/",0,&id)==ESP_OK);pump();assert(ouse_sd_browser_result(&other,id,result));ouse_sd_browser_forget(&other);
 assert(finished>=8);free(result);puts("PASS actual SD browser: full root, hidden entries, pages, owner isolation, close/cancel, errors, deadline and traversal rejection");return 0;
}
'''
(OUT / "check.c").write_text(fixture)
env = dict(os.environ)
setup = ROOT / ".tools/ouse-system-check/environment.cmd"
for line in subprocess.check_output(["cmd.exe", "/d", "/c", str(setup)], encoding="mbcs").splitlines():
    if "=" in line and not line.startswith("="):
        key, value = line.split("=", 1)
        env[key] = value
compiler = Path(env["VCToolsInstallDir"]) / "bin/Hostx64/x64/cl.exe"
subprocess.run([str(compiler), "/nologo", "/utf-8", "/std:c11", "/UNDEBUG", "/I"+str(OUT),
    "/I"+str(ROOT/"components/ouse_files/include"), "/I"+str(ROOT/"components/ouse_tasks/include"),
    str(ROOT/"components/ouse_files/ouse_sd_browser.c"), str(OUT/"check.c"), "/Fe"+str(OUT/"check.exe")], cwd=OUT, env=env, check=True)
subprocess.run([str(OUT/"check.exe")], check=True)
