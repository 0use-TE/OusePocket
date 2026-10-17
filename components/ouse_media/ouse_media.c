// SPDX-License-Identifier: MIT
#include "ouse_media.h"
#include "ouse_wave.h"
#include "ouse_net.h"
#include "ouse_notifications.h"
#include "ouse_device_driver.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
typedef struct {
    uint32_t id;
    char url[OUSE_NET_URL_MAX+1];
    unsigned char *data;
    size_t size;
    ouse_wave_t wave;
    ouse_http_response_t *response;
    int64_t deadline;
    bool timed_out;
} source_t;
static SemaphoreHandle_t mutex;
static QueueHandle_t sources;
static StreamBufferHandle_t pcm;
static struct {
    ouse_media_state_t state;
    bool claimed,producer_done,consumer_done,force_stop,cancelled;
    uint64_t consumed;
} session;
static void lock(void){xSemaphoreTake(mutex,portMAX_DELAY);}
static void unlock(void){xSemaphoreGive(mutex);}
bool ouse_media_available(void){return sources!=NULL;}
static bool match(uint32_t id){return session.state.active && session.state.task_id==id;}
static bool stop(void *context)
{
    source_t *source=context;
    if(esp_timer_get_time()>=source->deadline)source->timed_out=true;
    lock();bool stopped=!match(source->id)||session.force_stop;unlock();
    return source->timed_out || stopped || ouse_task_cancel_requested(source->id);
}
static void notify_playback(void)
{
    if(!session.state.active){(void)ouse_notifications_cancel(session.state.app_id,"ouse.media");return;}
    const char *title=session.state.interrupted ? "播放被闹铃占用" : session.state.paused ? "播放已暂停" : "正在播放";
    const ouse_notification_request_t request={.id="ouse.media",.title=title,.body="点击返回播放控制",.payload="media",.ongoing=true};
    (void)ouse_notifications_publish(session.state.app_id,&request);
}
static void finish_if_stopped(void)
{
    if(!session.state.active || !session.consumer_done || !session.producer_done)return;
    session.state.active=false;session.state.buffering=false;session.state.interrupted=false;notify_playback();
    ouse_task_result_t result={.present=true,.value.audio={.position_ms=session.state.position_ms}};
    ouse_task_finish_result(session.state.task_id,session.state.error,session.cancelled && session.state.error==ESP_OK,&result);
    ++session.state.revision;
}
static esp_err_t output(void *context,const int16_t *samples,unsigned count)
{
    source_t *source=context;size_t left=count*2;const unsigned char *p=(const unsigned char *)samples;
    while(left){
        if(stop(source))return ESP_ERR_INVALID_STATE;
        size_t sent=xStreamBufferSend(pcm,p,left,pdMS_TO_TICKS(20));p+=sent;left-=sent;
    }
    lock();if(match(source->id) && source->wave.data && source->wave.rate){
        session.state.duration_ms=(uint64_t)source->wave.data_size*1000/(source->wave.channels*2*source->wave.rate);
    }unlock();return ESP_OK;
}
static esp_err_t decode(void *context,const unsigned char *bytes,size_t size)
{
    source_t *source=context;
    if(source->response && source->response->status!=200)return ESP_FAIL;
    return ouse_wave_feed(&source->wave,bytes,size);
}
static void worker(void *unused)
{
    (void)unused;source_t *source;
    for(;;){
        if(xQueueReceive(sources,&source,portMAX_DELAY)!=pdTRUE)continue;
        // The codec consumer claims the registry before any transport begins.
        // A cancelled queued request therefore never starts DNS/TLS/I/O.
        bool claimed=false;
        while(!stop(source)){
            lock();claimed=session.claimed;unlock();if(claimed)break;vTaskDelay(1);
        }
        esp_err_t error=ESP_OK;ouse_wave_init(&source->wave,output,source);
        if(claimed && !stop(source)){
            if(source->url[0]){
                ouse_http_request_t request={.url=source->url,.method="GET",.timeout_ms=15000};
                ouse_http_response_t *response=calloc(1,sizeof(*response));
                if(!response)error=ESP_ERR_NO_MEM;
                else{
                    source->response=response;
                    error=ouse_http_transfer(&request,source->deadline,
                        128*1024*1024,response,decode,stop,source);
                    if(error==ESP_OK && response->status!=200)error=ESP_FAIL;
                    free(response);
                    source->response=NULL;
                }
            }else for(size_t offset=0;error==ESP_OK && offset<source->size && !stop(source);){
                size_t n=source->size-offset;if(n>1024)n=1024;
                error=decode(source,source->data+offset,n);offset+=n;vTaskDelay(1);
            }
            if(error==ESP_OK && !stop(source))error=ouse_wave_finish(&source->wave);
        }
        bool cancelled=stop(source);
        if(source->timed_out){error=ESP_ERR_TIMEOUT;cancelled=false;}
        else if(cancelled && error==ESP_ERR_INVALID_STATE)error=ESP_OK;
        uint32_t id=source->id;free(source->data);free(source);
        lock();if(match(id)){
            session.producer_done=true;session.cancelled|=cancelled;
            if(session.state.error==ESP_OK)session.state.error=error;
            ++session.state.revision;finish_if_stopped();
        }unlock();
    }
}
esp_err_t ouse_media_init(void)
{
    if(sources)return ESP_ERR_INVALID_STATE;
    mutex=xSemaphoreCreateMutex();if(!mutex)return ESP_ERR_NO_MEM;
    pcm=xStreamBufferCreate(OUSE_MEDIA_BUFFER_BYTES,2);sources=xQueueCreate(1,sizeof(source_t *));
    if(!pcm || !sources || xTaskCreate(worker,"ouse_media",12288,NULL,2,NULL)!=pdPASS){
        if(pcm)vStreamBufferDelete(pcm);
        if(sources)vQueueDelete(sources);
        vSemaphoreDelete(mutex);pcm=NULL;sources=NULL;mutex=NULL;return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
esp_err_t ouse_media_play(const ouse_task_owner_t *owner,const char *url,const void *data,size_t size,uint32_t *id)
{
    if(!id)return ESP_ERR_INVALID_ARG;
    *id=0;
    if(!owner || !owner->app_id[0] || (url && (data || size)) || (!url && (!data || !size || size>OUSE_MEDIA_ASSET_MAX)))return ESP_ERR_INVALID_ARG;
    if(!sources)return ESP_ERR_INVALID_STATE;
    if(url){ouse_http_request_t request={.url=url,.method="GET",.timeout_ms=15000};if(ouse_http_validate(&request)!=ESP_OK)return ESP_ERR_INVALID_ARG;}
    source_t *source=calloc(1,sizeof(*source));if(!source)return ESP_ERR_NO_MEM;
    if(url)strcpy(source->url,url);
    else{source->data=malloc(size);if(!source->data){free(source);return ESP_ERR_NO_MEM;}memcpy(source->data,data,size);source->size=size;}
    lock();esp_err_t error=ESP_ERR_INVALID_STATE;
    if(session.state.active)goto done;
    error=ouse_task_create(owner,OUSE_TASK_AUDIO,"play",OUSE_TASK_FINISH_ON_CLOSE,true,id);if(error!=ESP_OK)goto done;
    source->id=*id;source->deadline=esp_timer_get_time()+4LL*60*60*1000000;
    uint32_t revision=session.state.revision+1;memset(&session,0,sizeof(session));
    session.state=(ouse_media_state_t){.active=true,.buffering=true,.task_id=*id,.revision=revision};strcpy(session.state.app_id,owner->app_id);
    (void)xStreamBufferReset(pcm);
    error=ouse_devices_queue_media(*id);
    if(error!=ESP_OK){session.state.active=false;session.state.error=error;ouse_task_finish(*id,error,false);goto done;}
    notify_playback();
    BaseType_t queued=xQueueSend(sources,&source,0);configASSERT(queued==pdTRUE);(void)queued;
done:
    unlock();if(error!=ESP_OK){free(source->data);free(source);}return error;
}
bool ouse_media_get(const char *app_id,ouse_media_state_t *out)
{
    if(!out || !mutex)return false;
    lock();bool found=session.state.task_id && (!app_id || !strcmp(app_id,session.state.app_id));
    if(found){*out=session.state;out->buffered_bytes=xStreamBufferBytesAvailable(pcm);}
    unlock();return found;
}
esp_err_t ouse_media_pause(const char *app_id,bool paused)
{
    if(!app_id || !mutex)return ESP_ERR_INVALID_ARG;
    lock();bool found=session.state.active && !strcmp(app_id,session.state.app_id);
    if(found){session.state.paused=paused;++session.state.revision;notify_playback();}
    unlock();return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}
esp_err_t ouse_media_stop(const char *app_id)
{
    if(!app_id || !mutex)return ESP_ERR_INVALID_ARG;
    lock();bool found=session.state.active && !strcmp(app_id,session.state.app_id);uint32_t id=session.state.task_id;
    unlock();return found ? ouse_task_cancel(NULL,id,"media_stop") : ESP_ERR_NOT_FOUND;
}
esp_err_t ouse_media_purge(const char *app_id)
{
    if(!app_id)return ESP_ERR_INVALID_ARG;
    if(!mutex)return ESP_OK;
    (void)ouse_media_stop(app_id);
    for(;;){lock();bool active=session.state.active && !strcmp(app_id,session.state.app_id);unlock();if(!active)return ESP_OK;vTaskDelay(1);}
}
void ouse_media_consumer_started(uint32_t id)
{lock();if(match(id))session.claimed=true;unlock();}
unsigned ouse_media_read(uint32_t id,int16_t *samples,unsigned capacity,bool *done,esp_err_t *error)
{
    *done=true;*error=ESP_OK;
    lock();if(!match(id)){unlock();return 0;}
    bool paused=session.state.paused || session.state.interrupted;
    bool producer_done=session.producer_done;*error=session.state.error;unlock();
    unsigned bytes=paused ? 0 : xStreamBufferReceive(pcm,samples,capacity*2,pdMS_TO_TICKS(20));
    lock();if(match(id)){
        *error=session.state.error;
        *done=*error!=ESP_OK || (producer_done && !xStreamBufferBytesAvailable(pcm));
        session.state.buffering=!paused && !bytes && !*done;
    }unlock();return bytes/2;
}
void ouse_media_consumed(uint32_t id,unsigned samples)
{lock();if(match(id)){session.consumed+=samples;session.state.position_ms=session.consumed/16;++session.state.revision;}unlock();}
void ouse_media_interrupted(uint32_t id,bool interrupted)
{lock();if(match(id)){session.state.interrupted=interrupted;++session.state.revision;notify_playback();}unlock();}
void ouse_media_consumer_done(uint32_t id,esp_err_t error,bool cancelled)
{
    lock();if(match(id)){
        session.consumer_done=true;session.force_stop=true;session.cancelled|=cancelled;
        if(error!=ESP_OK)session.state.error=error;
        ++session.state.revision;finish_if_stopped();
    }unlock();
}
