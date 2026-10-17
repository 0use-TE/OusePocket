"""Exercise real SD path, upload and download code with a disposable host filesystem."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".tools/ouse-files-check"
OUT.mkdir(parents=True, exist_ok=True)
setup = ROOT / ".tools/ouse-system-check/environment.cmd"
assert setup.exists(), "Run test-ouse-system.py first"
sdk = r'''
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <setjmp.h>
#include <io.h>
#include <direct.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_LOGW(...) ((void)0)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
#define MALLOC_CAP_DMA 8
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0xffffffff
#define pdMS_TO_TICKS(value) (value)
#define mkdir(path,mode) _mkdir(path)
#define fsync _commit
#define strcasecmp _stricmp
#define S_ISDIR(mode) (((mode)&_S_IFDIR)!=0)
#define S_ISREG(mode) (((mode)&_S_IFREG)!=0)
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef unsigned char BYTE;
typedef struct {int unused;} FATFS;
typedef struct {int unused;} sdmmc_card_t;
typedef struct {const char *base_path,*fat_drive;int max_files;} esp_vfs_fat_conf_t;
#define FR_OK 0
#define FM_FAT32 2
typedef int FRESULT;
typedef struct {unsigned fmt,n_fat,align,au_size;} MKFS_PARM;
int f_mkfs(const char *,const MKFS_PARM *,void *,unsigned);
void *heap_caps_aligned_alloc(size_t,size_t,unsigned);
void heap_caps_free(void *);
typedef struct {int offset;} DIR;
struct dirent {char d_name[256];};
DIR *opendir(const char *);
struct dirent *readdir(DIR *);
int closedir(DIR *);
int64_t esp_timer_get_time(void);
void *heap_caps_calloc(size_t,size_t,unsigned);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t,unsigned);
void xSemaphoreGive(SemaphoreHandle_t);
void vSemaphoreDelete(SemaphoreHandle_t);
QueueHandle_t xQueueCreate(unsigned,unsigned);
int xQueueSend(QueueHandle_t,const void *,unsigned);
int xQueueReceive(QueueHandle_t,void *,unsigned);
void vQueueDelete(QueueHandle_t);
int xTaskCreate(void (*)(void *),const char *,unsigned,void *,unsigned,void *);
esp_err_t ff_diskio_get_drive(BYTE *);
void ff_diskio_register_sdmmc(BYTE,sdmmc_card_t *);
void ff_sdmmc_set_disk_status_check(BYTE,bool);
void ff_diskio_register(BYTE,const void *);
esp_err_t esp_vfs_fat_register(const esp_vfs_fat_conf_t *,FATFS **);
esp_err_t esp_vfs_fat_unregister_path(const char *);
esp_err_t esp_vfs_fat_info(const char *,uint64_t *,uint64_t *);
int f_mount(FATFS *,const char *,int);
esp_err_t sdmmc_get_status(sdmmc_card_t *);
'''
(OUT / "sdk.h").write_text(sdk)
for name in ["esp_err.h", "sdmmc_cmd.h", "esp_vfs_fat.h", "diskio_impl.h", "diskio_sdmmc.h", "esp_heap_caps.h", "esp_timer.h",
             "freertos/FreeRTOS.h", "freertos/task.h", "freertos/queue.h", "freertos/semphr.h", "unistd.h", "dirent.h", "strings.h", "esp_log.h"]:
    target = OUT / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text('#include "sdk.h"\n')
fixture = r'''
#include "sdk.h"
#include "ouse_files.h"
#include "ouse_net.h"
static void (*worker)(void *);
static void *queued[8];static int queued_count;
static int mount_failure,http_status=200,cancelled,terminal,terminal_error,not_ready;
static int64_t now;
static int formats;
void *heap_caps_aligned_alloc(size_t alignment,size_t bytes,unsigned caps){assert(alignment==64 && caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA));return malloc(bytes);}
void heap_caps_free(void *ptr){free(ptr);}
int f_mkfs(const char *drive,const MKFS_PARM *options,void *work,unsigned bytes){assert(!strcmp(drive,"0:") && work && bytes==4096);assert(options->fmt==FM_FAT32 && options->n_fat==2 && options->align==2048 && options->au_size==32768);++formats;return FR_OK;}
static jmp_buf idle;
static DIR directory;
DIR *opendir(const char *path){(void)path;directory.offset=0;return not_ready ? NULL : &directory;}
struct dirent *readdir(DIR *dir){static struct dirent entry;const char *names[]={"test.wav","../escape.wav","notes.txt"};if(dir->offset==3)return NULL;strcpy(entry.d_name,names[dir->offset++]);return &entry;}
int closedir(DIR *dir){(void)dir;return 0;}
int64_t esp_timer_get_time(void){return now;}
void *heap_caps_calloc(size_t count,size_t size,unsigned caps){(void)caps;return calloc(count,size);}
SemaphoreHandle_t xSemaphoreCreateMutex(void){return (void *)1;}
int xSemaphoreTake(SemaphoreHandle_t mutex,unsigned ticks){(void)mutex;(void)ticks;return 1;}
void xSemaphoreGive(SemaphoreHandle_t mutex){(void)mutex;}
void vSemaphoreDelete(SemaphoreHandle_t mutex){(void)mutex;}
QueueHandle_t xQueueCreate(unsigned count,unsigned size){assert(count==4 && size==sizeof(void *));return (void *)2;}
int xQueueSend(QueueHandle_t queue,const void *item,unsigned wait){(void)queue;(void)wait;queued[queued_count++]=*(void *const *)item;return 1;}
int xQueueReceive(QueueHandle_t queue,void *item,unsigned wait){(void)queue;(void)wait;if(!queued_count)longjmp(idle,1);*(void **)item=queued[0];memmove(queued,queued+1,(--queued_count)*sizeof(void *));return 1;}
void vQueueDelete(QueueHandle_t queue){(void)queue;}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle){(void)name;(void)stack;(void)arg;(void)priority;(void)handle;worker=fn;return 1;}
esp_err_t ff_diskio_get_drive(BYTE *drive){*drive=0;return 0;}
void ff_diskio_register_sdmmc(BYTE drive,sdmmc_card_t *card){(void)drive;(void)card;}
void ff_sdmmc_set_disk_status_check(BYTE drive,bool enabled){assert(!drive && !enabled);}
void ff_diskio_register(BYTE drive,const void *impl){(void)drive;(void)impl;}
esp_err_t esp_vfs_fat_register(const esp_vfs_fat_conf_t *config,FATFS **out){static FATFS fs;assert(config->max_files==6);*out=&fs;return 0;}
esp_err_t esp_vfs_fat_unregister_path(const char *path){(void)path;return 0;}
esp_err_t esp_vfs_fat_info(const char *path,uint64_t *total,uint64_t *free_bytes){(void)path;*total=1024*1024*1024;*free_bytes=*total/2;return 0;}
int f_mount(FATFS *fs,const char *drive,int mount){(void)fs;(void)drive;return mount && mount_failure ? 1 : 0;}
esp_err_t sdmmc_get_status(sdmmc_card_t *card){(void)card;return 0;}
esp_err_t ouse_task_create(const ouse_task_owner_t *owner,ouse_task_kind_t kind,const char *op,ouse_task_policy_t policy,bool cancellable,uint32_t *id){(void)owner;(void)kind;(void)op;(void)cancellable;assert(policy==OUSE_TASK_STOP_ON_CLOSE);*id=42;return 0;}
bool ouse_task_start(uint32_t id,bool cancellable){(void)id;(void)cancellable;return true;}
bool ouse_task_cancel_requested(uint32_t id){(void)id;return cancelled!=0;}
void ouse_task_finish(uint32_t id,esp_err_t error,bool was_cancelled){assert(id==42);terminal_error=error;terminal=was_cancelled ? 2 : 1;}
esp_err_t ouse_http_validate(const ouse_http_request_t *request){return request->url && !strncmp(request->url,"https://",8) ? 0 : ESP_ERR_INVALID_ARG;}
esp_err_t ouse_http_transfer(const ouse_http_request_t *request,int64_t deadline,size_t limit,ouse_http_response_t *response,ouse_http_sink_t sink,ouse_http_stop_t stop,void *context){(void)request;(void)deadline;(void)limit;response->status=http_status;if(stop(context))return ESP_FAIL;return sink(context,(const unsigned char *)"abc",3);}
static void pump(void){if(!setjmp(idle))worker(NULL);}
int main(int argc,char **argv){
 assert(argc==2);sdmmc_card_t card={0};char path[180];
 const char *bad[]={"../secret","Music/../secret","Music/.hidden","Music/a/b.wav","Music/a\\b.wav","Other/a.wav","/Music/a.wav","Music/a.wav.","Music/a..wav","Music/a.wav "};
 for(unsigned index=0;index<sizeof(bad)/sizeof(bad[0]);++index)assert(!ouse_files_path(bad[index],path,sizeof(path)));
 assert(ouse_files_path("Music/test.wav",path,sizeof(path)));
 if(!strcmp(argv[1],"mount-fail")){mount_failure=1;assert(ouse_files_init(&card)==ESP_FAIL);assert(!formats);assert(ouse_files_format_unmounted(&card)==ESP_OK && formats==1);return 0;}
 assert(ouse_files_init(&card)==ESP_OK);
 assert(ouse_files_format_unmounted(&card)==ESP_ERR_INVALID_STATE && !formats);
 assert(ouse_files_discard_partial("../bad")==ESP_ERR_INVALID_ARG);
 assert(ouse_files_discard_partial("Music/unused.wav")==ESP_OK);
 ouse_files_state_t state;ouse_files_get(&state);assert(state.ready && state.count==0);
 assert(ouse_files_upload_begin("Music/test.wav",3,0x352441c2)==ESP_OK);
 assert(ouse_files_upload_data(1,(const unsigned char *)"abc",3)==ESP_ERR_INVALID_ARG);
 assert(ouse_files_upload_data(0,(const unsigned char *)"abc",3)==ESP_OK);
 assert(ouse_files_upload_end()==ESP_OK);pump();
 size_t bytes;uint32_t crc;assert(ouse_files_checksum("Music/test.wav",&bytes,&crc)==ESP_OK && bytes==3 && crc==0x352441c2);
 assert(ouse_files_upload_begin("Music/test.wav",3,0)==ESP_FAIL);
 assert(ouse_files_checksum("Music/test.wav",&bytes,&crc)==ESP_OK && crc==0x352441c2);
 assert(ouse_files_upload_begin("Music/corrupt.wav",3,1)==ESP_OK);assert(ouse_files_upload_data(0,(const unsigned char *)"abc",3)==ESP_OK);assert(ouse_files_upload_end()!=ESP_OK);
 assert(ouse_files_checksum("Music/corrupt.wav",&bytes,&crc)==ESP_ERR_NOT_FOUND);
 assert(ouse_files_upload_begin("Music/timeout.wav",3,1)==ESP_OK);now=31000000;ouse_files_upload_poll();ouse_files_get(&state);assert(!state.busy);
 ouse_file_entry_t entries[2];assert(ouse_files_list(0,entries,2)==1 && !strcmp(entries[0].path,"Music/test.wav"));
 ouse_task_owner_t owner={.app_id="user.test"};uint32_t id;
 assert(ouse_files_download(&owner,"https://example.test/a","Downloads/download.wav",&id)==ESP_OK);pump();assert(terminal && terminal_error==ESP_OK);
 assert(ouse_files_checksum("Downloads/download.wav",&bytes,&crc)==ESP_OK && crc==0x352441c2);
 http_status=404;assert(ouse_files_download(&owner,"https://example.test/missing","Downloads/missing.wav",&id)==ESP_OK);pump();assert(terminal_error!=ESP_OK);assert(ouse_files_checksum("Downloads/missing.wav",&bytes,&crc)==ESP_ERR_NOT_FOUND);
 cancelled=1;assert(ouse_files_download(&owner,"https://example.test/cancel","Downloads/cancel.wav",&id)==ESP_OK);pump();assert(terminal==2);assert(ouse_files_checksum("Downloads/cancel.wav",&bytes,&crc)==ESP_ERR_NOT_FOUND);
 puts("PASS actual SD service: scoped paths, no overwrite, CRC, abort/timeout, cached scan, HTTP failure and cancellation");return 0;
}
'''
(OUT / "check.c").write_text(fixture)
env = dict(os.environ)
for line in subprocess.check_output(["cmd.exe", "/d", "/c", str(setup)], encoding="mbcs").splitlines():
    if "=" in line and not line.startswith("="):
        key, value = line.split("=", 1)
        env[key] = value
compiler = Path(env["VCToolsInstallDir"]) / "bin/Hostx64/x64/cl.exe"
for case in ["normal", "mount-fail"]:
    with tempfile.TemporaryDirectory(prefix="sandbox-", dir=OUT) as sandbox:
        assert Path(sandbox).resolve().is_relative_to(OUT.resolve())
        subprocess.run([str(compiler), "/nologo", "/utf-8", "/std:c11", "/UNDEBUG", "/D_CRT_NONSTDC_NO_WARNINGS",
            '/DOUSE_FILES_ROOT="'+Path(sandbox).as_posix()+'"', "/I"+str(OUT), "/I"+str(ROOT/"components/ouse_files/include"),
            "/I"+str(ROOT/"components/ouse_tasks/include"), "/I"+str(ROOT/"components/ouse_net/include"),
            str(ROOT/"components/ouse_files/ouse_files.c"), str(OUT/"check.c"), "/Fe"+str(OUT/"check.exe")], cwd=OUT, env=env, check=True)
        subprocess.run([str(OUT/"check.exe"), case], check=True)
