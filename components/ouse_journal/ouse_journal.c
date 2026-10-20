// SPDX-License-Identifier: MIT
#include "ouse_journal.h"
#include "ouse_storage.h"
#include "ouse_clock.h"
#include "ouse_files.h"
#include "esp_littlefs.h"
#include <fcntl.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#define DIRECTORY OUSE_STORAGE_BASE "/system-logs"
#define SLOTS 32
#define CHUNK (OUSE_JOURNAL_LIMIT/SLOTS)
#define STRIDE (OUSE_JOURNAL_PAGE-512)
#define DAYS (7*24*60*60LL)
typedef struct {uint32_t magic,sequence;int64_t created;uint32_t boot,reserved;} header_t;
typedef struct {char text[384];int64_t uptime;int64_t epoch;} record_t;
typedef struct {unsigned slot;header_t header;size_t size;} segment_t;
static QueueHandle_t queue;
static SemaphoreHandle_t lock,flushed;
static vprintf_like_t previous;
static atomic_uint dropped;
static atomic_bool stopping,flush_requested,flush_ok;
static ouse_journal_view_t view;
static unsigned requested_page,request_revision=1,done_revision;
static bool clear_requested;
static uint32_t export_job;
static uint32_t boot_id;
static segment_t segments[SLOTS];static unsigned count;static FILE *stream;static size_t current_size;static uint32_t sequence;
static int64_t epoch_now(void){ouse_clock_state_t clock;ouse_clock_get(&clock);return clock.valid ? clock.utc_ms/1000 : 0;}
static bool directory_ready(void){struct stat info;return mkdir(DIRECTORY,0700)==0 || (stat(DIRECTORY,&info)==0&&S_ISDIR(info.st_mode));}
static void path_for(unsigned slot,char *path){snprintf(path,96,DIRECTORY "/%02u.log",slot);}
// No filesystem, allocation, blocking lock, or recursive logging in the log sink.
static int capture(const char *format,va_list args)
{
    va_list copy;va_copy(copy,args);record_t record={0};int length=vsnprintf(record.text,sizeof(record.text),format,copy);va_end(copy);
    if(length>=(int)sizeof(record.text)){size_t tail=strlen(record.text),start=tail;while(start&&((unsigned char)record.text[start-1]&0xc0)==0x80)start--;if(start){size_t lead=start-1;unsigned char code=record.text[lead];unsigned needed=code<128?1:code<224?2:code<240?3:4;if(tail-lead<needed)record.text[lead]=0;}}
    int result=previous ? previous(format,args) : 0;
    bool important=strstr(record.text,"E (") || strstr(record.text,"W (");
    bool event=strstr(record.text,"OuseCore:") || strstr(record.text,"OuseInstaller:") || strstr(record.text,"OuseIdentity:") || strstr(record.text,"OuseHealth:");
    // User script output and high-frequency display/touch/network INFO stay UART-only.
    if((important||event) && !atomic_load(&stopping)){
        char lower[384];size_t n=strlen(record.text);for(size_t i=0;i<=n;i++)lower[i]=(record.text[i]>='A'&&record.text[i]<='Z')?record.text[i]+32:record.text[i];
        if(strstr(lower,"password")||strstr(lower,"authorization")||strstr(lower,"token=")||strstr(lower,"pin="))return result;
        record.uptime=esp_timer_get_time()/1000;record.epoch=epoch_now();
        if(xQueueSend(queue,&record,0)!=pdTRUE)atomic_fetch_add(&dropped,1);
    }
    return result;
}
static bool close_stream(void){if(!stream)return true;bool ok=fflush(stream)==0;if(ok)ok=fsync(fileno(stream))==0;if(fclose(stream))ok=false;stream=NULL;return ok;}
static void scan(void)
{
    count=0;int64_t now=epoch_now();
    for(unsigned slot=0;slot<SLOTS;slot++){
        char path[96];path_for(slot,path);FILE *f=fopen(path,"rb");if(!f)continue;
        header_t h;struct stat st;bool valid=fread(&h,1,sizeof(h),f)==sizeof(h)&&h.magic==0x31474f4c&&fstat(fileno(f),&st)==0&&st.st_size>=(long)sizeof(h)&&st.st_size<=CHUNK;fclose(f);
        if(!valid || (now&&h.created&&now>=h.created&&now-h.created>=DAYS)){unlink(path);continue;}
        // Unknown-time segments keep their original uptime text. Start a retention
        // window once wall time becomes available; no false calendar date is shown.
        if(now&&!h.created){h.created=now;f=fopen(path,"r+b");if(f){fwrite(&h,1,sizeof(h),f);fflush(f);fsync(fileno(f));fclose(f);}}
        unsigned index=count++;segments[index]=(segment_t){slot,h,(size_t)st.st_size};
        if(h.sequence>sequence)sequence=h.sequence;
    }
    for(unsigned i=1;i<count;i++){segment_t item=segments[i];unsigned j=i;while(j&&segments[j-1].header.sequence>item.header.sequence){segments[j]=segments[j-1];j--;}segments[j]=item;}
}
static bool append(const record_t *record)
{
    char line[512],stamp[48];if(record->epoch){time_t seconds=record->epoch;struct tm utc;gmtime_r(&seconds,&utc);strftime(stamp,sizeof(stamp),"%Y-%m-%d %H:%M:%SZ",&utc);}else snprintf(stamp,sizeof(stamp),"time pending boot=%08lx +%lldms",(unsigned long)boot_id,(long long)record->uptime);
    // Strip terminal color/control codes before keeping human-readable text.
    char clean[384];size_t used=0;bool escape=false;for(const unsigned char *s=(const unsigned char *)record->text;*s&&used<sizeof(clean)-1;s++){if(*s==27){escape=true;continue;}if(escape){if(*s=='m')escape=false;continue;}if(*s>=32||*s=='\t')clean[used++]=*s;}clean[used]=0;
    int length=snprintf(line,sizeof(line),"[%s] %s\n",stamp,clean);if(length<0)return false;size_t bytes=(size_t)length;
    int64_t now=record->epoch;
    if(stream&&(current_size+bytes>CHUNK || (now&&count&&segments[count-1].header.created&&now/86400!=segments[count-1].header.created/86400)))if(!close_stream())return false;
    if(!stream){scan();if(count){segment_t *last=&segments[count-1];if(last->size+bytes<=CHUNK && (!now || !last->header.created || now/86400==last->header.created/86400)){char path[96];path_for(last->slot,path);stream=fopen(path,"ab");if(stream)current_size=last->size;}}}
    if(!stream){unsigned slot=count==SLOTS?segments[0].slot:0;if(count<SLOTS){bool taken[SLOTS]={0};for(unsigned i=0;i<count;i++)taken[segments[i].slot]=true;while(slot<SLOTS&&taken[slot])slot++;}char path[96];path_for(slot,path);stream=fopen(path,"wb");if(!stream)return false;header_t h={.magic=0x31474f4c,.sequence=++sequence,.created=now,.boot=boot_id};current_size=sizeof(h);if(fwrite(&h,1,sizeof(h),stream)!=sizeof(h)){close_stream();return false;}if(count==SLOTS){memmove(segments,segments+1,(SLOTS-1)*sizeof(*segments));count--;}segments[count++]=(segment_t){slot,h,current_size};}
    if(fwrite(line,1,bytes,stream)!=bytes){close_stream();return false;}current_size+=bytes;segments[count-1].size=current_size;return true;
}
static void publish(unsigned revision,unsigned page)
{
    static ouse_journal_view_t next;memset(&next,0,sizeof(next));next.ready=true;next.revision=revision;next.dropped=atomic_load(&dropped);scan();size_t payload=0;for(unsigned i=0;i<count;i++){next.bytes+=segments[i].size;payload+=segments[i].size-sizeof(header_t);}next.pages=(unsigned)((payload+STRIDE-1)/STRIDE);if(!next.pages)next.pages=1;if(page>=next.pages)page=next.pages-1;next.page=page;
    size_t end=payload>(size_t)page*STRIDE?payload-(size_t)page*STRIDE:0,start=end>OUSE_JOURNAL_PAGE?end-OUSE_JOURNAL_PAGE:0,position=0,used=0;
    for(unsigned i=0;i<count&&position<end;i++){size_t size=segments[i].size-sizeof(header_t);if(position+size>start){char path[96];path_for(segments[i].slot,path);FILE *f=fopen(path,"rb");if(f){size_t offset=start>position?start-position:0,amount=size-offset;if(amount>end-position-offset)amount=end-position-offset;fseek(f,(long)(sizeof(header_t)+offset),SEEK_SET);used+=fread(next.text+used,1,amount,f);fclose(f);}else next.error=ESP_FAIL;}position+=size;}
    // Skip any partial leading line/UTF-8 sequence at the paging boundary.
    if(start&&used){char *newline=memchr(next.text,'\n',used);if(newline){size_t skip=(size_t)(newline-next.text)+1;memmove(next.text,next.text+skip,used-skip);used-=skip;}}if(page&&used){while(used&&next.text[used-1]!='\n')used--;}next.text[used]=0;
    xSemaphoreTake(lock,portMAX_DELAY);if(request_revision==revision){next.exporting=view.exporting;next.export_error=view.export_error;memcpy(next.exported,view.exported,sizeof(next.exported));view=next;done_revision=revision;}xSemaphoreGive(lock);
}
// A storage task record makes shutdown wait for an accepted export. A leased
// SD mount cannot be ejected while the temporary file is being committed.
static esp_err_t export_logs(char *exported,size_t capacity)
{
    if(!ouse_files_lease_begin())return ESP_ERR_INVALID_STATE;
    char relative[96],target[180],temporary[192];snprintf(relative,sizeof(relative),"Downloads/system-logs-%08lx-%lld.txt",(unsigned long)boot_id,(long long)(esp_timer_get_time()/1000));
    esp_err_t error=ESP_FAIL;FILE *out=NULL;
    if(ouse_files_path(relative,target,sizeof(target))){
        snprintf(temporary,sizeof(temporary),"%s.part",target);int fd=open(temporary,O_WRONLY|O_CREAT|O_EXCL,0666);if(fd>=0){out=fdopen(fd,"wb");if(!out){close(fd);unlink(temporary);}}
        if(out){
            bool ok=close_stream();scan();char buffer[512];
            for(unsigned i=0;i<count&&ok;i++){char path[96];path_for(segments[i].slot,path);FILE *in=fopen(path,"rb");if(!in){ok=false;break;}if(fseek(in,sizeof(header_t),SEEK_SET))ok=false;size_t n;while(ok&&(n=fread(buffer,1,sizeof(buffer),in))>0)if(fwrite(buffer,1,n,out)!=n)ok=false;if(ferror(in))ok=false;fclose(in);}
            if(fflush(out)||fsync(fileno(out)))ok=false;
            if(fclose(out))ok=false;
            struct stat existing;
            if(ok&&stat(target,&existing)!=0&&rename(temporary,target)==0){snprintf(exported,capacity,"Ouse/%s",relative);error=ESP_OK;}else unlink(temporary);
        }
    }
    ouse_files_lease_end();return error;
}
esp_err_t ouse_journal_export(void)
{
    if(!lock)return ESP_ERR_INVALID_STATE;
    if(!ouse_tasks_submission_begin())return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(lock,portMAX_DELAY);esp_err_t error=ESP_ERR_INVALID_STATE;
    if(view.ready&&!view.exporting){ouse_task_owner_t owner={.app_id="ouse.logs"};uint32_t id;error=ouse_task_create(&owner,OUSE_TASK_STORAGE,"log-export",OUSE_TASK_FINISH_ON_CLOSE,false,&id);if(error==ESP_OK){export_job=id;view.exporting=true;view.export_error=ESP_OK;view.exported[0]=0;}}
    xSemaphoreGive(lock);ouse_tasks_submission_end();return error;
}
static void worker(void *unused)
{
    (void)unused;int64_t last_sync=esp_timer_get_time(),last_scan=0;bool mounted=false;esp_err_t error=ESP_OK;size_t free_bytes=SIZE_MAX;int64_t next_space=0;
    for(;;){
        ouse_storage_stats_t storage;ouse_storage_get_stats(&storage);if(!mounted&&storage.ready){mounted=directory_ready();if(mounted)scan();}
        if(!mounted){vTaskDelay(pdMS_TO_TICKS(100));continue;}
        record_t record;if(xQueueReceive(queue,&record,pdMS_TO_TICKS(100))==pdTRUE){int64_t at=esp_timer_get_time();if(at>=next_space||free_bytes<128*1024){size_t total,used;if(esp_littlefs_info("littlefs_data",&total,&used)==ESP_OK)free_bytes=total>used?total-used:0;next_space=at+2000000;}if(free_bytes<128*1024){scan();if(count){close_stream();char path[96];path_for(segments[0].slot,path);unlink(path);}atomic_fetch_add(&dropped,1);error=ESP_ERR_NO_MEM;}else if(!append(&record))error=ESP_FAIL;else if(free_bytes!=SIZE_MAX)free_bytes=free_bytes>512?free_bytes-512:0;}
        int64_t now=esp_timer_get_time();if(stream&&now-last_sync>=2000000){if(fflush(stream)||fsync(fileno(stream)))error=ESP_FAIL;last_sync=now;}
        if(now-last_scan>=60000000){if(!close_stream())error=ESP_FAIL;scan();last_scan=now;}
        xSemaphoreTake(lock,portMAX_DELAY);unsigned revision=request_revision,page=requested_page;bool clear=clear_requested;clear_requested=false;uint32_t export_id=export_job;export_job=0;xSemaphoreGive(lock);
        if(clear){close_stream();for(unsigned i=0;i<SLOTS;i++){char path[96];path_for(i,path);unlink(path);}count=0;while(xQueueReceive(queue,&record,0)==pdTRUE){}atomic_store(&dropped,0);error=ESP_OK;}
        if(export_id){char exported[128]="";esp_err_t exported_error=ouse_task_start(export_id,false)?export_logs(exported,sizeof(exported)):ESP_ERR_INVALID_STATE;ouse_task_finish(export_id,exported_error,false);xSemaphoreTake(lock,portMAX_DELAY);view.exporting=false;view.export_error=exported_error;snprintf(view.exported,sizeof(view.exported),"%s",exported);xSemaphoreGive(lock);}
        if(revision!=done_revision){if(stream)fflush(stream);publish(revision,page);}
        xSemaphoreTake(lock,portMAX_DELAY);view.error=error;view.dropped=atomic_load(&dropped);xSemaphoreGive(lock);
        if(atomic_load(&flush_requested)){while(xQueueReceive(queue,&record,0)==pdTRUE)if(!append(&record))error=ESP_FAIL;if(!close_stream())error=ESP_FAIL;atomic_store(&flush_ok,error==ESP_OK);atomic_store(&flush_requested,false);xSemaphoreGive(flushed);}
    }
}
esp_err_t ouse_journal_start(void)
{
    if(queue)return ESP_ERR_INVALID_STATE;
    queue=xQueueCreate(32,sizeof(record_t));lock=xSemaphoreCreateMutex();flushed=xSemaphoreCreateBinary();if(!queue||!lock||!flushed){if(queue)vQueueDelete(queue);if(lock)vSemaphoreDelete(lock);if(flushed)vSemaphoreDelete(flushed);queue=NULL;lock=flushed=NULL;return ESP_ERR_NO_MEM;}
    boot_id=esp_random();if(xTaskCreate(worker,"ouse_logs",6144,NULL,1,NULL)!=pdPASS){vQueueDelete(queue);vSemaphoreDelete(lock);vSemaphoreDelete(flushed);queue=NULL;lock=flushed=NULL;return ESP_ERR_NO_MEM;}
    previous=esp_log_set_vprintf(capture);ESP_LOGI("OuseCore","Persistent journal started; reset=%d; 1MiB/7 days",esp_reset_reason());return ESP_OK;
}
void ouse_journal_request(unsigned page,bool clear){if(!lock)return;xSemaphoreTake(lock,portMAX_DELAY);requested_page=page;clear_requested|=clear;request_revision++;view.busy=true;xSemaphoreGive(lock);}
void ouse_journal_get(ouse_journal_view_t *out){if(!out)return;memset(out,0,sizeof(*out));if(!lock)return;xSemaphoreTake(lock,portMAX_DELAY);*out=view;out->busy=request_revision!=done_revision;xSemaphoreGive(lock);}
bool ouse_journal_flush(unsigned timeout_ms){if(!queue)return true;(void)xSemaphoreTake(flushed,0);atomic_store(&stopping,true);atomic_store(&flush_requested,true);bool ok=xSemaphoreTake(flushed,pdMS_TO_TICKS(timeout_ms))==pdTRUE&&atomic_load(&flush_ok);atomic_store(&stopping,false);return ok;}
