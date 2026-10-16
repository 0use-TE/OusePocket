# Executed by the real QuickJS/LVGL host harness, sharing ROOT and OUT.
# Only board transport, RTOS notification and partition APIs are doubles.
headers = r'''
#pragma once
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <direct.h>
#include <io.h>
#include <fcntl.h>
#include <errno.h>
#include "esp_err.h"
const char *esp_err_to_name(esp_err_t);
#define mkdir(p,m) _mkdir(p)
static int fail_sync;
static int host_sync(int fd){if(fail_sync){fail_sync=0;return -1;}return _commit(fd);}
#define fsync host_sync
#define fileno _fileno
static int fail_unlink;
static int host_unlink(const char *path){if(fail_unlink){fail_unlink=0;errno=EACCES;return -1;}return _unlink(path);}
#define unlink host_unlink
static int fail_ui_lock;
// Windows rename cannot replace an existing file; emulate POSIX atomic replace.
static int fail_rename;
static int posix_rename(const char *from,const char *to) { if(fail_rename){fail_rename=0;return -1;}return MoveFileExA(from,to,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)?0:-1; }
#define rename posix_rename
#define ESP_LOGW(...) ((void)0)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define UART_NUM_0 0
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define portMAX_DELAY 0xffffffff
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
#define taskYIELD() ((void)0)
typedef struct {int baud_rate,data_bits,parity,stop_bits,flow_ctrl,source_clk;} uart_config_t;
typedef int *SemaphoreHandle_t;
typedef struct {size_t size;} esp_partition_t;
typedef struct {const char *base_path,*partition_label;bool format_if_mount_failed;} esp_vfs_littlefs_conf_t;
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_LITTLEFS 1
static int mount_failure=0,partition_kind,format_count;
static int esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *c){ if(c->format_if_mount_failed){++format_count;mount_failure=0;}return mount_failure?ESP_FAIL:ESP_OK; }
static const esp_partition_t *esp_partition_find_first(int t,int s,const char *label){(void)t;(void)s;(void)label;static esp_partition_t part={8192};return partition_kind?&part:NULL;}
static int esp_partition_read(const esp_partition_t *p,size_t off,void *b,size_t n){(void)p;memset(b,255,n);if(partition_kind==2 && off==4096)((unsigned char *)b)[n-1]=0;return partition_kind==3?ESP_FAIL:ESP_OK;}
static void *heap_caps_malloc(size_t n,int caps){(void)caps;return malloc(n);}
static bool uart_is_driver_installed(int p){(void)p;return false;}
static int uart_param_config(int p,const uart_config_t *c){(void)p;(void)c;return 0;}
static int uart_driver_install(int p,int rx,int tx,int q,void *out,int flags){(void)p;(void)rx;(void)tx;(void)q;(void)out;(void)flags;return 0;}
static int uart_driver_delete(int p){(void)p;return 0;}
static int uart_read_bytes(int p,void *b,size_t n,unsigned timeout){(void)p;(void)b;(void)n;(void)timeout;return 0;}
static int uart_get_buffered_data_len(int p,size_t *n){(void)p;*n=0;return 0;}
static SemaphoreHandle_t xSemaphoreCreateBinary(void){return calloc(1,sizeof(int));}
static void xSemaphoreGive(SemaphoreHandle_t s){*s=1;}
static void xSemaphoreTake(SemaphoreHandle_t s,unsigned timeout){(void)timeout;unsigned i=0;while(!*s && ++i<500){lv_tick_inc(20);lv_timer_handler();}assert(*s);*s=0;}
static void vSemaphoreDelete(SemaphoreHandle_t s){free(s);}
static void vTaskDelete(void *t){(void)t;}
static int xTaskCreate(void (*f)(void *),const char *n,unsigned stack,void *ctx,int pri,void *out){(void)f;(void)n;(void)stack;(void)ctx;(void)pri;(void)out;return 1;}
struct dirent {char d_name[256];};
typedef struct {HANDLE handle;WIN32_FIND_DATAA data;bool first;struct dirent entry;} DIR;
static DIR *opendir(const char *path){char pattern[300];snprintf(pattern,sizeof(pattern),"%s/*",path);DIR *d=calloc(1,sizeof(*d));d->handle=FindFirstFileA(pattern,&d->data);if(d->handle==INVALID_HANDLE_VALUE){free(d);return NULL;}d->first=true;return d;}
static struct dirent *readdir(DIR *d){if(!d->first && !FindNextFileA(d->handle,&d->data))return NULL;d->first=false;strcpy(d->entry.d_name,d->data.cFileName);return &d->entry;}
static void closedir(DIR *d){FindClose(d->handle);free(d);}
'''
(OUT/"usb-fakes.h").write_text(headers, encoding="utf-8")
for name in ["driver/uart.h", "esp_littlefs.h", "esp_partition.h", "esp_heap_caps.h", "freertos/FreeRTOS.h", "freertos/task.h", "freertos/semphr.h", "unistd.h", "dirent.h"]:
    path=OUT/name
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text('#include "usb-fakes.h"\n',encoding="utf-8")
# assert and LVGL declarations precede the service double.
host = r'''
#include <assert.h>
#define BASE "usb-storage"
#include "ouse_installer.c"
static void usb_server(void) {
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
    _mkdir(BASE);ui_done=xSemaphoreCreateBinary();
    mount_failure=1;partition_kind=0;assert(mount_storage()!=ESP_OK && !format_count);
    partition_kind=2;assert(mount_storage()!=ESP_OK && !format_count);
    partition_kind=3;assert(mount_storage()!=ESP_OK && !format_count);
    partition_kind=1;assert(mount_storage()==ESP_OK && format_count==1);
    partition_kind=0;
    assert(mount_storage()==ESP_OK);restore_packages();
    rx_line=malloc(OUSE_WIRE_MAX);puts("HOST READY");fflush(stdout);
    int c;while((c=getchar())!=EOF) {
        if(c==1){activity=esp_timer_get_time()-31000000;consume_bytes(NULL,0);continue;}
        if(c==2){fail_sync=1;continue;}
        if(c==3){fail_rename=1;continue;}
        if(c==4){fail_ui_lock=1;continue;}
        if(c==5){fail_unlink=1;continue;}
        unsigned char byte=(unsigned char)c;consume_bytes(&byte,1);
    }
    for(unsigned i=0;i<OUSE_APP_CAPACITY;++i)if(installed[i]){
        assert(on_ui(2,NULL,installed[i]->id)==ESP_OK);ouse_bpk_free(installed[i]);installed[i]=NULL;
    }
    abort_upload();free(rx_line);vSemaphoreDelete(ui_done);
    ouse_runtime_stats_t stats;ouse_runtime_get_stats(&stats);assert(!stats.instances && !stats.contexts && !stats.timers);
}
'''
(OUT/"usb-host.h").write_text(host,encoding="utf-8")
