#include "ouse_recorder.h"
#include "ouse_app_files.h"
#include "ouse_devices.h"
#include "ouse_files.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/idf_additions.h"
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <utime.h>

typedef struct {ouse_task_owner_t owner;unsigned operation,offset;uint32_t id;char path[128],name[80];} job_t;
static SemaphoreHandle_t mutex;
static QueueHandle_t jobs;
static StreamBufferHandle_t pcm;
static ouse_recorder_state_t *state;
static ouse_task_owner_t owner;
static bool producer_done;
static void lock(void){xSemaphoreTake(mutex,portMAX_DELAY);}
static void unlock(void){xSemaphoreGive(mutex);}
static bool card_ready(void){ouse_files_state_t card;ouse_files_get(&card);return card.ready && card.present;}
static bool same(const ouse_task_owner_t *candidate) {
  return candidate && owner.instance == candidate->instance &&
         !strcmp(owner.app_id, candidate->app_id);
}
static bool recording_path(const char *id, const char *path,
                           char absolute[180]) {
  char prefix[96];
  int n = snprintf(prefix, sizeof(prefix), "/Ouse/Apps/%s/Recordings/", id);
  if (!path || n < 0 || strncmp(path, prefix, n))
    return false;
  const char *name = path + n, *extension = strrchr(name, '.');
  if (strchr(name, '/') || !extension || strcasecmp(extension, ".wav"))
    return false;
  char relative[128];
  if (snprintf(relative, sizeof(relative), "Recordings/%s", name) >=
      (int)sizeof(relative))
    return false;
  return ouse_app_files_path(id, relative, absolute, 180);
}
static bool prepare_recordings(const char *id) {
  char absolute[180];
  if (ouse_app_files_prepare(id) != ESP_OK ||
      !ouse_app_files_path(id, "Recordings", absolute, sizeof(absolute)))
    return false;
  if (!mkdir(absolute, 0755))
    return true;
  struct stat st;
  return !stat(absolute, &st) && S_ISDIR(st.st_mode);
}
static void u32(unsigned char *target, uint32_t value) {
  for (unsigned index = 0; index < 4; ++index)
    target[index] = (unsigned char)(value >> (index * 8));
}
static bool wave_header(FILE *file,uint32_t bytes)
{
    unsigned char header[44]={0};memcpy(header,"RIFF",4);u32(header+4,bytes+36);memcpy(header+8,"WAVEfmt ",8);u32(header+16,16);
    header[20]=1;header[22]=1;u32(header+24,16000);u32(header+28,32000);header[32]=2;header[34]=16;memcpy(header+36,"data",4);u32(header+40,bytes);
    return fseek(file,0,SEEK_SET)==0 && fwrite(header,1,sizeof(header),file)==sizeof(header);
}
void ouse_recorder_get(ouse_recorder_state_t *out)
{
    if(!out)return;
    if(!mutex || !state){*out=(ouse_recorder_state_t){.error=ESP_ERR_NOT_SUPPORTED};return;}
    lock();*out=*state;unlock();
}
bool ouse_recorder_current(uint32_t id)
{
    if(!state)return false;
    lock();bool current=state->active && state->task_id==id && !state->stopping;unlock();return current && !ouse_task_cancel_requested(id);
}
bool ouse_recorder_idle(void)
{
    if(!state)return true;
    lock();bool idle=!state->active && !state->busy;unlock();return idle;
}
bool ouse_recorder_write(uint32_t id,const int16_t *samples,unsigned count)
{
    if(!samples || count>2048 || !ouse_recorder_current(id))return false;
    size_t bytes=count*2;
    if(xStreamBufferSend(pcm,samples,bytes,0)==bytes)return true;
    lock();if(state->active && state->task_id==id){state->error=ESP_ERR_NO_MEM;state->stopping=true;}unlock();return false;
}
void ouse_recorder_producer_done(uint32_t id,esp_err_t error)
{
    if(!state)return;
    lock();if(state->active && state->task_id==id){producer_done=true;state->stopping=true;if(error!=ESP_OK)state->error=error;}unlock();
}
bool ouse_recorder_stop(const ouse_task_owner_t *candidate)
{
    if(!state)return false;
    lock();bool found=state->active && same(candidate);if(found){state->stopping=true;++state->revision;}unlock();return found;
}
static void save_recording(void)
{
    char path[128],absolute[180],temporary[192];uint32_t id;
    lock();strcpy(path,state->path);id=state->task_id;unlock();
    bool leased=ouse_files_lease_begin();
    bool valid=leased && prepare_recordings(owner.app_id) && recording_path(owner.app_id,path,absolute);snprintf(temporary,sizeof(temporary),"%s.part",valid ? absolute : "");
    int descriptor=valid && card_ready() ? open(temporary,O_CREAT|O_EXCL|O_WRONLY,0600) : -1;
    FILE *file=descriptor>=0 ? fdopen(descriptor,"wb") : NULL;
    if(!file && descriptor>=0)close(descriptor);
    esp_err_t error=file && wave_header(file,0) ? ESP_OK : ESP_FAIL;
    if(error!=ESP_OK){lock();state->error=error;state->stopping=true;unlock();}
    uint32_t bytes=0;unsigned char buffer[4096];
    for(;;){
        size_t count=xStreamBufferReceive(pcm,buffer,sizeof(buffer),pdMS_TO_TICKS(30));
        if(count && error==ESP_OK){
            if(!card_ready() || bytes+count>115200000U || fwrite(buffer,1,count,file)!=count)error=ESP_FAIL;
            else bytes+=count;
        }
        lock();state->duration_ms=bytes/32;if(error!=ESP_OK){state->error=error;state->stopping=true;}bool done=producer_done;++state->revision;unlock();
        if(!done){ouse_task_snapshot_t task;if(ouse_task_get(NULL,id,&task) && task.state>=OUSE_TASK_SUCCEEDED)done=true;}
        if(done && !xStreamBufferBytesAvailable(pcm))break;
    }
    lock();if(error==ESP_OK)error=state->error;unlock();
    if(file){
        if(!wave_header(file,bytes) || fflush(file) || fsync(fileno(file)))error=ESP_FAIL;
        if(fclose(file))error=ESP_FAIL;
    }
    int64_t ended=time(NULL);
    if(error==ESP_OK && !bytes)error=ESP_ERR_INVALID_SIZE;
    if(error==ESP_OK && bytes){struct stat existing;if(stat(absolute,&existing)==0 || rename(temporary,absolute))error=ESP_FAIL;
        else if(ended>=1577836800){struct utimbuf stamp={.actime=ended,.modtime=ended};(void)utime(absolute,&stamp);}}
    else if(!bytes && valid)(void)unlink(temporary);
    lock();state->active=false;state->stopping=false;state->error=error;state->ended=ended;state->duration_ms=bytes/32;if(error!=ESP_OK || !bytes)state->path[0]=0;++state->revision;unlock();
    ouse_task_result_t result={.present=true,.value.audio={.recorded_ms=bytes/32}};
    ouse_task_finish_result(id,error,ouse_task_cancel_requested(id),&result);
    if(leased)ouse_files_lease_end();
}
static int newest(const void *first,const void *second)
{
    const ouse_recording_entry_t *left=first,*right=second;
    if(left->ended!=right->ended)return left->ended>right->ended ? -1 : 1;
    return strcmp(right->path,left->path);
}
static esp_err_t scan(const job_t *job)
{
    char directory[180];if(!prepare_recordings(job->owner.app_id)||!ouse_app_files_path(job->owner.app_id,"Recordings",directory,sizeof(directory)))return ESP_FAIL;
    DIR *handle=opendir(directory);if(!handle)return ESP_FAIL;
    unsigned capacity=job->offset+OUSE_RECORDER_PAGE+1,count=0;
    ouse_recording_entry_t *entries=heap_caps_calloc(capacity,sizeof(*entries),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!entries){closedir(handle);return ESP_ERR_NO_MEM;}
    esp_err_t error=ESP_OK;struct dirent *entry;int64_t deadline=esp_timer_get_time()+3000000;
    while((entry=readdir(handle))){
        if(esp_timer_get_time()>deadline || ouse_task_cancel_requested(job->id)){error=ESP_ERR_TIMEOUT;break;}
        char path[128],absolute[180];if(snprintf(path,sizeof(path),"/Ouse/Apps/%s/Recordings/%s",job->owner.app_id,entry->d_name)>=sizeof(path) || !recording_path(job->owner.app_id,path,absolute))continue;
        struct stat info;if(stat(absolute,&info) || !S_ISREG(info.st_mode) || info.st_size<44)continue;
        ouse_recording_entry_t candidate={.bytes=info.st_size,.duration_ms=(info.st_size-44)/32,.ended=info.st_mtime};strcpy(candidate.path,path);
        if(count<capacity){entries[count++]=candidate;qsort(entries,count,sizeof(*entries),newest);}
        else if(newest(&candidate,&entries[count-1])<0){entries[count-1]=candidate;qsort(entries,count,sizeof(*entries),newest);}
    }
    closedir(handle);
    if(error==ESP_OK){lock();unsigned available=count>job->offset ? count-job->offset : 0;state->more=available>OUSE_RECORDER_PAGE;state->count=available>OUSE_RECORDER_PAGE ? OUSE_RECORDER_PAGE : available;state->offset=job->offset;
        if(state->count)memcpy(state->entries,entries+job->offset,state->count*sizeof(*entries));
        ++state->revision;unlock();}
    free(entries);return error;
}
static void worker(void *unused)
{
    (void)unused;
    for(;;){
        lock();bool active=state->active;unlock();if(active){save_recording();continue;}
        job_t *job=NULL;if(xQueueReceive(jobs,&job,pdMS_TO_TICKS(20))!=pdTRUE)continue;
        esp_err_t error=ESP_ERR_INVALID_STATE;bool claimed=ouse_task_start(job->id,true);
        bool leased=claimed && ouse_files_lease_begin();
        if(leased && card_ready() && !ouse_task_cancel_requested(job->id)){
            if(job->operation==0)error=scan(job);
            else{char absolute[180];if(!recording_path(job->owner.app_id,job->path,absolute))error=ESP_ERR_INVALID_ARG;
                else if(job->operation==2)error=unlink(absolute) ? ESP_FAIL : ESP_OK;
                else{char path[128],destination[180];struct stat info;int n=snprintf(path,sizeof(path),"/Ouse/Apps/%s/Recordings/%s.wav",job->owner.app_id,job->name);
                    error=n<0||n>=(int)sizeof(path)?ESP_ERR_INVALID_ARG:!recording_path(job->owner.app_id,path,destination) ? ESP_ERR_INVALID_ARG : !strcmp(absolute,destination) ? ESP_OK : stat(destination,&info)==0 ? ESP_ERR_INVALID_STATE : rename(absolute,destination) ? ESP_FAIL : ESP_OK;
                    if(error==ESP_OK){lock();if(!strcmp(state->path,job->path))strcpy(state->path,path);unlock();}
                }
            }
        }
        if(leased)ouse_files_lease_end();
        lock();state->busy=false;state->error=error;++state->revision;unlock();ouse_task_finish(job->id,error,!claimed || ouse_task_cancel_requested(job->id));free(job);
    }
}
esp_err_t ouse_recorder_init(void)
{
    if(state)return ESP_OK;
    mutex=xSemaphoreCreateMutex();state=heap_caps_calloc(1,sizeof(*state),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);jobs=xQueueCreate(4,sizeof(job_t *));pcm=xStreamBufferCreateWithCaps(65536,1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(mutex && state && jobs && pcm && xTaskCreateWithCaps(worker,"ouse_recorder",8192,NULL,3,NULL,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS)return ESP_OK;
    if(pcm)vStreamBufferDeleteWithCaps(pcm);
    if(jobs)vQueueDelete(jobs);
    if(mutex)vSemaphoreDelete(mutex);
    free(state);state=NULL;mutex=NULL;jobs=NULL;pcm=NULL;return ESP_ERR_NO_MEM;
}
esp_err_t ouse_recorder_start(const ouse_task_owner_t *candidate,uint32_t *id)
{
    if(!state || !candidate || !candidate->instance || !id || !card_ready())return ESP_ERR_INVALID_STATE;
    ouse_files_state_t card;ouse_files_get(&card);if(card.free_bytes<1024*1024)return ESP_ERR_NO_MEM;
    lock();if(state->active || state->busy){unlock();return ESP_ERR_INVALID_STATE;}
    xStreamBufferReset(pcm);producer_done=false;owner=*candidate;state->stopping=false;state->error=ESP_OK;state->duration_ms=0;state->ended=0;
    snprintf(state->path,sizeof(state->path),"/Ouse/Apps/%s/Recordings/REC_%lld_%08lx.wav",candidate->app_id,(long long)time(NULL),(unsigned long)esp_random());
    esp_err_t error=ouse_audio_submit(candidate,OUSE_AUDIO_DISK_CAPTURE,0,id);
    if(error==ESP_OK){state->active=true;state->task_id=*id;++state->revision;}else state->path[0]=0;unlock();return error;
}
esp_err_t ouse_recorder_request(const ouse_task_owner_t *candidate,unsigned operation,const char *path,const char *name,unsigned offset,uint32_t *id)
{
    if(!state || !candidate || !candidate->instance || !id || operation>2 || offset>1024 || !card_ready())return ESP_ERR_INVALID_STATE;
    char absolute[180];if(operation && !recording_path(candidate->app_id,path,absolute))return ESP_ERR_INVALID_ARG;
    if(operation==1){char target[128];if(!name || !*name || strnlen(name,80)>=80)return ESP_ERR_INVALID_ARG;snprintf(target,sizeof(target),"/Ouse/Apps/%s/Recordings/%s.wav",candidate->app_id,name);if(!recording_path(candidate->app_id,target,absolute))return ESP_ERR_INVALID_ARG;}
    job_t *job=calloc(1,sizeof(*job));if(!job)return ESP_ERR_NO_MEM;
    lock();if(state->active || state->busy){unlock();free(job);return ESP_ERR_INVALID_STATE;}
    static const char *names[]={"recordings","renameRecording","deleteRecording"};esp_err_t error=ouse_task_create(candidate,OUSE_TASK_STORAGE,names[operation],OUSE_TASK_STOP_ON_CLOSE,true,id);
    if(error==ESP_OK){owner=*candidate;job->owner=*candidate;job->id=*id;job->operation=operation;job->offset=offset;if(path)strcpy(job->path,path);if(name)strcpy(job->name,name);state->busy=true;
        if(xQueueSend(jobs,&job,0)!=pdTRUE){state->busy=false;ouse_task_finish(*id,ESP_ERR_NO_MEM,false);error=ESP_ERR_NO_MEM;}}
    unlock();if(error!=ESP_OK)free(job);return error;
}

void ouse_recorder_get_owned(const ouse_task_owner_t *candidate,
                             ouse_recorder_state_t *out) {
  if (!out)
    return;
  if (!mutex || !state) {
    *out = (ouse_recorder_state_t){.error = ESP_ERR_NOT_SUPPORTED};
    return;
  }
  lock();
  if (candidate && !strcmp(candidate->app_id, owner.app_id))
    *out = *state;
  else
    *out = (ouse_recorder_state_t){.revision = state->revision};
  unlock();
}
