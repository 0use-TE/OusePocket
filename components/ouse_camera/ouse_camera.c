#include "ouse_camera.h"
#include "ouse_avi.h"
#include "ouse_files.h"
#include "ouse_devices.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/jpeg_encode.h"
#include "driver/jpeg_decode.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#define PHOTO_LIMIT (4U*1024*1024)
typedef struct {
    bool used,closed,wanted,play_wanted;ouse_task_owner_t owner;ouse_camera_state_t state;
    uint16_t *images[OUSE_CAMERA_IMAGES];uint32_t versions[OUSE_CAMERA_IMAGES];
} client_t;
typedef struct {ouse_task_owner_t owner;ouse_camera_operation_t operation;uint32_t id,offset;char path[128];} job_t;
static client_t *clients;
static QueueHandle_t jobs;
static SemaphoreHandle_t mutex;
static ouse_camera_driver_t driver;
static ouse_task_owner_t camera_owner;
static bool hardware,working;
static uint32_t nonce,sequence,preview_task;
static uint16_t *scaled;
static int64_t release_retry;
static esp_err_t record_finish(esp_err_t error);
static void playback_stop(void);
static bool playback_active;
static void lock(void){xSemaphoreTake(mutex,portMAX_DELAY);}
static void unlock(void){xSemaphoreGive(mutex);}
static bool same(const ouse_task_owner_t *first,const ouse_task_owner_t *second)
{return first->instance==second->instance && !strcmp(first->app_id,second->app_id);}
static client_t *find(const ouse_task_owner_t *owner)
{for(unsigned index=0;index<OUSE_TASK_OWNERS_MAX;++index)if(clients[index].used && same(owner,&clients[index].owner))return &clients[index];return NULL;}
static bool live(const ouse_task_owner_t *owner)
{lock();client_t *client=find(owner);bool value=client && !client->closed;unlock();return value;}
unsigned ouse_camera_image_width(unsigned image){return image<2 ? OUSE_CAMERA_WIDTH : 128;}
unsigned ouse_camera_image_height(unsigned image){return image<2 ? OUSE_CAMERA_HEIGHT : 96;}
static bool photo_path(const char *path,char absolute[180])
{
    if(!path || strncmp(path,"Pictures/",9) || !ouse_files_path(path,absolute,180))return false;
    const char *extension=strrchr(path,'.');return extension && (!strcasecmp(extension,".jpg") || !strcasecmp(extension,".jpeg"));
}
static bool video_path(const char *path,char absolute[180])
{
    if(!path || strncmp(path,"Pictures/",9) || !ouse_files_path(path,absolute,180))return false;
    const char *extension=strrchr(path,'.');return extension && !strcasecmp(extension,".avi");
}
static bool media_path(const char *path,char absolute[180])
{return photo_path(path,absolute) || video_path(path,absolute);}
static bool sd_ready(void){ouse_files_state_t state;ouse_files_get(&state);return state.ready && state.present;}
static bool publish(const ouse_task_owner_t *owner,unsigned image,const uint16_t *source,unsigned width,unsigned height,unsigned stride)
{
    if(!width || !height || image>=OUSE_CAMERA_IMAGES)return false;
    unsigned target_width=ouse_camera_image_width(image),target_height=ouse_camera_image_height(image);
    size_t bytes=target_width*target_height*2;
    if(!scaled)scaled=heap_caps_malloc(OUSE_CAMERA_WIDTH*OUSE_CAMERA_HEIGHT*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!scaled)return false;
    memset(scaled,0,bytes);
    lock();client_t *current=find(owner);bool allocate=current && !current->images[image];unlock();
    uint16_t *candidate=allocate ? heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT) : NULL;
    if(allocate && !candidate)return false;
    unsigned draw_width=target_width,draw_height=(uint64_t)height*target_width/width;
    if(draw_height>target_height){draw_height=target_height;draw_width=(uint64_t)width*target_height/height;}
    if(!draw_width)draw_width=1;
    if(!draw_height)draw_height=1;
    unsigned source_width=width,source_height=height,source_left=0,source_top=0;
    if(image>=2){
        if((uint64_t)width*target_height>(uint64_t)height*target_width)source_width=(uint64_t)height*target_width/target_height;
        else source_height=(uint64_t)width*target_height/target_width;
        source_left=(width-source_width)/2;source_top=(height-source_height)/2;draw_width=target_width;draw_height=target_height;
    }
    unsigned left=(target_width-draw_width)/2,top=(target_height-draw_height)/2;
    for(unsigned vertical=0;vertical<draw_height;++vertical){unsigned source_row=(source_top+vertical*source_height/draw_height)*stride;
        for(unsigned horizontal=0;horizontal<draw_width;++horizontal)scaled[(top+vertical)*target_width+left+horizontal]=source[source_row+source_left+horizontal*source_width/draw_width];}
    lock();client_t *client=find(owner);bool published=false;
    if(client && !client->closed){
        if(!client->images[image]){client->images[image]=candidate;candidate=NULL;}
        if(client->images[image]){memcpy(client->images[image],scaled,bytes);++client->versions[image];published=true;}
    }
    unlock();free(candidate);return published;
}
bool ouse_camera_copy(const ouse_task_owner_t *owner,unsigned image,uint32_t *revision,void *pixels,size_t capacity)
{
    if(!mutex || !owner || image>=OUSE_CAMERA_IMAGES || !revision || !pixels || capacity<ouse_camera_image_width(image)*ouse_camera_image_height(image)*2)return false;
    lock();client_t *client=find(owner);bool changed=client && !client->closed && client->images[image] && *revision!=client->versions[image];
    if(changed){memcpy(pixels,client->images[image],ouse_camera_image_width(image)*ouse_camera_image_height(image)*2);*revision=client->versions[image];}
    unlock();return changed;
}
bool ouse_camera_get(const ouse_task_owner_t *owner,ouse_camera_state_t *out)
{
    if(!mutex || !owner || !out)return false;
    lock();client_t *client=find(owner);*out=client ? client->state : (ouse_camera_state_t){0};
    out->running=out->running && client && client->wanted;
    out->in_use=camera_owner.instance && !same(owner,&camera_owner);out->recovery_pending=release_retry!=0;bool found=client && !client->closed;unlock();return found;
}
void ouse_camera_stop(const ouse_task_owner_t *owner)
{if(mutex && owner){lock();client_t *client=find(owner);if(client){client->wanted=false;client->play_wanted=false;}unlock();}}
void ouse_photos_stop(const ouse_task_owner_t *owner)
{if(mutex && owner){lock();client_t *client=find(owner);if(client)client->play_wanted=false;unlock();}}
void ouse_camera_forget(const ouse_task_owner_t *owner)
{if(mutex && owner){lock();client_t *client=find(owner);if(client){client->closed=true;client->wanted=false;client->play_wanted=false;}unlock();}}
bool ouse_camera_idle(void)
{if(!mutex)return true;lock();bool idle=!working && !camera_owner.instance && !playback_active;for(unsigned index=0;index<OUSE_TASK_OWNERS_MAX;++index)if(clients[index].used && clients[index].state.busy)idle=false;unlock();return idle;}
bool ouse_camera_media_active(void)
{
    if(!mutex)return false;
    lock();client_t *client=find(&camera_owner);bool active=playback_active || (client && !client->closed && client->wanted && client->state.recording);unlock();return active;
}
static bool wanted(const ouse_task_owner_t *owner)
{lock();client_t *client=find(owner);bool value=client && !client->closed && client->wanted;unlock();return value;}
static void retire_camera(void)
{
    lock();client_t *record_client=find(&camera_owner);esp_err_t record_error=record_client ? record_client->state.error : ESP_OK;unlock();
    (void)record_finish(record_error);
    esp_err_t stopped=driver.stop();
    if(stopped!=ESP_OK){lock();release_retry=esp_timer_get_time()+1000000;client_t *client=find(&camera_owner);if(client){client->wanted=false;client->state.running=false;client->state.error=stopped;++client->state.revision;}unlock();return;}
    hardware=false;
    lock();client_t *client=find(&camera_owner);esp_err_t error=client ? client->state.error : ESP_OK;if(client){client->state.running=false;++client->state.revision;}
    memset(&camera_owner,0,sizeof(camera_owner));release_retry=0;unlock();
    if(preview_task){ouse_task_finish(preview_task,error,ouse_task_cancel_requested(preview_task));preview_task=0;}
}
static esp_err_t decode_frame(const ouse_task_owner_t *owner,unsigned image,unsigned char *input,size_t length)
{
    size_t allocated=0;
    jpeg_decode_picture_info_t picture={0};esp_err_t error=jpeg_decoder_get_info(input,length,&picture);
    if(error==ESP_OK && (!picture.width || !picture.height || picture.width>2560 || picture.height>1920))error=ESP_ERR_NOT_SUPPORTED;
    unsigned stride=(picture.width+15)&~15U;size_t output_size=(size_t)stride*((picture.height+15)&~15U)*2;
    unsigned char *output=NULL;jpeg_decoder_handle_t decoder=NULL;
    if(error==ESP_OK){jpeg_decode_memory_alloc_cfg_t output_config={.buffer_direction=JPEG_DEC_ALLOC_OUTPUT_BUFFER};output=jpeg_alloc_decoder_mem(output_size,&output_config,&allocated);if(!output)error=ESP_ERR_NO_MEM;}
    if(error==ESP_OK){jpeg_decode_engine_cfg_t config={.timeout_ms=1000};error=jpeg_new_decoder_engine(&config,&decoder);}
    if(error==ESP_OK){jpeg_decode_cfg_t config={.output_format=JPEG_DECODE_OUT_FORMAT_RGB565,.rgb_order=JPEG_DEC_RGB_ELEMENT_ORDER_BGR};uint32_t written=0;
        error=jpeg_decoder_process(decoder,&config,input,length,output,allocated,&written);
        if(error==ESP_OK && written<output_size)error=ESP_ERR_INVALID_SIZE;
        if(error==ESP_OK && !publish(owner,image,(uint16_t *)output,picture.width,picture.height,stride))error=ESP_ERR_NO_MEM;
    }
    if(decoder)jpeg_del_decoder_engine(decoder);
    free(output);return error;
}
static esp_err_t load_photo(const ouse_task_owner_t *owner,const char *path,unsigned image)
{
    if(!sd_ready())return ESP_ERR_INVALID_STATE;
    char absolute[180];if(!media_path(path,absolute))return ESP_ERR_INVALID_ARG;
    bool video=video_path(path,absolute);
    struct stat info;if(stat(absolute,&info) || info.st_size<=0 || info.st_size>(video ? 1024U*1024*1024 : PHOTO_LIMIT))return ESP_ERR_INVALID_SIZE;
    size_t capacity=video ? PHOTO_LIMIT : info.st_size,allocated=0;
    jpeg_decode_memory_alloc_cfg_t memory={.buffer_direction=JPEG_DEC_ALLOC_INPUT_BUFFER};
    unsigned char *input=jpeg_alloc_decoder_mem(capacity,&memory,&allocated);if(!input)return ESP_ERR_NO_MEM;
    FILE *file=fopen(absolute,"rb");uint32_t length=capacity;bool read=false;
    if(file){if(video){ouse_avi_info_t avi;read=ouse_avi_read_info(file,&avi) && ouse_avi_read_frame(file,&avi,input,allocated,&length);}
        else read=fread(input,1,length,file)==length;
        if(fclose(file))read=false;}
    esp_err_t error=read ? decode_frame(owner,image,input,length) : ESP_FAIL;free(input);return error;
}
static esp_err_t capture(const job_t *job,char saved[128])
{
    if(!hardware || !same(&job->owner,&camera_owner) || !wanted(&job->owner) || !sd_ready())return ESP_ERR_INVALID_STATE;
    ouse_camera_frame_t frame;esp_err_t error=driver.frame(&frame);if(error!=ESP_OK)return error;
    jpeg_encoder_handle_t encoder=NULL;unsigned char *output=NULL;
    jpeg_encode_engine_cfg_t engine={.timeout_ms=1000};error=jpeg_new_encoder_engine(&engine,&encoder);size_t capacity=0;uint32_t length=0;
    if(error==ESP_OK){jpeg_encode_memory_alloc_cfg_t memory={.buffer_direction=JPEG_ENC_ALLOC_OUTPUT_BUFFER};output=jpeg_alloc_encoder_mem((size_t)frame.width*frame.height*2,&memory,&capacity);if(!output)error=ESP_ERR_NO_MEM;}
    if(error==ESP_OK){jpeg_encode_cfg_t config={.width=frame.width,.height=frame.height,.src_type=JPEG_ENCODE_IN_FORMAT_RGB565,.sub_sample=JPEG_DOWN_SAMPLING_YUV420,.image_quality=85};
        error=jpeg_encoder_process(encoder,&config,frame.pixels,frame.bytes,output,capacity,&length);}
    driver.put();if(encoder)jpeg_del_encoder_engine(encoder);
    char absolute[180]="",temporary[192];
    if(error==ESP_OK && (!length || length>capacity || length>PHOTO_LIMIT))error=ESP_ERR_INVALID_SIZE;
    snprintf(saved,128,"Pictures/IMG_%lld_%08lx_%04lx.jpg",(long long)time(NULL),(unsigned long)nonce,(unsigned long)++sequence);
    if(!photo_path(saved,absolute))error=ESP_ERR_INVALID_ARG;
    snprintf(temporary,sizeof(temporary),"%s.part",absolute);
    if(error==ESP_OK && (!live(&job->owner) || ouse_task_cancel_requested(job->id) || !sd_ready()))error=ESP_ERR_INVALID_STATE;
    if(error==ESP_OK){int descriptor=open(temporary,O_WRONLY|O_CREAT|O_EXCL,0600);if(descriptor<0)error=ESP_FAIL;
        else {FILE *file=fdopen(descriptor,"wb");if(!file){close(descriptor);error=ESP_FAIL;}
            else {bool written=true;for(uint32_t offset=0;offset<length;){uint32_t count=length-offset;if(count>4096)count=4096;if(fwrite(output+offset,1,count,file)!=count){written=false;break;}offset+=count;}
                if(fflush(file) || fsync(fileno(file)))written=false;
                if(fclose(file))written=false;
                if(!written)error=ESP_FAIL;}
            if(error==ESP_OK && (!live(&job->owner) || ouse_task_cancel_requested(job->id) || !wanted(&job->owner)))error=ESP_ERR_INVALID_STATE;
            struct stat existing;if(error==ESP_OK && (stat(absolute,&existing)==0 || rename(temporary,absolute)))error=ESP_FAIL;
            if(error!=ESP_OK)unlink(temporary);
        }
    }
    free(output);if(error==ESP_OK)(void)ouse_files_refresh();return error;
}
static int recent_first(const void *first,const void *second)
{
    const ouse_photo_entry_t *left=first,*right=second;
    if(left->modified!=right->modified)return left->modified>right->modified ? -1 : 1;
    const char *left_name=strrchr(left->path,'/')+1,*right_name=strrchr(right->path,'/')+1;
    if((!strncmp(left_name,"IMG_",4) || !strncmp(left_name,"VID_",4)) && (!strncmp(right_name,"IMG_",4) || !strncmp(right_name,"VID_",4)))return strcmp(right_name+4,left_name+4);
    return strcmp(right_name,left_name);
}
static esp_err_t enumerate(const job_t *job)
{
    if(!sd_ready())return ESP_ERR_INVALID_STATE;
    char directory[180];if(!ouse_files_path("Pictures/placeholder.jpg",directory,sizeof(directory)))return ESP_FAIL;
    *strrchr(directory,'/')=0;DIR *handle=opendir(directory);if(!handle)return ESP_FAIL;
    unsigned capacity=job->offset+OUSE_PHOTO_PAGE+1;
    ouse_photo_entry_t *recent=heap_caps_calloc(capacity,sizeof(*recent),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!recent){closedir(handle);return ESP_ERR_NO_MEM;}
    unsigned total=0;struct dirent *entry;int64_t deadline=esp_timer_get_time()+3000000;
    while((entry=readdir(handle))!=NULL){
        if(esp_timer_get_time()>deadline || !live(&job->owner) || ouse_task_cancel_requested(job->id)){closedir(handle);free(recent);return ESP_ERR_TIMEOUT;}
        char path[128],absolute[180];int length=snprintf(path,sizeof(path),"Pictures/%s",entry->d_name);if(length<0 || length>=sizeof(path) || !media_path(path,absolute))continue;
        struct stat info;if(stat(absolute,&info) || !S_ISREG(info.st_mode) || info.st_size<=0)continue;
        ouse_photo_entry_t candidate={.bytes=info.st_size,.video=video_path(path,absolute),.modified=info.st_mtime};strcpy(candidate.path,path);
        if(total<capacity){recent[total++]=candidate;qsort(recent,total,sizeof(*recent),recent_first);}
        else if(recent_first(&candidate,&recent[total-1])<0){recent[total-1]=candidate;qsort(recent,total,sizeof(*recent),recent_first);}
    }
    closedir(handle);
    ouse_photo_entry_t entries[OUSE_PHOTO_PAGE]={0};unsigned count=total>job->offset ? total-job->offset : 0;
    bool more=count>OUSE_PHOTO_PAGE;if(count>OUSE_PHOTO_PAGE)count=OUSE_PHOTO_PAGE;
    if(count)memcpy(entries,recent+job->offset,count*sizeof(*recent));
    free(recent);
    bool cached[OUSE_PHOTO_PAGE]={0};
    lock();client_t *client=find(&job->owner);if(client && !client->closed){
        uint16_t *previous[OUSE_PHOTO_PAGE],*next[OUSE_PHOTO_PAGE]={0};
        memcpy(previous,client->images+2,sizeof(previous));
        for(unsigned index=0;index<count;++index)for(unsigned old=0;old<client->state.count;++old)
            if(previous[old] && !strcmp(entries[index].path,client->state.entries[old].path) && entries[index].bytes==client->state.entries[old].bytes && entries[index].modified==client->state.entries[old].modified){next[index]=previous[old];previous[old]=NULL;cached[index]=true;break;}
        for(unsigned index=0;index<OUSE_PHOTO_PAGE;++index){
            if(!next[index])for(unsigned old=0;old<OUSE_PHOTO_PAGE;++old)if(previous[old]){next[index]=previous[old];previous[old]=NULL;break;}
            client->images[index+2]=next[index];if(!cached[index] && next[index])memset(next[index],0,128*96*2);++client->versions[index+2];
        }
        memcpy(client->state.entries,entries,sizeof(entries));client->state.count=count;client->state.more=more;client->state.offset=job->offset;
    }
    unlock();
    for(unsigned index=0;index<count && live(&job->owner) && !ouse_task_cancel_requested(job->id);++index)if(!cached[index])(void)load_photo(&job->owner,entries[index].path,index+2);
    return ESP_OK;
}
#include "ouse_camera_record.inc"
#include "ouse_camera_playback.inc"
static void worker(void *unused)
{
    (void)unused;unsigned timeouts=0;
    for(;;){
        lock();ouse_task_owner_t active=camera_owner;unlock();
        ouse_power_state_t power={0};(void)ouse_power_get(&power);
        if(active.instance && esp_timer_get_time()>=release_retry && (!wanted(&active) || power.asleep || ouse_tasks_quiescing() || (preview_task && ouse_task_cancel_requested(preview_task))))retire_camera();
        lock();for(unsigned index=0;index<OUSE_TASK_OWNERS_MAX;++index){client_t *client=&clients[index];if(client->used && client->closed && !client->state.busy && !same(&client->owner,&camera_owner)){for(unsigned image=0;image<OUSE_CAMERA_IMAGES;++image)free(client->images[image]);memset(client,0,sizeof(*client));}}unlock();
        job_t job;
        if(xQueueReceive(jobs,&job,pdMS_TO_TICKS(hardware && !release_retry ? 1 : playback_active ? 10 : 100))==pdTRUE){
            lock();working=true;unlock();esp_err_t error=ESP_ERR_INVALID_STATE;char saved[128]="";
            bool claimed=ouse_task_start(job.id,true);
            bool needs_sd=job.operation!=OUSE_CAMERA_START && job.operation!=OUSE_CAMERA_RECORD_STOP;
            bool leased=needs_sd && claimed && ouse_files_lease_begin();
            if(claimed && (!needs_sd || leased) && live(&job.owner) && !ouse_task_cancel_requested(job.id)){
                if(job.operation==OUSE_CAMERA_START){
                    if(wanted(&job.owner) && same(&job.owner,&camera_owner)){error=driver.start();hardware=error==ESP_OK;timeouts=0;
                        if(error==ESP_OK){error=ouse_task_create(&job.owner,OUSE_TASK_CAMERA,"preview",OUSE_TASK_STOP_ON_CLOSE,true,&preview_task);if(error==ESP_OK && !ouse_task_start(preview_task,true))error=ESP_ERR_INVALID_STATE;}
                        if(error==ESP_OK && !wanted(&job.owner))error=ESP_ERR_INVALID_STATE;
                    }
                }else if(job.operation==OUSE_CAMERA_CAPTURE)error=capture(&job,saved);
                else if(job.operation==OUSE_CAMERA_RECORD)error=record_start(&job);
                else if(job.operation==OUSE_CAMERA_RECORD_STOP)error=record_finish(ESP_OK);
                else if(job.operation==OUSE_PHOTOS_LIST){playback_stop();error=enumerate(&job);}
                else if(job.operation==OUSE_PHOTOS_REMOVE){playback_stop();char absolute[180];error=!sd_ready() ? ESP_ERR_INVALID_STATE : !media_path(job.path,absolute) ? ESP_ERR_INVALID_ARG : unlink(absolute) ? ESP_FAIL : ESP_OK;}
                else {playback_stop();error=load_photo(&job.owner,job.path,1);if(error==ESP_OK){strcpy(saved,job.path);char absolute[180];if(video_path(job.path,absolute))error=playback_start(&job);}}
            }
            if(job.operation==OUSE_CAMERA_RECORD && recording.file && leased){recording.leased=true;leased=false;}
            if(leased)ouse_files_lease_end();
            if(job.operation==OUSE_CAMERA_START && (error!=ESP_OK || ouse_task_cancel_requested(job.id))){if(same(&job.owner,&camera_owner))retire_camera();if(error==ESP_OK)error=ESP_ERR_INVALID_STATE;}
            lock();client_t *client=find(&job.owner);if(client){client->state.busy=false;client->state.error=error;client->state.running=hardware && !release_retry && same(&job.owner,&camera_owner);if(saved[0] && error==ESP_OK)strcpy(client->state.path,saved);++client->state.revision;}
            working=false;unlock();ouse_task_finish(job.id,error,!claimed || (error!=ESP_OK && ouse_task_cancel_requested(job.id)));
        }
        if(hardware && wanted(&camera_owner)){
            ouse_camera_frame_t frame;esp_err_t error=driver.frame(&frame);
            if(error==ESP_OK){timeouts=0;record_frame(&frame);bool published=publish(&camera_owner,0,(const uint16_t *)frame.pixels,frame.width,frame.height,frame.stride);driver.put();if(!published){lock();client_t *client=find(&camera_owner);if(client)client->state.error=ESP_ERR_NO_MEM;unlock();retire_camera();}}
            else if(error!=ESP_ERR_TIMEOUT || ++timeouts>=10){lock();client_t *client=find(&camera_owner);if(client)client->state.error=error;unlock();retire_camera();}
            vTaskDelay(pdMS_TO_TICKS(60));
        }
        playback_pump(&power);
    }
}
esp_err_t ouse_camera_request(const ouse_task_owner_t *owner,ouse_camera_operation_t operation,const char *path,uint32_t offset,uint32_t *id)
{
    if(!mutex || !owner || !owner->instance || !id || operation>OUSE_PHOTOS_REMOVE || operation<OUSE_CAMERA_START || !owner->app_id[0] || strnlen(owner->app_id,48)>=48 || (path && strnlen(path,128)>=128) || offset>4096)return ESP_ERR_INVALID_ARG;
    if(ouse_tasks_quiescing())return ESP_ERR_INVALID_STATE;
    ouse_power_state_t power={0};(void)ouse_power_get(&power);if(power.asleep)return ESP_ERR_INVALID_STATE;
    char absolute[180];if(operation==OUSE_PHOTOS_OPEN && !media_path(path,absolute))return ESP_ERR_INVALID_ARG;
    if(operation!=OUSE_CAMERA_START && operation!=OUSE_CAMERA_RECORD_STOP && !sd_ready())return ESP_ERR_INVALID_STATE;
    lock();client_t *client=find(owner);if(!client)for(unsigned index=0;index<OUSE_TASK_OWNERS_MAX;++index)if(!clients[index].used){client=&clients[index];client->used=true;client->owner=*owner;break;}
    esp_err_t error=!client ? ESP_ERR_NO_MEM : client->closed || client->state.busy ? ESP_ERR_INVALID_STATE : ESP_OK;
    if(error==ESP_OK && operation==OUSE_CAMERA_START && camera_owner.instance)error=ESP_ERR_INVALID_STATE;
    if(error==ESP_OK && (operation==OUSE_CAMERA_CAPTURE || operation==OUSE_CAMERA_RECORD || operation==OUSE_CAMERA_RECORD_STOP) && (!same(owner,&camera_owner) || !client->state.running || !client->wanted))error=ESP_ERR_INVALID_STATE;
    if(error==ESP_OK && (operation==OUSE_CAMERA_CAPTURE || operation==OUSE_CAMERA_RECORD) && client->state.recording)error=ESP_ERR_INVALID_STATE;
    if(error==ESP_OK){const char *names[]={"start","capture","list","open","record","recordStop","remove"};error=ouse_task_create(owner,OUSE_TASK_CAMERA,names[operation],OUSE_TASK_STOP_ON_CLOSE,true,id);
        if(error==ESP_OK){job_t job={.owner=*owner,.operation=operation,.offset=offset,.id=*id};if(path)strcpy(job.path,path);
            if(operation==OUSE_CAMERA_START){camera_owner=*owner;client->wanted=true;}
            client->state.busy=true;client->state.task_id=*id;client->state.error=ESP_OK;
            if(xQueueSend(jobs,&job,0)!=pdTRUE){client->state.busy=false;if(operation==OUSE_CAMERA_START){memset(&camera_owner,0,sizeof(camera_owner));client->wanted=false;}error=ESP_ERR_NO_MEM;ouse_task_finish(*id,error,false);}
        }
    }
    unlock();return error;
}
esp_err_t ouse_camera_init(const ouse_camera_driver_t *port)
{
    if(mutex || !port || !port->start || !port->frame || !port->put || !port->stop)return ESP_ERR_INVALID_ARG;
    clients=heap_caps_calloc(OUSE_TASK_OWNERS_MAX,sizeof(*clients),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);mutex=xSemaphoreCreateMutex();jobs=xQueueCreate(8,sizeof(job_t));driver=*port;nonce=esp_random();
    if(clients && mutex && jobs && xTaskCreateWithCaps(worker,"ouse_camera",12288,NULL,2,NULL,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS)return ESP_OK;
    free(clients);clients=NULL;if(jobs)vQueueDelete(jobs);jobs=NULL;if(mutex)vSemaphoreDelete(mutex);mutex=NULL;return ESP_ERR_NO_MEM;
}
