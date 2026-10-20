"""Test production journal disk retention, capacity, paging and filtering."""
from pathlib import Path
import os, subprocess
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/journal-check'; OUT.mkdir(parents=True,exist_ok=True)
def put(name,text):
    p=OUT/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text,encoding='utf-8')
put('esp_err.h','#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_NO_MEM 1\n#define ESP_ERR_INVALID_STATE 2\n')
put('ouse_storage.h','''#pragma once
#include <stdbool.h>
#include <stddef.h>
#define OUSE_STORAGE_BASE "journal-test"
typedef struct {bool ready;size_t free_bytes;} ouse_storage_stats_t;
int ouse_storage_get_stats(ouse_storage_stats_t *s);
''')
put('ouse_clock.h','#include <stdbool.h>\n#include <stdint.h>\ntypedef struct {bool valid;int64_t utc_ms;} ouse_clock_state_t;\nvoid ouse_clock_get(ouse_clock_state_t *);\n')
put('esp_timer.h','#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
put('esp_system.h','int esp_reset_reason(void);\n')
put('esp_random.h','#include <stdint.h>\nuint32_t esp_random(void);\n')
put('esp_log.h','''#include <stdarg.h>
typedef int (*vprintf_like_t)(const char *,va_list);
vprintf_like_t esp_log_set_vprintf(vprintf_like_t);
#define ESP_LOGI(...) ((void)0)
''')
put('freertos/FreeRTOS.h','#pragma once\n#define pdTRUE 1\n#define pdPASS 1\n#define portMAX_DELAY (~0u)\n#define pdMS_TO_TICKS(x) (x)\n')
put('freertos/task.h','#include "FreeRTOS.h"\nint xTaskCreate(void (*)(void *),const char *,unsigned,void *,int,void *);\nvoid vTaskDelay(unsigned);\n')
put('freertos/queue.h','#pragma once\n#include <stddef.h>\ntypedef void *QueueHandle_t;\nQueueHandle_t xQueueCreate(unsigned,size_t);\nint xQueueSend(QueueHandle_t,const void *,unsigned);\nint xQueueReceive(QueueHandle_t,void *,unsigned);\nvoid vQueueDelete(QueueHandle_t);\n')
put('freertos/semphr.h','#pragma once\ntypedef void *SemaphoreHandle_t;\nSemaphoreHandle_t xSemaphoreCreateMutex(void);\nSemaphoreHandle_t xSemaphoreCreateBinary(void);\nint xSemaphoreTake(SemaphoreHandle_t,unsigned);\nint xSemaphoreGive(SemaphoreHandle_t);\nvoid vSemaphoreDelete(SemaphoreHandle_t);\n')
put('ouse_files.h','''#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {char app_id[48];} ouse_task_owner_t;
#define OUSE_TASK_STORAGE 1
#define OUSE_TASK_FINISH_ON_CLOSE 1
bool ouse_files_lease_begin(void);
void ouse_files_lease_end(void);
bool ouse_files_path(const char *,char *,size_t);
bool ouse_tasks_submission_begin(void);
void ouse_tasks_submission_end(void);
int ouse_task_create(const ouse_task_owner_t *,int,const char *,int,bool,uint32_t *);
int ouse_task_start(uint32_t,bool);
void ouse_task_finish(uint32_t,int,bool);
''')
put('esp_littlefs.h','#include <stddef.h>\nint esp_littlefs_info(const char *,size_t *,size_t *);\n')
put('strings.h','')
put('unistd.h','''#include <io.h>
#define fsync _commit
#define fileno _fileno
#define unlink _unlink
#define access _access
#define close _close
#define fdopen _fdopen
#define F_OK 0
''')
source=(ROOT/'components/ouse_journal/ouse_journal.c').as_posix()
put('test.c',r'''#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <direct.h>
#include <time.h>
static struct tm *gmtime_r(const time_t *t,struct tm *out){*out=*gmtime(t);return out;}
#define mkdir(path,mode) _mkdir(path)
#define S_ISDIR(mode) (((mode)&_S_IFMT)==_S_IFDIR)
#define open _open
#include "SOURCE"
bool ouse_files_lease_begin(void){return true;}
void ouse_files_lease_end(void){}
bool ouse_files_path(const char *p,char *out,size_t n){snprintf(out,n,"journal-test/export.txt");return true;}
bool ouse_tasks_submission_begin(void){return true;}
void ouse_tasks_submission_end(void){}
int ouse_task_create(const ouse_task_owner_t *o,int k,const char *s,int p,bool c,uint32_t *id){*id=1;return 0;}
int ouse_task_start(uint32_t id,bool c){return 1;}
void ouse_task_finish(uint32_t id,int e,bool c){}
int esp_littlefs_info(const char *p,size_t *total,size_t *used){*total=8000000;*used=2000000;return 0;}
static int64_t fake_epoch=1800000000;
static unsigned queued;static record_t last_record;
void ouse_clock_get(ouse_clock_state_t *s){s->valid=fake_epoch!=0;s->utc_ms=fake_epoch*1000;}
int64_t esp_timer_get_time(void){return 123000000;}
int esp_reset_reason(void){return 1;}
uint32_t esp_random(void){return 123;}
int ouse_storage_get_stats(ouse_storage_stats_t *s){s->ready=true;s->free_bytes=2000000;return 0;}
void vQueueDelete(void *q){}
void vSemaphoreDelete(void *s){}
void *xQueueCreate(unsigned n,size_t s){return (void *)1;}
int xQueueSend(void *q,const void *r,unsigned t){last_record=*(const record_t *)r;queued++;return 1;}
int xQueueReceive(void *q,void *r,unsigned t){return 0;}
void *xSemaphoreCreateMutex(void){return (void *)1;}
void *xSemaphoreCreateBinary(void){return (void *)1;}
int xSemaphoreTake(void *s,unsigned t){return 1;}
int xSemaphoreGive(void *s){return 1;}
int xTaskCreate(void (*f)(void *),const char *n,unsigned s,void *a,int p,void *h){return 1;}
void vTaskDelay(unsigned n){}
vprintf_like_t esp_log_set_vprintf(vprintf_like_t fn){return NULL;}
static void log_line(const char *fmt,...){va_list args;va_start(args,fmt);capture(fmt,args);va_end(args);}
static void clean(void){close_stream();for(unsigned i=0;i<SLOTS;i++){char path[96];path_for(i,path);unlink(path);}count=0;sequence=0;}
int main(void){
 _mkdir("journal-test");assert(directory_ready());assert(directory_ready());clean();lock=(void *)1;queue=(void *)1;boot_id=123;
 log_line("I (12) OuseTouch: moving\n");assert(queued==0);
 log_line("W (12) wifi: disconnected\n");assert(queued==1);
 log_line("E (12) OuseNet: password=secret\n");assert(queued==1);
 log_line("I (12) OuseCore: start\n");assert(queued==2);
 char long_message[450];memset(long_message,'a',369);memcpy(long_message+369,"\xe4\xb8\xad",3);memset(long_message+372,'b',40);long_message[412]=0;log_line("W (1) Test: %s",long_message);assert(strlen(last_record.text)==381&&last_record.text[380]=='a');
 record_t record={.epoch=fake_epoch,.uptime=100};strcpy(record.text,"W (1) Test: persisted");assert(append(&record));assert(close_stream());scan();assert(count==1);
 for(int i=0;i<50;i++){assert(append(&record));assert(close_stream());}scan();assert(count==1);
 request_revision=1;publish(1,0);assert(strstr(view.text,"persisted"));assert(view.bytes<CHUNK);
 clean();record.epoch=0;fake_epoch=0;assert(append(&record));close_stream();scan();assert(count==1);request_revision=2;publish(2,0);assert(strstr(view.text,"time pending boot="));
 fake_epoch=1800000000;scan();assert(segments[0].header.created==fake_epoch);fake_epoch+=DAYS;scan();assert(count==0);
 clean();record.epoch=fake_epoch;memset(record.text,'x',sizeof(record.text)-2);record.text[sizeof(record.text)-2]=0;
 for(int i=0;i<4000;i++)assert(append(&record));close_stream();scan();assert(count==32);size_t total=0;for(unsigned i=0;i<count;i++)total+=segments[i].size;assert(total<=OUSE_JOURNAL_LIMIT);
 request_revision=3;publish(3,0);assert(view.pages>1&&strlen(view.text)<=OUSE_JOURNAL_PAGE);request_revision=4;publish(4,1);assert(view.page==1&&view.text[strlen(view.text)-1]=='\n');
 char exported[128];unlink("journal-test/export.txt");unlink("journal-test/export.txt.part");assert(export_logs(exported,sizeof(exported))==ESP_OK);FILE *exported_file=fopen("journal-test/export.txt","rb");assert(exported_file);char first;assert(fread(&first,1,1,exported_file)==1&&first=='[');fclose(exported_file);assert(export_logs(exported,sizeof(exported))!=ESP_OK);unlink("journal-test/export.txt");
 unsigned old_sequence=sequence;count=0;sequence=0;scan();assert(sequence==old_sequence);assert(count==32);
 fake_epoch+=DAYS;scan();assert(count==0);clean();puts("PASS production journal: filtering, persistence, reopen, 7-day retention, unknown time, 1MiB rotation, paging, restart");
}
'''.replace('SOURCE',source))
vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
vs=subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
put('env.cmd',f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n')
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(OUT/'env.cmd')],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        k,v=line.split('=',1);env[k]=v
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
subprocess.run([str(compiler),'/nologo','/std:c11','/experimental:c11atomics','/W3',*(['/fsanitize=address','/Zi'] if os.environ.get('OUSE_JOURNAL_ASAN') else []),'/I'+str(OUT),'/I'+str(ROOT/'components/ouse_journal/include'),str(OUT/'test.c'),'/Fe:'+str(OUT/'test.exe')],cwd=OUT,env=env,check=True)
env['PATH']=str(compiler.parent)+os.pathsep+env['PATH']
subprocess.run([str(OUT/'test.exe')],cwd=OUT,env=env,check=True)
