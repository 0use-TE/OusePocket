// SPDX-License-Identifier: MIT
// Trusted Quake II host bridge. Every allocation/file belongs to one worker;
// its return (including trapped _Exit/assert) closes files and frees its heap.
#include "ouse_native_game.h"
#include "ouse_native.h"
#include "ouse_native_api.h"
#include "ouse_files.h"
#include "ouse_media.h"
#include "esp_elf.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static StaticSemaphore_t storage;
static SemaphoreHandle_t guard;
static uint16_t *pixels;
static unsigned frames,consumed,held_keys,allocation_count;
static size_t allocation_bytes;
static bool running,leased;
static uint32_t audio_id;
static unsigned audio_written,audio_nonzero,audio_peak;
static int mouse_x,mouse_y;
static int64_t input_at,activity_at;
static EXT_RAM_BSS_ATTR uint8_t keys[256],delivered[256];
static EXT_RAM_BSS_ATTR struct {uint8_t key,down;} events[128];
static unsigned event_read,event_write;
static jmp_buf escape;
static bool can_escape;
typedef struct block {struct block *next,*prev;size_t bytes;uint32_t magic;} block_t;
static block_t *blocks;
static FILE *files[24];
#define HEADER 64u
#define MAGIC 0x51414c4cu
static void lock(void){xSemaphoreTake(guard,portMAX_DELAY);}
static void unlock(void){xSemaphoreGive(guard);}
void *ouse_game_malloc(size_t size)
{
    if(!running || size>20u*1024u*1024u || size>SIZE_MAX-HEADER)return NULL;
    block_t *b=heap_caps_aligned_alloc(64,HEADER+size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!b)return NULL;
    b->bytes=size;b->magic=MAGIC;b->prev=NULL;b->next=blocks;
    if(blocks)blocks->prev=b;
    blocks=b;
    lock();++allocation_count;allocation_bytes+=size;unlock();
    return (uint8_t *)b+HEADER;
}
void ouse_game_free(void *pointer)
{
    if(!pointer)return;
    block_t *b=(block_t *)((uint8_t *)pointer-HEADER);
    if(b->magic!=MAGIC)return;
    if(b->prev)b->prev->next=b->next;else blocks=b->next;
    if(b->next)b->next->prev=b->prev;
    lock();--allocation_count;allocation_bytes-=b->bytes;unlock();
    b->magic=0;heap_caps_free(b);
}
void *ouse_game_calloc(size_t count,size_t size)
{if(size && count>SIZE_MAX/size)return NULL;void *p=ouse_game_malloc(count*size);if(p)memset(p,0,count*size);return p;}
void *ouse_game_realloc(void *p,size_t size)
{
    if(!p)return ouse_game_malloc(size);
    if(!size){ouse_game_free(p);return NULL;}
    block_t *b=(block_t *)((uint8_t *)p-HEADER);if(b->magic!=MAGIC)return NULL;
    void *next=ouse_game_malloc(size);if(next){memcpy(next,p,size<b->bytes?size:b->bytes);ouse_game_free(p);}return next;
}
FILE *ouse_game_fopen(const char *path,const char *mode)
{
    if(!running || !path || !mode)return NULL;
    const char *actual=path;
    if(!strcmp(path,"/sdcard/Ouse/Quake2/baseq2/pak0.pak"))actual="/sdcard/Ouse/Downloads/quake2.pak";
    if(!strcmp(path,"/sdcard/Ouse/Quake2/baseq2/pak1.pak"))actual="/sdcard/Ouse/Downloads/quake2-sound.pak";
    for(unsigned i=0;i<24;++i)if(!files[i]){files[i]=fopen(actual,mode);return files[i];}
    return NULL;
}
int ouse_game_fclose(FILE *file)
{for(unsigned i=0;i<24;++i)if(files[i]==file){files[i]=NULL;return fclose(file);}return EOF;}
_Noreturn void ouse_game_exit(int code){if(can_escape)longjmp(escape,code?code:256);abort();}
static _Noreturn void game_assert(const char *file,int line,const char *function,const char *condition)
{ESP_LOGE("OuseQuake2","Assertion %s:%d %s: %s",file,line,function,condition);ouse_game_exit(1);}
static char *game_getenv(const char *key){(void)key;return NULL;}
static int game_putenv(char *value){(void)value;return 0;}
static char *game_strdup(const char *value)
{size_t n=strlen(value)+1;char *p=ouse_game_malloc(n);if(p)memcpy(p,value,n);return p;}

int ouse_game_audio_open(void)
{
    static const ouse_task_owner_t owner={.app_id="system.quake2",.instance=0};
    audio_written=audio_nonzero=audio_peak=0;
    esp_err_t error=ouse_media_stream_begin(&owner,&audio_id);
    ESP_LOGI("OuseQuake2","Audio start: %s",esp_err_to_name(error));return error==ESP_OK;
}
unsigned ouse_game_audio_position(void){return ouse_media_stream_position(audio_id);}
unsigned ouse_game_audio_write(const int16_t *samples,unsigned count)
{
    unsigned sent=ouse_media_stream_write(audio_id,samples,count),nonzero=0,peak=0;
    for(unsigned i=0;i<sent;++i){unsigned v=samples[i]<0 ? -(int)samples[i] : samples[i];if(v)++nonzero;if(v>peak)peak=v;}
    lock();audio_written+=sent;audio_nonzero+=nonzero;if(peak>audio_peak)audio_peak=peak;unlock();return sent;
}
void ouse_game_audio_close(void){ouse_media_stream_end(audio_id);}
void ouse_game_audio_stats(unsigned *written,unsigned *nonzero,unsigned *peak,unsigned *played)
{
    if(!guard){*written=*nonzero=*peak=*played=0;return;}
    lock();*written=audio_written;*nonzero=audio_nonzero;*peak=audio_peak;unlock();
    *played=ouse_game_audio_position();
}
void ouse_game_frame(const unsigned char *source,const unsigned char *palette)
{
    uint16_t colors[256];
    for(unsigned i=0;i<256;++i)colors[i]=((palette[i*3]>>3)<<11)|((palette[i*3+1]>>2)<<5)|(palette[i*3+2]>>3);
    lock();if(pixels){for(unsigned i=0;i<320u*240u;++i)pixels[i]=colors[source[i]];++frames;}unlock();
}
bool ouse_game_copy_frame(uint16_t *destination,unsigned width,unsigned height,unsigned *serial)
{
    if(!guard)return false;
    lock();bool changed=pixels && frames!=*serial;
    if(changed){
        // Fill the landscape panel while keeping the tested renderer budget.
        for(unsigned y=0;y<height;++y){
            const uint16_t *row=pixels+(y*240u/height)*320u;
            for(unsigned x=0;x<width;++x)destination[y*width+x]=row[x*320u/width];
        }
        *serial=frames;consumed=frames;
    }
    unlock();return changed;
}
void ouse_game_release_input(void)
{if(guard){lock();memset(keys,0,sizeof(keys));event_read=event_write;held_keys=0;mouse_x=mouse_y=0;unlock();}}
esp_err_t ouse_game_input(unsigned key,bool down,int dx,int dy)
{
    if(!guard || key>255 || dx<-1024 || dx>1024 || dy<-1024 || dy>1024)return ESP_ERR_INVALID_ARG;
    lock();if(!running){unlock();return ESP_ERR_INVALID_STATE;}
    if(key && keys[key]!=down){
        if(event_write-event_read>=128){unlock();return ESP_ERR_NO_MEM;}
        held_keys+=down?1:-1;keys[key]=down;
        events[event_write&127].key=(uint8_t)key;events[event_write&127].down=down;++event_write;
    }
    if(key || dx || dy)activity_at=esp_timer_get_time();
    mouse_x+=dx;mouse_y+=dy;if(mouse_x>4096)mouse_x=4096;if(mouse_x<-4096)mouse_x=-4096;
    if(mouse_y>4096)mouse_y=4096;
    if(mouse_y<-4096)mouse_y=-4096;
    input_at=esp_timer_get_time();unlock();return ESP_OK;
}
int64_t ouse_game_input_activity(void)
{if(!guard)return 0;lock();int64_t value=activity_at;unlock();return value;}
int ouse_game_key(int *down,int *key)
{
    lock();if(input_at && esp_timer_get_time()-input_at>750000){memset(keys,0,sizeof(keys));event_read=event_write;held_keys=0;}
    if(event_read!=event_write){
        unsigned at=event_read++&127;*key=events[at].key;*down=events[at].down;delivered[*key]=(uint8_t)*down;
        unlock();return 1;
    }
    for(unsigned i=1;i<256;++i)if(keys[i]!=delivered[i]){
        delivered[i]=keys[i];*key=(int)i;*down=keys[i];unlock();return 1;
    }
    unlock();return 0;
}
void ouse_game_mouse(int *dx,int *dy){lock();*dx=mouse_x;*dy=mouse_y;mouse_x=mouse_y=0;unlock();}
void ouse_game_get_stats(unsigned *f,unsigned *k,unsigned *a,size_t *b)
{if(!guard){*f=*k=*a=0;*b=0;return;}lock();*f=frames;*k=held_keys;*a=allocation_count;*b=allocation_bytes;unlock();}
esp_err_t ouse_game_begin(void)
{
    if(!guard)guard=xSemaphoreCreateMutexStatic(&storage);
    if(!ouse_files_lease_begin())return ESP_ERR_INVALID_STATE;
    leased=true;
    FILE *pak=fopen("/sdcard/Ouse/Downloads/quake2.pak","rb");
    if(!pak){ouse_files_lease_end();leased=false;return ESP_ERR_NOT_FOUND;}
    unsigned char header[12];bool valid=fread(header,1,sizeof(header),pak)==sizeof(header) && !memcmp(header,"PACK",4);fclose(pak);
    if(!valid){ouse_files_lease_end();leased=false;return ESP_ERR_INVALID_ARG;}
    uint16_t *buffer=heap_caps_calloc(320u*240u,2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!buffer){ouse_files_lease_end();leased=false;return ESP_ERR_NO_MEM;}
    mkdir("/sdcard/Ouse/Quake2",0700);mkdir("/sdcard/Ouse/Quake2/baseq2",0700);
    lock();pixels=buffer;frames=consumed=0;running=true;memset(keys,0,256);memset(delivered,0,256);
    event_read=event_write=0;input_at=activity_at=0;unlock();
    return ESP_OK;
}
void ouse_game_end(void)
{
    if(!guard)return;
    ouse_game_audio_close();if(audio_id)ouse_media_purge("system.quake2");
    ouse_game_release_input();lock();running=false;uint16_t *old=pixels;pixels=NULL;unlock();
    free(old);for(unsigned i=0;i<24;++i)if(files[i]){fclose(files[i]);files[i]=NULL;}
    while(blocks)ouse_game_free((uint8_t *)blocks+HEADER);
    if(leased){ouse_files_lease_end();leased=false;}
}
static int game_guard(int (*entry)(int,char **),int argc,char **argv)
{
    can_escape=true;int result=setjmp(escape);
    if(!result)result=entry(argc,argv);
    else if(result==256)result=0;
    can_escape=false;return result;
}
static struct {int (*entry)(int,char **);int argc;char **argv;int result;SemaphoreHandle_t done;StaticSemaphore_t storage;} execution;
static void game_task(void *unused)
{
    (void)unused;execution.result=game_guard(execution.entry,execution.argc,execution.argv);
    xSemaphoreGive(execution.done);
    // The internal-stack parent owns deletion and waits for this task to stop
    // before releasing its PSRAM stack and the executable image.
    for(;;)vTaskSuspend(NULL);
}
esp_err_t ouse_game_execute(int (*entry)(int,char **),int argc,char **argv,int *exit_code)
{
    if(!execution.done)execution.done=xSemaphoreCreateBinaryStatic(&execution.storage);
    execution.entry=entry;execution.argc=argc;execution.argv=argv;
    TaskHandle_t task=NULL;
    if(xTaskCreateWithCaps(game_task,"ouse_quake2",256u*1024u,NULL,2,&task,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS)
        return ESP_ERR_NO_MEM;
    xSemaphoreTake(execution.done,portMAX_DELAY);
    vTaskDeleteWithCaps(task);
    *exit_code=execution.result;return ESP_OK;
}

// Compiler helpers used by the original engine's double arithmetic. Their
// addresses are exported, never called with these declarations in this TU.
#define HELPER(name) extern void name(void);
HELPER(__adddf3) HELPER(__divdf3) HELPER(__eqdf2) HELPER(__extendsfdf2)
HELPER(__fixdfsi) HELPER(__fixunsdfsi) HELPER(__floatsidf) HELPER(__gedf2)
HELPER(__gtdf2) HELPER(__ledf2) HELPER(__ltdf2) HELPER(__muldf3)
HELPER(__subdf3) HELPER(__truncdfsf2)
#define __assert_func game_assert
#define getenv game_getenv
#define putenv game_putenv
#define strdup game_strdup
static const struct esp_elfsym symbols[]={
#define GAME_IMPORT(symbol) {#symbol,(void *)&symbol},
#include "ouse_game_imports.inc"
#undef GAME_IMPORT
    ESP_ELFSYM_END
};
esp_err_t ouse_game_register_symbols(void)
{return esp_elf_register_symbol(symbols)?ESP_FAIL:ESP_OK;}
