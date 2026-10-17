// SPDX-License-Identifier: MIT
#include "ouse_usb_private.h"
#include "ouse_device_driver.h"
#include "tinyusb_default_config.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
// TinyUSB 0.21 audio completions run in ISR context. Serialize our two possible
// producers/consumers during the codec <-> silence handoff; FIFO copies below
// are bounded, have no configured mutex, and perform no hardware I/O.
static portMUX_TYPE pcm_lock=portMUX_INITIALIZER_UNLOCKED;
static ouse_usb_state_t state;
static bool requested_enabled=true,requested_network=false,requested_audio=true;
static unsigned streams;
static uint32_t generation=1;
static bool starting;
static TaskHandle_t worker_handle;
static bool audio_live;
static void wake(void){if(worker_handle)xTaskNotifyGive(worker_handle);}
void ouse_usb_get(ouse_usb_state_t *out)
{
    if(!out)return;
    portENTER_CRITICAL(&lock);*out=state;portEXIT_CRITICAL(&lock);
    ouse_usb_network_snapshot(out);
}
esp_err_t ouse_usb_set_enabled(bool enabled)
{
    if(!ouse_tasks_submission_begin())return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&lock);requested_enabled=enabled;portEXIT_CRITICAL(&lock);
    ouse_tasks_submission_end();wake();return ESP_OK;
}
esp_err_t ouse_usb_set_network(bool enabled)
{
    if(!ouse_tasks_submission_begin())return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&lock);requested_network=enabled;portEXIT_CRITICAL(&lock);
    ouse_tasks_submission_end();wake();return ESP_OK;
}
esp_err_t ouse_usb_set_audio(bool enabled)
{
    if(!ouse_tasks_submission_begin())return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&lock);
    if(requested_audio!=enabled){requested_audio=enabled;++generation;}
    state.audio_enabled=enabled;
    portEXIT_CRITICAL(&lock);ouse_tasks_submission_end();wake();return ESP_OK;
}
static void session_end(void)
{
    portENTER_CRITICAL(&lock);streams=0;++generation;
    state.speaker=state.microphone=false;portEXIT_CRITICAL(&lock);wake();
}
static void event(tinyusb_event_t *event,void *arg)
{
    (void)arg;
    portENTER_CRITICAL(&lock);state.mounted=event->id==TINYUSB_EVENT_ATTACHED;
    state.suspended=false;portEXIT_CRITICAL(&lock);
    session_end();
}
void tud_suspend_cb(bool remote_wakeup)
{
    (void)remote_wakeup;portENTER_CRITICAL(&lock);state.suspended=true;portEXIT_CRITICAL(&lock);
    // Revoke microphone access until the host explicitly selects an active alt.
    session_end();
}
void tud_resume_cb(void)
{
    portENTER_CRITICAL(&lock);state.suspended=false;portEXIT_CRITICAL(&lock);wake();
}
bool tud_audio_set_itf_cb(uint8_t rhport,const tusb_control_request_t *request)
{
    (void)rhport;unsigned itf=tu_u16_low(request->wIndex),alt=tu_u16_low(request->wValue);
    if(itf<1 || itf>2 || alt>1)return false;
    portENTER_CRITICAL(&lock);
    if(alt)streams|=1u<<(itf-1);else streams&=~(1u<<(itf-1));
    ++generation;state.speaker=streams&1;state.microphone=streams&2;
    portEXIT_CRITICAL(&lock);wake();return true;
}
bool tud_audio_set_itf_close_ep_cb(uint8_t rhport,const tusb_control_request_t *request)
{(void)rhport;(void)request;return true;}
bool tud_audio_rx_done_isr(uint8_t rhport,uint16_t bytes,uint8_t function,uint8_t endpoint,uint8_t alt)
{
    (void)rhport;(void)bytes;(void)function;(void)endpoint;(void)alt;
    // A host can keep its stream open while an alarm owns the codec. Consume
    // and discard playback then, so a full FIFO cannot stop endpoint rearming.
    portENTER_CRITICAL_ISR(&pcm_lock);
    if(!audio_live){
        int16_t discard[17];(void)tud_audio_read(discard,sizeof(discard));
    }
    portEXIT_CRITICAL_ISR(&pcm_lock);
    return true;
}
bool tud_audio_tx_done_isr(uint8_t rhport,uint16_t bytes,uint8_t function,uint8_t endpoint,uint8_t alt)
{
    (void)rhport;(void)bytes;(void)function;(void)endpoint;(void)alt;
    portENTER_CRITICAL_ISR(&pcm_lock);
    if(!audio_live){
        static const int16_t silence[16]={0};
        unsigned bytes=tud_speed_get()==TUSB_SPEED_HIGH ? 4 : sizeof(silence);
        (void)tud_audio_write(silence,bytes);
    }
    portEXIT_CRITICAL_ISR(&pcm_lock);
    return true;
}
void tud_audio_feedback_params_cb(uint8_t func,uint8_t alt,audio_feedback_params_t *params)
{
    (void)func;(void)alt;params->method=AUDIO_FEEDBACK_METHOD_FIFO_COUNT;params->sample_freq=16000;
}
bool tud_audio_get_req_entity_cb(uint8_t rhport,const tusb_control_request_t *request)
{
    if(tu_u16_high(request->wIndex)!=4)return false;
    unsigned selector=tu_u16_high(request->wValue);
    if(selector==AUDIO_CS_CTRL_SAM_FREQ){
        if(request->bRequest==AUDIO_CS_REQ_CUR){uint32_t rate=tu_htole32(16000);
            return tud_audio_buffer_and_schedule_control_xfer(rhport,request,&rate,sizeof(rate));}
        if(request->bRequest==AUDIO_CS_REQ_RANGE){
            audio_control_range_4_n_t(1) range={.wNumSubRanges=tu_htole16(1),
                .subrange={{.bMin=tu_htole32(16000),.bMax=tu_htole32(16000),.bRes=0}}};
            return tud_audio_buffer_and_schedule_control_xfer(rhport,request,&range,sizeof(range));
        }
    }else if(selector==AUDIO_CS_CTRL_CLK_VALID && request->bRequest==AUDIO_CS_REQ_CUR){
        uint8_t valid=1;return tud_audio_buffer_and_schedule_control_xfer(rhport,request,&valid,1);
    }
    return false;
}
bool tud_audio_set_req_entity_cb(uint8_t rhport,const tusb_control_request_t *request,uint8_t *buffer)
{
    (void)rhport;uint32_t rate;
    if(tu_u16_high(request->wIndex)!=4 || tu_u16_high(request->wValue)!=AUDIO_CS_CTRL_SAM_FREQ ||
       request->bRequest!=AUDIO_CS_REQ_CUR || request->wLength!=4)return false;
    memcpy(&rate,buffer,4);return tu_le32toh(rate)==16000;
}
bool ouse_usb_audio_requested(void)
{
    portENTER_CRITICAL(&lock);
    bool active=state.available && state.enabled && state.audio_enabled && state.mounted && !state.suspended && streams;
    portEXIT_CRITICAL(&lock);return active && !ouse_tasks_quiescing();
}
bool ouse_usb_microphone_requested(void)
{
    portENTER_CRITICAL(&lock);bool active=state.microphone;portEXIT_CRITICAL(&lock);
    return active && ouse_usb_audio_requested();
}
unsigned ouse_usb_speaker_read(int16_t *samples,unsigned count)
{
    portENTER_CRITICAL(&lock);bool active=state.speaker;portEXIT_CRITICAL(&lock);
    if(!active)return 0;
    portENTER_CRITICAL(&pcm_lock);
    unsigned got=audio_live ? tud_audio_read(samples,count*2)/2 : 0;
    portEXIT_CRITICAL(&pcm_lock);
    unsigned nonzero=0,peak=0;
    for(unsigned i=0;i<got;++i){int v=samples[i];unsigned magnitude=(unsigned)(v<0 ? -v : v);nonzero+=v!=0;if(magnitude>peak)peak=magnitude;}
    portENTER_CRITICAL(&lock);state.speaker_samples+=got;state.speaker_nonzero+=nonzero;
    if(peak>state.speaker_peak)state.speaker_peak=peak;
    if(got<count)++state.underruns;
    portEXIT_CRITICAL(&lock);
    return got;
}
void ouse_usb_microphone_write(const int16_t *samples,unsigned count)
{
    if(!ouse_usb_microphone_requested())return;
    portENTER_CRITICAL(&pcm_lock);
    bool short_write=audio_live && tud_audio_write(samples,count*2)!=count*2;
    portEXIT_CRITICAL(&pcm_lock);
    if(short_write){portENTER_CRITICAL(&lock);++state.overruns;portEXIT_CRITICAL(&lock);}
}
void ouse_usb_audio_running(uint32_t id,bool running,esp_err_t error)
{
    portENTER_CRITICAL(&lock);
    if(running && id>state.audio_task)state.audio_task=id;
    if(id==state.audio_task){state.audio_running=running;state.error=error;}
    portEXIT_CRITICAL(&lock);
    portENTER_CRITICAL(&pcm_lock);audio_live=running;
    if(!running){(void)tud_audio_clear_ep_in_ff();(void)tud_audio_clear_ep_out_ff();}
    portEXIT_CRITICAL(&pcm_lock);
}
static void worker(void *arg)
{
    (void)arg;bool was_enabled=true;uint32_t task=0,last_generation=0,network_task=0;bool retry_alarm=false;
    for(;;){
        bool enabled,network;uint32_t current_generation;
        portENTER_CRITICAL(&lock);enabled=requested_enabled;network=requested_network;
        current_generation=generation;portEXIT_CRITICAL(&lock);
        enabled=enabled && !ouse_tasks_quiescing();
        if(enabled!=was_enabled){
            if(enabled)tud_connect();else{tud_disconnect();session_end();}
            was_enabled=enabled;
        }
        portENTER_CRITICAL(&lock);state.enabled=enabled;state.network_enabled=network;
        state.session=generation;portEXIT_CRITICAL(&lock);
        if(task){
            if(!ouse_usb_audio_requested())(void)ouse_task_cancel(NULL,task,"usb_stream_closed");
            ouse_task_snapshot_t snapshot;
            if(ouse_task_get(NULL,task,&snapshot) && snapshot.state>=OUSE_TASK_SUCCEEDED){
                retry_alarm=!strcmp(snapshot.cancel_reason,"alarm_preempt");task=0;
                if(!retry_alarm && (snapshot.error!=ESP_OK || (snapshot.cancel_requested &&
                    strcmp(snapshot.cancel_reason,"usb_stream_closed"))))last_generation=current_generation;
            }
        }
        if(!task && ouse_usb_audio_requested() && (current_generation!=last_generation || retry_alarm)){
            uint32_t id=0;esp_err_t error=ouse_devices_queue_usb(&id);
            if(error==ESP_OK){
                task=id;last_generation=current_generation;retry_alarm=false;
                portENTER_CRITICAL(&lock);state.audio_task=id;state.error=ESP_OK;portEXIT_CRITICAL(&lock);
            }else if(error!=ESP_ERR_INVALID_STATE){
                last_generation=current_generation;retry_alarm=false;
                portENTER_CRITICAL(&lock);state.error=error;portEXIT_CRITICAL(&lock);
            }
        }
        bool share=enabled && network;
        if(network_task && ouse_task_cancel_requested(network_task)){
            portENTER_CRITICAL(&lock);requested_network=false;state.network_enabled=false;portEXIT_CRITICAL(&lock);
            share=false;
        }
        if(share && !network_task){
            static const ouse_task_owner_t owner={.app_id="ouse.usb"};
            esp_err_t error=ouse_task_create(&owner,OUSE_TASK_NETWORK,"usbSharing",OUSE_TASK_FINISH_ON_CLOSE,true,&network_task);
            if(error==ESP_OK && !ouse_task_start(network_task,true))network_task=0;
            if(error!=ESP_OK)share=false;
            portENTER_CRITICAL(&lock);state.network_task=network_task;portEXIT_CRITICAL(&lock);
        }
        ouse_usb_network_poll(share);
        if(!share && network_task){
            ouse_usb_state_t current={0};ouse_usb_network_snapshot(&current);
            if(!current.network_up && !current.network_stopping){
                ouse_task_finish(network_task,current.network_error,ouse_task_cancel_requested(network_task));network_task=0;
            }
        }
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(20));
    }
}
esp_err_t ouse_usb_start(void)
{
    portENTER_CRITICAL(&lock);bool busy=starting;starting=true;portEXIT_CRITICAL(&lock);
    if(busy)return ESP_ERR_INVALID_STATE;
    esp_err_t error=ouse_usb_network_start();
    if(error==ESP_OK){
        tinyusb_config_t config=TINYUSB_DEFAULT_CONFIG(event,NULL);
        config.port=TINYUSB_PORT_HIGH_SPEED_0; // native OTG Type-C USBD_N/P, NOT GPIO24/25 FS.
        ouse_usb_descriptors(&config);error=tinyusb_driver_install(&config);
        if(error==ESP_OK && xTaskCreate(worker,"ouse_usb",4096,NULL,3,&worker_handle)!=pdPASS){
            error=ESP_ERR_NO_MEM;(void)tinyusb_driver_uninstall();
        }
    }
    portENTER_CRITICAL(&lock);state.available=error==ESP_OK;state.enabled=state.available;
    state.audio_enabled=requested_audio;state.error=error;portEXIT_CRITICAL(&lock);
    ESP_LOGI("OuseUSB","OTG HS composite: %s; PCM16 mono 16000 Hz; NCM sharing defaults OFF",esp_err_to_name(error));
    return error;
}
