// SPDX-License-Identifier: MIT
// One owner for ES8311 BOTH mode and the shared duplex I2S channels.
#include "ouse_device_driver.h"
#include "ouse_audio_mix.h"
#include "ouse_media.h"
#include "ouse_recorder.h"
#include "ouse_usb.h"
#include "bsp/esp-bsp.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>
static i2s_chan_handle_t tx,rx;
static const audio_codec_data_if_t *data_if;
static const audio_codec_ctrl_if_t *ctrl_if;
static const audio_codec_gpio_if_t *gpio_if;
static const audio_codec_if_t *codec_if;
static esp_codec_dev_handle_t codec;
static int16_t *recording;
static unsigned recorded_samples;
static int data_error;
static ouse_audio_request_t overlay_request;
static uint64_t overlay_offset,overlay_total;
static ouse_audio_mixer_t mixer={.gain=32768};
static void overlay_finish(esp_err_t error,bool cancelled)
{
    if(overlay_request.id){
        ouse_task_result_t result={.present=true,.value.audio={.position_ms=(uint32_t)(overlay_offset/16)}};
        ouse_task_finish_result(overlay_request.id,error,cancelled,&result);
    }
    overlay_request.id=0;
}
static bool overlay_update(void)
{
    if(overlay_request.id && !ouse_devices_overlay_current(overlay_request.id))overlay_finish(ESP_OK,true);
    ouse_audio_request_t next;
    if(ouse_devices_take_overlay(&next)){
        overlay_finish(ESP_OK,true);
        if(ouse_task_start(next.id,true)){
            overlay_request=next;overlay_offset=0;
            overlay_total=next.action==OUSE_AUDIO_TEST ? 8000 : next.action==OUSE_AUDIO_RING ? (uint64_t)next.seconds*16000 : UINT64_MAX;
        }else ouse_task_finish(next.id,ESP_OK,true);
    }
    return overlay_request.id!=0;
}
static void mix_output(int16_t *samples,unsigned count)
{
    bool active=overlay_update();
    ouse_audio_mix(&mixer,samples,count,active,overlay_request.action,overlay_offset,overlay_total);
    if(active){overlay_offset+=count;if(overlay_offset>=overlay_total)overlay_finish(ESP_OK,false);}
}
static esp_err_t channel_enabled(i2s_chan_handle_t channel,bool enabled)
{
    i2s_chan_info_t info;
    esp_err_t error=i2s_channel_get_info(channel,&info);
    if(error!=ESP_OK || info.is_enabled==enabled)return error;
    if(enabled && channel==tx){
        // Reusing the fixed-format DMA buffers must not replay a previous tail
        // after the codec is unmuted. Preload silence while TX is still READY.
        static const uint8_t silence[512]={0};
        for(size_t left=info.total_dma_buf_size;left;){
            size_t count=left<sizeof(silence) ? left : sizeof(silence),loaded=0;
            error=i2s_channel_preload_data(channel,silence,count,&loaded);
            if(error!=ESP_OK)return error;
            if(!loaded || loaded>count)return ESP_FAIL;
            left-=loaded;
        }
    }
    return enabled ? i2s_channel_enable(channel) : i2s_channel_disable(channel);
}
static int data_result(int error)
{
    // esp_codec_dev 1.5.11 ignores data_if set_fmt/enable return values.
    // Preserve the first failure so the worker cannot report a false success.
    if(error!=ESP_CODEC_DEV_OK && data_error==ESP_CODEC_DEV_OK)data_error=error;
    return error;
}
static bool fixed_is_open(const audio_codec_data_if_t *unused)
{
    (void)unused;return data_if && data_if->is_open(data_if);
}
static int fixed_format(const audio_codec_data_if_t *unused,esp_codec_dev_type_t type,
                        esp_codec_dev_sample_info_t *format)
{
    (void)unused;
    // Both channels already use exactly this format. The upstream set_fmt
    // unconditionally disables READY channels and reconfigures them on every
    // open. Keep its read/write transport, with this board owning fixed-format
    // I2S state; do not bypass format changes for any other sample format.
    if(type!=ESP_CODEC_DEV_TYPE_IN_OUT || !format || format->sample_rate!=16000 ||
       format->channel!=1 || format->bits_per_sample!=16 || format->channel_mask>1 ||
       (format->mclk_multiple && format->mclk_multiple!=256))
        return data_result(ESP_CODEC_DEV_NOT_SUPPORT);
    return ESP_CODEC_DEV_OK;
}
static int fixed_enable(const audio_codec_data_if_t *unused,esp_codec_dev_type_t type,bool enabled)
{
    (void)unused;
    if(type!=ESP_CODEC_DEV_TYPE_IN_OUT)return data_result(ESP_CODEC_DEV_NOT_SUPPORT);
    if(enabled && data_error!=ESP_CODEC_DEV_OK)return data_error;
    esp_err_t error;
    if(enabled){
        error=channel_enabled(tx,true);
        if(error==ESP_OK)error=channel_enabled(rx,true);
        if(error!=ESP_OK){
            // Roll back partial duplex startup; RX must stop before its TX clock.
            (void)channel_enabled(rx,false);(void)channel_enabled(tx,false);
        }
    }else{
        error=channel_enabled(rx,false);
        esp_err_t tx_error=channel_enabled(tx,false);
        if(error==ESP_OK)error=tx_error;
    }
    return data_result(error);
}
static int fixed_read(const audio_codec_data_if_t *unused,uint8_t *data,int size)
{
    (void)unused;size_t received=0;
    esp_err_t error=i2s_channel_read(rx,data,(size_t)size,&received,1000);
    return error==ESP_OK && received==(size_t)size ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}
static int fixed_write(const audio_codec_data_if_t *unused,uint8_t *data,int size)
{
    (void)unused;size_t written=0;
    esp_err_t error=i2s_channel_write(tx,data,(size_t)size,&written,1000);
    return error==ESP_OK && written==(size_t)size ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}
static const audio_codec_data_if_t fixed_data_if={
    .is_open=fixed_is_open,.set_fmt=fixed_format,.enable=fixed_enable,
    .read=fixed_read,.write=fixed_write,
};
static void release_channel(i2s_chan_handle_t *channel)
{
    if(!*channel)return;
    esp_err_t error=channel_enabled(*channel,false);
    if(error==ESP_OK)error=i2s_del_channel(*channel);
    if(error==ESP_OK)*channel=NULL;
    else ESP_LOGW("OuseAudio","I2S cleanup failed: %s",esp_err_to_name(error));
}
static void release_codec(void)
{
    if(codec){esp_codec_dev_delete(codec);codec=NULL;}
    if(codec_if){audio_codec_delete_codec_if(codec_if);codec_if=NULL;}
    if(data_if){audio_codec_delete_data_if(data_if);data_if=NULL;}
    if(ctrl_if){audio_codec_delete_ctrl_if(ctrl_if);ctrl_if=NULL;}
    if(gpio_if){audio_codec_delete_gpio_if(gpio_if);gpio_if=NULL;}
    release_channel(&rx);release_channel(&tx);
}
static esp_err_t prepare_codec(void)
{
    if(codec)return ESP_OK;
    // Retain handles if a previous driver cleanup failed; never overwrite them.
    if(tx || rx){release_codec();if(tx || rx)return ESP_FAIL;}
    // Reuse the display/touch I2C bus. An absent codec is an operation error.
    if(!bsp_i2c_get_handle() || i2c_master_probe(bsp_i2c_get_handle(),ES8311_CODEC_DEFAULT_ADDR>>1,50)!=ESP_OK)return ESP_ERR_NOT_FOUND;
    i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM,I2S_ROLE_MASTER);
    // Six 4 ms periods retain 24 ms of scheduling headroom, while reducing
    // scarce internal DMA memory by 4x versus the former 256-frame periods.
    // 64 mono PCM16 samples also match P4's 128-byte cache alignment.
    channel.auto_clear=true;channel.dma_frame_num=64;
    esp_err_t e=i2s_new_channel(&channel,&tx,&rx);if(e!=ESP_OK){release_codec();return e;}
    i2s_std_config_t cfg={.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_MONO),
        .gpio_cfg={.mclk=BSP_I2S_MCLK,.bclk=BSP_I2S_SCLK,.ws=BSP_I2S_LCLK,.dout=BSP_I2S_DOUT,.din=BSP_I2S_DSIN}};
    e=i2s_channel_init_std_mode(tx,&cfg);
    // P4's default MONO mask is BOTH: RX then delivers two slots per frame.
    // Keep TX duplication, but select the ES8311 ADC's left slot explicitly.
    cfg.slot_cfg.slot_mask=I2S_STD_SLOT_LEFT;
    if(e==ESP_OK)e=i2s_channel_init_std_mode(rx,&cfg);
    if(e!=ESP_OK){release_codec();return e;}
    audio_codec_i2s_cfg_t data={.port=CONFIG_BSP_I2S_NUM,.tx_handle=tx,.rx_handle=rx};
    data_if=audio_codec_new_i2s_data(&data);
    audio_codec_i2c_cfg_t ctrl={.port=BSP_I2C_NUM,.addr=ES8311_CODEC_DEFAULT_ADDR,.bus_handle=bsp_i2c_get_handle()};
    ctrl_if=audio_codec_new_i2c_ctrl(&ctrl);gpio_if=audio_codec_new_gpio();
    if(!data_if || !ctrl_if || !gpio_if){release_codec();return ESP_ERR_NO_MEM;}
    es8311_codec_cfg_t es={.ctrl_if=ctrl_if,.gpio_if=gpio_if,.codec_mode=ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin=BSP_POWER_AMP_IO,.master_mode=false,.use_mclk=true,.hw_gain={.pa_voltage=5.0,.codec_dac_voltage=3.3}};
    codec_if=es8311_codec_new(&es);if(!codec_if){release_codec();return ESP_FAIL;}
    esp_codec_dev_cfg_t config={.dev_type=ESP_CODEC_DEV_TYPE_IN_OUT,.codec_if=codec_if,.data_if=&fixed_data_if};
    codec=esp_codec_dev_new(&config);if(!codec){release_codec();return ESP_ERR_NO_MEM;}
    return ESP_OK;
}
static esp_err_t volume(bool capture)
{
    ouse_audio_state_t state;ouse_audio_get(&state);
    if(esp_codec_dev_set_out_vol(codec,state.volume)!=ESP_CODEC_DEV_OK ||
       esp_codec_dev_set_out_mute(codec,capture||state.muted)!=ESP_CODEC_DEV_OK)return ESP_FAIL;
    return ESP_OK;
}
static esp_err_t close_audio(esp_err_t error)
{
    if(esp_codec_dev_set_out_mute(codec,true)!=ESP_CODEC_DEV_OK && error==ESP_OK)error=ESP_FAIL;
    data_error=ESP_CODEC_DEV_OK;
    int close_error=esp_codec_dev_close(codec);
    if((close_error!=ESP_CODEC_DEV_OK || data_error!=ESP_CODEC_DEV_OK) && error==ESP_OK)error=ESP_FAIL;
    if(error!=ESP_OK)release_codec();
    return error;
}
static esp_err_t run(ouse_audio_request_t request,ouse_audio_state_t *state)
{
    if(request.action==OUSE_AUDIO_STOP)return ESP_OK;
    esp_err_t error=prepare_codec();if(error!=ESP_OK)return error;
    esp_codec_dev_sample_info_t info={.sample_rate=16000,.channel=1,.bits_per_sample=16};
    data_error=ESP_CODEC_DEV_OK;
    if(esp_codec_dev_open(codec,&info)!=ESP_CODEC_DEV_OK || data_error!=ESP_CODEC_DEV_OK){release_codec();return ESP_FAIL;}
    bool capture=request.action==OUSE_AUDIO_CAPTURE;
    error=volume(capture);
    if(capture && esp_codec_dev_set_in_gain(codec,24.0f)!=ESP_CODEC_DEV_OK)error=ESP_FAIL;
    if(capture && !recording)recording=heap_caps_malloc(16000*3*sizeof(int16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(capture && !recording)error=ESP_ERR_NO_MEM;
    if(capture)recorded_samples=0;
    state->mode=capture ? OUSE_AUDIO_RECORDING : request.action==OUSE_AUDIO_TEST || request.action==OUSE_AUDIO_RING ? OUSE_AUDIO_TONE : OUSE_AUDIO_PLAYBACK;
    unsigned total=capture || request.action==OUSE_AUDIO_RING ? request.seconds*16000 : request.action==OUSE_AUDIO_TEST ? 8000 : recorded_samples;
    int64_t started_us=esp_timer_get_time();
    int16_t samples[256];state->position_ms=0;state->peak=0;
    for(unsigned offset=0;error==ESP_OK && offset<total && (ouse_devices_audio_current(request.id) ||
        ((request.action==OUSE_AUDIO_TEST || request.action==OUSE_AUDIO_RING) && ouse_devices_overlay_current(request.id)));) {
        unsigned length=total-offset;if(length>256)length=256;
        error=volume(capture);if(error!=ESP_OK)break;
        if(capture) {
            if(esp_codec_dev_read(codec,samples,length*2)!=ESP_CODEC_DEV_OK){error=ESP_FAIL;break;}
            memcpy(recording+offset,samples,length*2);recorded_samples=offset+length;
            unsigned peak=0;for(unsigned i=0;i<length;++i){unsigned v=samples[i]<0 ? -(int)samples[i] : samples[i];if(v>peak)peak=v;}
            state->peak=peak*100/32768;state->recorded_ms=recorded_samples/16;
        } else {
            if(request.action==OUSE_AUDIO_REPLAY)memcpy(samples,recording+offset,length*2);
            else for(unsigned i=0;i<length;++i) {
                float position=offset+i,envelope=fminf(1.0f,fminf(position/320,(total-position)/320));
                float frequency=request.action==OUSE_AUDIO_RING ? 880 : 440;
                bool audible=request.action!=OUSE_AUDIO_RING || ((offset+i)/8000)%2==0;
                samples[i]=audible ? (int16_t)(4000*envelope*sinf(6.2831853f*frequency*position/16000)) : 0;
            }
            mix_output(samples,length);
            if(esp_codec_dev_write(codec,samples,length*2)!=ESP_CODEC_DEV_OK){error=ESP_FAIL;break;}
        }
        offset+=length;state->position_ms=offset/16;
        if(offset%1024==0)ouse_devices_publish_audio(state,request.id);
    }
    if(capture || request.action==OUSE_AUDIO_REPLAY)
        ESP_LOGI("OuseAudio","%s: samples=%u, pcm_ms=%u, elapsed_ms=%lld, rate=16000, RX=left",
            capture ? "capture" : "replay",capture ? recorded_samples : total,state->position_ms,
            (long long)((esp_timer_get_time()-started_us)/1000));
    if(error!=ESP_OK)overlay_finish(error,false);
    return close_audio(error);
}
static void short_request(ouse_audio_request_t request)
{
    ouse_audio_state_t state;ouse_audio_get(&state);
    bool claimed=ouse_task_start(request.id,true);
    esp_err_t error=claimed ? run(request,&state) : ESP_OK;
    bool cancelled=error==ESP_OK && ouse_task_cancel_requested(request.id);
    state.mode=error==ESP_OK ? OUSE_AUDIO_IDLE : OUSE_AUDIO_ERROR;state.error=error;state.peak=0;
    state.recorded_ms=recorded_samples/16;state.completed_id=request.id;ouse_devices_publish_audio(&state,request.id);
    ouse_task_result_t result={.present=true,.value.audio={.recorded_ms=state.recorded_ms,.position_ms=state.position_ms}};
    ouse_task_finish_result(request.id,error,cancelled,&result);
    if(error!=ESP_OK)ESP_LOGW("OuseAudio","Operation failed: %s",esp_err_to_name(error));
}
static void media_request(ouse_audio_request_t request)
{
    if(!ouse_task_start(request.id,true)){ouse_media_consumer_done(request.id,ESP_OK,true);return;}
    ouse_media_consumer_started(request.id);
    esp_err_t error=ESP_OK;bool opened=false;
    uint64_t played_samples=0;
    ouse_audio_state_t state;ouse_audio_get(&state);state.position_ms=0;
    for(;;){
        if(ouse_task_cancel_requested(request.id))break;
        if(!ouse_devices_audio_current(request.id)){
            ouse_audio_request_t alarm;
            if(ouse_devices_take_alarm(&alarm)){
                ouse_media_interrupted(request.id,true);
                if(opened){error=close_audio(error);opened=false;}
                short_request(alarm);
                (void)ouse_devices_resume_media(false);
                ouse_media_interrupted(request.id,false);
                if(error!=ESP_OK)break;
                continue;
            }
            // Ordinary focus replacement cancels the old registry task. A
            // queued alarm will be claimed on the next bounded iteration.
            vTaskDelay(1);continue;
        }
        if(!opened){
            error=prepare_codec();if(error!=ESP_OK)break;
            esp_codec_dev_sample_info_t format={.sample_rate=16000,.channel=1,.bits_per_sample=16};
            data_error=ESP_CODEC_DEV_OK;
            if(esp_codec_dev_open(codec,&format)!=ESP_CODEC_DEV_OK || data_error!=ESP_CODEC_DEV_OK){error=ESP_FAIL;release_codec();break;}
            opened=true;
        }
        ouse_media_state_t media;bool own=ouse_media_get(NULL,&media);
        if(!own || !media.active || media.task_id!=request.id){error=ESP_ERR_INVALID_STATE;break;}
        if(media.error!=ESP_OK){error=media.error;break;}
        if(media.paused){
            if(overlay_update()){
                int16_t silence[256]={0};error=volume(false);mix_output(silence,256);
                if(error==ESP_OK && esp_codec_dev_write(codec,silence,sizeof(silence))!=ESP_CODEC_DEV_OK)error=ESP_FAIL;
                if(error!=ESP_OK)break;
                continue;
            }
            if(esp_codec_dev_set_out_mute(codec,true)!=ESP_CODEC_DEV_OK){error=ESP_FAIL;break;}
            vTaskDelay(pdMS_TO_TICKS(20));continue;
        }
        error=volume(false);if(error!=ESP_OK)break;
        int16_t samples[256];bool done=false;
        unsigned count=ouse_media_read(request.id,samples,256,&done,&error);
        if(error!=ESP_OK)break;
        if(count){
            mix_output(samples,count);
            if(esp_codec_dev_write(codec,samples,count*2)!=ESP_CODEC_DEV_OK){error=ESP_FAIL;break;}
            ouse_media_consumed(request.id,count);played_samples+=count;state.position_ms=played_samples/16;state.mode=OUSE_AUDIO_PLAYBACK;
            if(state.position_ms%64<16)ouse_devices_publish_audio(&state,request.id);
        }else if(overlay_update()){
            memset(samples,0,sizeof(samples));mix_output(samples,256);
            if(esp_codec_dev_write(codec,samples,sizeof(samples))!=ESP_CODEC_DEV_OK){error=ESP_FAIL;break;}
        }else vTaskDelay(1);
        if(done)break;
    }
    if(error!=ESP_OK)overlay_finish(error,false);
    if(opened)error=close_audio(error);
    bool cancelled=ouse_task_cancel_requested(request.id);
    ouse_media_consumer_done(request.id,error,cancelled);
    state.mode=error==ESP_OK ? OUSE_AUDIO_IDLE : OUSE_AUDIO_ERROR;state.error=error;state.peak=0;
    ouse_task_snapshot_t task;if(ouse_task_get(NULL,request.id,&task) && task.state>=OUSE_TASK_SUCCEEDED)state.completed_id=request.id;
    ouse_devices_publish_audio(&state,request.id);
}
static void usb_request(ouse_audio_request_t request)
{
    if(!ouse_task_start(request.id,true))return;
    esp_err_t error=prepare_codec();bool opened=false;
    if(error==ESP_OK){
        esp_codec_dev_sample_info_t format={.sample_rate=16000,.channel=1,.bits_per_sample=16};
        data_error=ESP_CODEC_DEV_OK;
        if(esp_codec_dev_open(codec,&format)!=ESP_CODEC_DEV_OK || data_error!=ESP_CODEC_DEV_OK){
            error=ESP_FAIL;release_codec();
        }else opened=true;
    }
    if(opened && esp_codec_dev_set_in_gain(codec,24.0f)!=ESP_CODEC_DEV_OK)error=ESP_FAIL;
    ouse_usb_audio_running(request.id,error==ESP_OK,error);
    ouse_audio_state_t state;ouse_audio_get(&state);unsigned frames=0;
    while(error==ESP_OK && ouse_devices_audio_current(request.id) && ouse_usb_audio_requested()){
        int16_t input[64],output[64]={0};size_t bytes=0;
        // RX supplies the actual hardware clock even when only speaker is open.
        error=i2s_channel_read(rx,input,sizeof(input),&bytes,50);
        if(error!=ESP_OK || bytes!=sizeof(input)){if(error==ESP_OK)error=ESP_FAIL;break;}
        if(!ouse_devices_audio_current(request.id) || !ouse_usb_audio_requested())break;
        if(ouse_usb_microphone_requested())ouse_usb_microphone_write(input,64);
        ouse_usb_state_t usb;ouse_usb_get(&usb);
        error=volume(!usb.speaker);if(error!=ESP_OK)break;
        (void)ouse_usb_speaker_read(output,64);
        bytes=0;error=i2s_channel_write(tx,output,sizeof(output),&bytes,50);
        if(error!=ESP_OK || bytes!=sizeof(output)){if(error==ESP_OK)error=ESP_FAIL;break;}
        state.position_ms=(++frames)*4;state.mode=usb.microphone ? OUSE_AUDIO_RECORDING : OUSE_AUDIO_PLAYBACK;
        if(frames%16==0)ouse_devices_publish_audio(&state,request.id);
    }
    if(opened)error=close_audio(error);
    bool cancelled=ouse_task_cancel_requested(request.id);
    state.mode=error==ESP_OK ? OUSE_AUDIO_IDLE : OUSE_AUDIO_ERROR;state.error=error;state.peak=0;
    state.completed_id=request.id;ouse_devices_publish_audio(&state,request.id);
    // Stop is acknowledged only after I2S/codec cleanup, including disconnect.
    ouse_task_finish(request.id,error,cancelled);
    ouse_usb_audio_running(request.id,false,error);
}
static void recorder_request(ouse_audio_request_t request)
{
    bool claimed=ouse_task_start(request.id,true);esp_err_t error=claimed ? prepare_codec() : ESP_ERR_INVALID_STATE;bool opened=false;
    if(error==ESP_OK){esp_codec_dev_sample_info_t format={.sample_rate=16000,.channel=1,.bits_per_sample=16};data_error=ESP_CODEC_DEV_OK;
        opened=esp_codec_dev_open(codec,&format)==ESP_CODEC_DEV_OK && data_error==ESP_CODEC_DEV_OK;if(!opened){release_codec();error=ESP_FAIL;}}
    if(error==ESP_OK)error=volume(true);
    if(error==ESP_OK && esp_codec_dev_set_in_gain(codec,24.0f)!=ESP_CODEC_DEV_OK)error=ESP_FAIL;
    ouse_audio_state_t state;ouse_audio_get(&state);state.mode=OUSE_AUDIO_RECORDING;state.position_ms=0;state.peak=0;
    int16_t samples[256];unsigned captured=0;
    while(error==ESP_OK && ouse_recorder_current(request.id) && ouse_devices_audio_current(request.id)){
        if(esp_codec_dev_read(codec,samples,sizeof(samples))!=ESP_CODEC_DEV_OK){error=ESP_FAIL;break;}
        if(!ouse_recorder_write(request.id,samples,256))break;
        captured+=256;state.position_ms=captured/16;
        if(captured%1024==0)ouse_devices_publish_audio(&state,request.id);
    }
    if(opened)error=close_audio(error);
    state.mode=error==ESP_OK ? OUSE_AUDIO_IDLE : OUSE_AUDIO_ERROR;state.error=error;state.completed_id=request.id;state.peak=0;ouse_devices_publish_audio(&state,request.id);
    ouse_recorder_producer_done(request.id,error);
}
void ouse_board_audio_worker(void *argument)
{
    (void)argument;
    // Reserve the existing fixed-size duplex DMA/codec cache before apps and
    // diagnostic UI compete for DMA-capable internal memory. Channels stay
    // disabled: this does not start playback or microphone capture. A failed
    // reservation remains retryable through the normal operation error path.
    esp_err_t prepared=prepare_codec();
    ESP_LOGI("OuseAudio","Idle DMA/codec reservation: %s; DMA free=%u",esp_err_to_name(prepared),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));
    for(;;){
        ouse_audio_request_t request;
        if(!ouse_devices_take_audio(&request)){
            if(overlay_update()){
                esp_err_t error=prepare_codec();bool opened=false;
                if(error==ESP_OK){
                    esp_codec_dev_sample_info_t format={.sample_rate=16000,.channel=1,.bits_per_sample=16};data_error=ESP_CODEC_DEV_OK;
                    opened=esp_codec_dev_open(codec,&format)==ESP_CODEC_DEV_OK && data_error==ESP_CODEC_DEV_OK;
                    if(!opened){release_codec();error=ESP_FAIL;}
                }
                while(error==ESP_OK && !ouse_devices_audio_waiting() && overlay_update()){
                    int16_t samples[256]={0};error=volume(false);mix_output(samples,256);
                    if(error==ESP_OK && esp_codec_dev_write(codec,samples,sizeof(samples))!=ESP_CODEC_DEV_OK)error=ESP_FAIL;
                }
                if(opened)error=close_audio(error);
                if(error!=ESP_OK)overlay_finish(error,false);
                continue;
            }
            ulTaskNotifyTake(pdTRUE,portMAX_DELAY);continue;
        }
        if(overlay_request.id && !ouse_devices_overlay_current(overlay_request.id))overlay_finish(ESP_OK,true);
        if(request.action==OUSE_AUDIO_DISK_CAPTURE)recorder_request(request);
        else if(request.action==OUSE_AUDIO_USB)usb_request(request);
        else if(request.action==OUSE_AUDIO_MEDIA)media_request(request);
        else{
            short_request(request);
            if(request.action==OUSE_AUDIO_RING)(void)ouse_devices_resume_media(true);
        }
    }
}
