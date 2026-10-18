"""Exercise the product board audio worker against a deterministic codec/driver double, without hardware I/O."""
from pathlib import Path
import os, subprocess, sys
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/".tools/ouse-audio-check"
OUT.mkdir(exist_ok=True)
assert (ROOT/".tools/ouse-core-check/esp_err.h").exists(), "Run test-ouse-core.py first"
vswhere=Path(os.environ["ProgramFiles(x86)"])/"Microsoft Visual Studio/Installer/vswhere.exe"
vs=subprocess.check_output([str(vswhere),"-latest","-products","*","-requires","Microsoft.VisualStudio.Component.VC.Tools.x86.x64","-property","installationPath"],text=True).strip()
script=OUT/"environment.cmd"
script.write_text(f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n',encoding="mbcs")
env=dict(os.environ)
for line in subprocess.check_output(["cmd.exe","/d","/c",str(script)],encoding="mbcs").splitlines():
 if "=" in line and not line.startswith("="):
  k,v=line.split("=",1);env[k]=v
compiler=Path(env["VCToolsInstallDir"])/"bin/Hostx64/x64/cl.exe"

headers=r'''
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
int64_t esp_timer_get_time(void);
#define BSP_I2C_NUM 0
#define CONFIG_BSP_I2S_NUM 0
#define BSP_I2S_MCLK 13
#define BSP_I2S_SCLK 12
#define BSP_I2S_LCLK 10
#define BSP_I2S_DOUT 9
#define BSP_I2S_DSIN 11
#define BSP_POWER_AMP_IO 53
#define ES8311_CODEC_DEFAULT_ADDR 0x30
#define ESP_CODEC_DEV_WORK_MODE_BOTH 3
#define ESP_CODEC_DEV_TYPE_IN_OUT 3
#define ESP_CODEC_DEV_OK 0
#define ESP_CODEC_DEV_DRV_ERR ESP_FAIL
#define ESP_CODEC_DEV_NOT_SUPPORT ESP_ERR_NOT_SUPPORTED
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define I2S_ROLE_MASTER 0
#define I2S_DATA_BIT_WIDTH_16BIT 16
#define I2S_SLOT_MODE_MONO 1
#define I2S_STD_SLOT_LEFT 1
#define I2S_STD_SLOT_BOTH 3
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
void vTaskDelay(unsigned ticks);
#define portMAX_DELAY 0xffffffff
#define I2S_CHANNEL_DEFAULT_CONFIG(a,b) {.dma_frame_num=240}
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) {rate}
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(a,b) {a,b,I2S_STD_SLOT_BOTH}
typedef void *i2s_chan_handle_t;
typedef struct {bool auto_clear;unsigned dma_frame_num;} i2s_chan_config_t;
typedef struct {struct {int rate;} clk_cfg;struct {int bits,channels,slot_mask;} slot_cfg;struct{int mclk,bclk,ws,dout,din;} gpio_cfg;} i2s_std_config_t;
typedef struct {bool is_enabled;size_t total_dma_buf_size;} i2s_chan_info_t;
typedef int esp_codec_dev_type_t;
typedef struct {int sample_rate,channel,bits_per_sample,channel_mask,mclk_multiple;} esp_codec_dev_sample_info_t;
typedef struct audio_codec_data_if_t audio_codec_data_if_t;
struct audio_codec_data_if_t {
 int (*open)(const audio_codec_data_if_t *,void *,int);
 bool (*is_open)(const audio_codec_data_if_t *);
 int (*enable)(const audio_codec_data_if_t *,int,bool);
 int (*set_fmt)(const audio_codec_data_if_t *,int,esp_codec_dev_sample_info_t *);
 int (*read)(const audio_codec_data_if_t *,uint8_t *,int);
 int (*write)(const audio_codec_data_if_t *,uint8_t *,int);
 int (*close)(const audio_codec_data_if_t *);
};
typedef struct {int dummy;} audio_codec_ctrl_if_t;
typedef struct {int dummy;} audio_codec_gpio_if_t;
typedef struct {int dummy;} audio_codec_if_t;
typedef void *esp_codec_dev_handle_t;
typedef struct {int port;void *tx_handle,*rx_handle;} audio_codec_i2s_cfg_t;
typedef struct {int port,addr;void *bus_handle;} audio_codec_i2c_cfg_t;
typedef struct {const void *ctrl_if,*gpio_if;int codec_mode,pa_pin;bool master_mode,use_mclk;struct{double pa_voltage,codec_dac_voltage;} hw_gain;} es8311_codec_cfg_t;
typedef struct {int dev_type;const void *codec_if,*data_if;} esp_codec_dev_cfg_t;
void *bsp_i2c_get_handle(void);
int i2c_master_probe(void *,int,int);
int i2s_new_channel(const i2s_chan_config_t *,void **,void **);
int i2s_channel_init_std_mode(void *,const i2s_std_config_t *);
int i2s_channel_read(void *,void *,size_t,size_t *,unsigned);
int i2s_channel_write(void *,const void *,size_t,size_t *,unsigned);
int i2s_channel_get_info(void *,i2s_chan_info_t *);
int i2s_channel_enable(void *);
int i2s_channel_preload_data(void *,const void *,size_t,size_t *);
int i2s_channel_disable(void *);
int i2s_del_channel(void *);
const audio_codec_data_if_t *audio_codec_new_i2s_data(audio_codec_i2s_cfg_t *);
const audio_codec_ctrl_if_t *audio_codec_new_i2c_ctrl(audio_codec_i2c_cfg_t *);
const audio_codec_gpio_if_t *audio_codec_new_gpio(void);
const audio_codec_if_t *es8311_codec_new(es8311_codec_cfg_t *);
void *esp_codec_dev_new(esp_codec_dev_cfg_t *);
void esp_codec_dev_delete(void *);
void audio_codec_delete_codec_if(const audio_codec_if_t *);
void audio_codec_delete_data_if(const audio_codec_data_if_t *);
void audio_codec_delete_ctrl_if(const audio_codec_ctrl_if_t *);
void audio_codec_delete_gpio_if(const audio_codec_gpio_if_t *);
int esp_codec_dev_open(void *,esp_codec_dev_sample_info_t *);
int esp_codec_dev_close(void *);
int esp_codec_dev_set_out_vol(void *,int);
int esp_codec_dev_set_out_mute(void *,bool);
int esp_codec_dev_set_in_gain(void *,float);
int esp_codec_dev_write(void *,void *,int);
int esp_codec_dev_read(void *,void *,int);
void *heap_caps_malloc(size_t,unsigned);
unsigned ulTaskNotifyTake(int,unsigned);
const char *esp_err_to_name(int);
'''
(OUT/"fakes.h").write_text(headers)
for name in ["esp_timer.h","bsp/esp-bsp.h","driver/i2s_std.h","esp_codec_dev.h","esp_codec_dev_defaults.h","esp_heap_caps.h","esp_log.h","freertos/FreeRTOS.h","freertos/task.h"]:
 f=OUT/name;f.parent.mkdir(exist_ok=True,parents=True);f.write_text('#include "fakes.h"\n')
fixture=r'''
#include "fakes.h"
#include "ouse_device_driver.h"
#include "ouse_media.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>
static jmp_buf idle;
static uint32_t observed_task;
static bool observed_cancel_at_close;
void vTaskDelay(unsigned ticks){(void)ticks;}
static int64_t sample_clock;
int64_t esp_timer_get_time(void){return ++sample_clock;}
static int fail_probe,fail_open,fail_init,fail_allocate,fail_io;
static bool short_io;
static int fail_enable_tx,fail_enable_rx,fail_disable_tx,fail_disable_rx,fail_preload;
static unsigned allocated,opened,closed,writes,reads,cancel_after;
static unsigned invalid_transitions,format_changes,dma_frames;
typedef struct {bool initialized,running;bool output;size_t preloaded;uint8_t dma[3072];} fake_channel_t;
typedef struct {audio_codec_data_if_t base;void *tx,*rx;} fake_data_t;
typedef struct {const audio_codec_data_if_t *data;bool opened;} fake_codec_t;
static fake_channel_t *active_tx,*active_rx;
static fake_data_t *active_data;
static unsigned active_codecs,partial_rollbacks;
static bool rx_start_failed;
static int output_volume;static bool output_muted;
void *bsp_i2c_get_handle(void){return (void*)1;}
int i2c_master_probe(void *b,int addr,int ms){assert(b && addr==0x18 && ms<=100);return fail_probe ? ESP_FAIL : 0;}
int i2s_new_channel(const i2s_chan_config_t *c,void **tx,void **rx){assert(c->auto_clear&&!active_tx&&!active_rx);dma_frames=c->dma_frame_num;*tx=active_tx=calloc(1,sizeof(fake_channel_t));*rx=active_rx=calloc(1,sizeof(fake_channel_t));active_tx->output=true;allocated+=2;return 0;}
int i2s_channel_init_std_mode(void *p,const i2s_std_config_t *c){assert(p && c->clk_cfg.rate==16000 && c->slot_cfg.bits==16 && c->slot_cfg.channels==1);assert(c->slot_cfg.slot_mask==(((fake_channel_t*)p)->output?I2S_STD_SLOT_BOTH:I2S_STD_SLOT_LEFT));if(fail_init)return ESP_FAIL;((fake_channel_t*)p)->initialized=true;return 0;}
int i2s_channel_get_info(void *p,i2s_chan_info_t *info){assert(p&&info);fake_channel_t *c=p;info->is_enabled=c->running;info->total_dma_buf_size=c->initialized?sizeof(c->dma):0;return 0;}
int i2s_channel_preload_data(void *p,const void *buffer,size_t size,size_t *loaded){fake_channel_t *c=p;assert(c&&c->output&&c->initialized&&!c->running&&buffer&&loaded);if(fail_preload){--fail_preload;return ESP_FAIL;}*loaded=size<sizeof(c->dma)-c->preloaded?size:sizeof(c->dma)-c->preloaded;memcpy(c->dma+c->preloaded,buffer,*loaded);c->preloaded+=*loaded;return 0;}
int i2s_channel_enable(void *p){fake_channel_t *c=p;assert(c);if(!c->initialized || c->running){++invalid_transitions;return ESP_ERR_INVALID_STATE;}if(c->output){assert(c->preloaded==sizeof(c->dma));for(unsigned i=0;i<sizeof(c->dma);++i)assert(c->dma[i]==0);}int *failure=c->output?&fail_enable_tx:&fail_enable_rx;if(*failure){--*failure;if(!c->output){assert(active_tx->running);rx_start_failed=true;}return ESP_FAIL;}c->running=true;return 0;}
int i2s_channel_disable(void *p){fake_channel_t *c=p;assert(c);if(!c->running){++invalid_transitions;return ESP_ERR_INVALID_STATE;}int *failure=c->output?&fail_disable_tx:&fail_disable_rx;if(*failure){--*failure;return ESP_FAIL;}c->running=false;c->preloaded=0;if(c->output&&rx_start_failed){assert(!active_rx->running);++partial_rollbacks;rx_start_failed=false;}return 0;}
int i2s_del_channel(void *p){assert(p&&!((fake_channel_t*)p)->running&&!active_codecs&&!active_data);if(p==active_tx)active_tx=NULL;else {assert(p==active_rx);active_rx=NULL;}free(p);--allocated;return 0;}
static bool fake_data_opened(const audio_codec_data_if_t *h){return h!=NULL;}
// Match pinned esp_codec_dev's lifecycle, including ignored disable errors on
// set_fmt and the final-RX-only return value from its duplex enable callback.
static int fake_data_enable(const audio_codec_data_if_t *h,int type,bool enable){const fake_data_t *d=(const fake_data_t*)h;assert(type==3);int result=enable?i2s_channel_enable(d->tx):i2s_channel_disable(d->tx);result=enable?i2s_channel_enable(d->rx):i2s_channel_disable(d->rx);return result;}
static int fake_data_format(const audio_codec_data_if_t *h,int type,esp_codec_dev_sample_info_t *f){const fake_data_t *d=(const fake_data_t*)h;assert(type==3&&f->sample_rate==16000&&f->channel==1&&f->bits_per_sample==16);(void)i2s_channel_disable(d->tx);(void)i2s_channel_disable(d->rx);assert(!((fake_channel_t*)d->tx)->running&&!((fake_channel_t*)d->rx)->running);++format_changes;return 0;}
static int fake_data_write(const audio_codec_data_if_t *h,uint8_t *b,int n){const fake_data_t *d=(const fake_data_t*)h;assert(d==active_data&&((fake_channel_t*)d->tx)->running&&b&&n<=512);memcpy(((fake_channel_t*)d->tx)->dma,b,n);++writes;if(cancel_after&&writes==cancel_after)(void)ouse_audio_stop();return fail_io?ESP_FAIL:0;}
static int fake_data_read(const audio_codec_data_if_t *h,uint8_t *b,int n){const fake_data_t *d=(const fake_data_t*)h;assert(d==active_data&&((fake_channel_t*)d->rx)->running&&b&&output_muted&&n<=512);++reads;int16_t *s=(int16_t*)b;for(int i=0;i<n/2;++i)s[i]=(i&1)?12000:-12000;return fail_io?ESP_FAIL:0;}
int i2s_channel_read(void *p,void *b,size_t n,size_t *got,unsigned wait){assert(p==active_rx&&wait==1000);int error=fake_data_read(&active_data->base,b,(int)n);*got=error?0:short_io?n-2:n;sample_clock+=(int64_t)*got*1000000/32000;return error;}
int i2s_channel_write(void *p,const void *b,size_t n,size_t *sent,unsigned wait){assert(p==active_tx&&wait==1000);int error=fake_data_write(&active_data->base,(uint8_t*)b,(int)n);*sent=error?0:short_io?n-2:n;sample_clock+=(int64_t)*sent*1000000/32000;return error;}
const audio_codec_data_if_t *audio_codec_new_i2s_data(audio_codec_i2s_cfg_t *c){assert(c->tx_handle&&c->rx_handle&&!active_data);fake_data_t *d=calloc(1,sizeof(*d));d->base=(audio_codec_data_if_t){.is_open=fake_data_opened,.enable=fake_data_enable,.set_fmt=fake_data_format,.read=fake_data_read,.write=fake_data_write};d->tx=c->tx_handle;d->rx=c->rx_handle;active_data=d;++allocated;return &d->base;}
const audio_codec_ctrl_if_t *audio_codec_new_i2c_ctrl(audio_codec_i2c_cfg_t *c){assert(c->bus_handle);++allocated;return malloc(1);}
const audio_codec_gpio_if_t *audio_codec_new_gpio(void){++allocated;return malloc(1);}
const audio_codec_if_t *es8311_codec_new(es8311_codec_cfg_t *c){assert(c->codec_mode==ESP_CODEC_DEV_WORK_MODE_BOTH);++allocated;return malloc(1);}
void *esp_codec_dev_new(esp_codec_dev_cfg_t *c){assert(c->dev_type==ESP_CODEC_DEV_TYPE_IN_OUT);fake_codec_t *codec=calloc(1,sizeof(*codec));codec->data=c->data_if;++allocated;++active_codecs;return codec;}
void esp_codec_dev_delete(void *p){(void)esp_codec_dev_close(p);free(p);--allocated;--active_codecs;}
void audio_codec_delete_codec_if(const audio_codec_if_t *p){free((void*)p);--allocated;}
void audio_codec_delete_data_if(const audio_codec_data_if_t *p){assert(!active_codecs&&p==&active_data->base);free((void*)p);active_data=NULL;--allocated;}
void audio_codec_delete_ctrl_if(const audio_codec_ctrl_if_t *p){free((void*)p);--allocated;}
void audio_codec_delete_gpio_if(const audio_codec_gpio_if_t *p){free((void*)p);--allocated;}
int esp_codec_dev_open(void *p,esp_codec_dev_sample_info_t *f){fake_codec_t *c=p;assert(c&&c->data->is_open(c->data)&&f->sample_rate==16000&&f->channel==1&&f->bits_per_sample==16);assert(!c->opened);c->opened=true;++opened;(void)c->data->set_fmt(c->data,3,f);(void)c->data->enable(c->data,3,true);return fail_open?ESP_FAIL:0;}
int esp_codec_dev_close(void *p){fake_codec_t *c=p;assert(c);if(c->opened){if(observed_task){ouse_task_snapshot_t task;assert(ouse_task_get(NULL,observed_task,&task) && (task.state==OUSE_TASK_CANCELLING || task.state==OUSE_TASK_CANCELLED));observed_cancel_at_close=true;}(void)c->data->enable(c->data,3,false);c->opened=false;++closed;}return 0;}
int esp_codec_dev_set_out_vol(void *p,int v){assert(p);output_volume=v;return 0;}
int esp_codec_dev_set_out_mute(void *p,bool m){assert(p);output_muted=m;return 0;}
int esp_codec_dev_set_in_gain(void *p,float g){assert(p && g==24);return 0;}
int esp_codec_dev_write(void *p,void *b,int n){fake_codec_t *c=p;assert(c&&c->opened);return c->data->write(c->data,b,n);}
int esp_codec_dev_read(void *p,void *b,int n){fake_codec_t *c=p;assert(c&&c->opened);return c->data->read(c->data,b,n);}
void *heap_caps_malloc(size_t n,unsigned flags){assert(n==96000 && flags==3);return fail_allocate ? NULL : malloc(n);}
unsigned ulTaskNotifyTake(int a,unsigned b){(void)a;(void)b;longjmp(idle,1);return 0;}
const char *esp_err_to_name(int e){(void)e;return "test";}

#include "ouse_usb.h"
bool ouse_usb_audio_requested(void){return false;}
bool ouse_usb_microphone_requested(void){return false;}
unsigned ouse_usb_speaker_read(int16_t *p,unsigned n){(void)p;(void)n;return 0;}
void ouse_usb_microphone_write(const int16_t *p,unsigned n){(void)p;(void)n;}
void ouse_usb_audio_running(uint32_t id,bool on,esp_err_t error){(void)id;(void)on;(void)error;}
void ouse_usb_get(ouse_usb_state_t *out){memset(out,0,sizeof(*out));}
#include "BOARD_AUDIO"
static bool media_fixture;
static ouse_media_state_t media_state;
static unsigned media_reads,media_samples,media_interrupts;
void ouse_media_consumer_started(uint32_t id){assert(media_fixture&&id==media_state.task_id);}
unsigned ouse_media_read(uint32_t id,int16_t *samples,unsigned capacity,bool *done,esp_err_t *error){
 assert(media_fixture&&id==media_state.task_id&&capacity==256);++media_reads;*error=ESP_OK;*done=false;
 if(media_reads==2){ouse_task_owner_t alarm={.app_id="ouse.alarm"};uint32_t ring;assert(ouse_audio_submit(&alarm,OUSE_AUDIO_RING,1,&ring)==ESP_OK);return 0;}
 if(media_reads>=4){*done=true;return 0;}
 for(unsigned i=0;i<capacity;++i)samples[i]=1000;return capacity;
}
void ouse_media_consumed(uint32_t id,unsigned n){assert(id==media_state.task_id);media_samples+=n;}
void ouse_media_interrupted(uint32_t id,bool value){assert(id==media_state.task_id);media_state.interrupted=value;++media_interrupts;}
void ouse_media_consumer_done(uint32_t id,esp_err_t error,bool cancelled){assert(media_fixture&&opened==closed&&!media_state.interrupted);media_state.active=false;ouse_task_finish(id,error,cancelled);}
bool ouse_media_get(const char *app,ouse_media_state_t *state){(void)app;if(!media_fixture)return false;*state=media_state;return true;}

static unsigned sd_samples;static esp_err_t sd_error;static bool sd_done;
bool ouse_recorder_current(uint32_t id){return !sd_done && sd_samples<1024 && !ouse_task_cancel_requested(id);}
bool ouse_recorder_write(uint32_t id,const int16_t *samples,unsigned count){assert(id&&samples&&count==256);sd_samples+=count;return true;}
void ouse_recorder_producer_done(uint32_t id,esp_err_t error){assert(opened==closed);sd_done=true;sd_error=error;ouse_task_finish(id,error,ouse_task_cancel_requested(id));}

static ouse_audio_request_t take(void){ouse_audio_request_t r;assert((ouse_devices_take_audio(&r) || ouse_devices_take_overlay(&r)) && ouse_task_start(r.id,true));return r;}
static esp_err_t execute_request(ouse_audio_request_t request,ouse_audio_state_t *state){esp_err_t error=run(request,state);ouse_task_finish(request.id,error,ouse_task_cancel_requested(request.id));return error;}
static void pump(void){if(!setjmp(idle))ouse_board_audio_worker(NULL);}
int main(int argc,char **argv){
 ouse_device_port_t port={0};ouse_device_config_t config={.volume=40};assert(ouse_devices_init(&port,&config)==0);ouse_devices_set_available(1,true,0);

 if(argc>1 && !strcmp(argv[1],"--mixing")){
  ouse_task_owner_t player,alert,other;
  assert(ouse_task_owner_open("user.player",&player)==ESP_OK);
  assert(ouse_task_owner_open("user.alert",&alert)==ESP_OK);
  assert(ouse_task_owner_open("user.other",&other)==ESP_OK);
  uint32_t music,ring,rejected,stop;
  assert(ouse_task_create(&player,OUSE_TASK_AUDIO,"play",OUSE_TASK_FINISH_ON_CLOSE,true,&music)==ESP_OK);
  assert(ouse_devices_queue_media(music)==ESP_OK);ouse_audio_request_t request=take();assert(request.id==music);
  assert(ouse_audio_submit(&alert,OUSE_AUDIO_ALERT,0,&ring)==ESP_OK);
  int16_t samples[1024];for(unsigned index=0;index<1024;++index)samples[index]=1000;
  mix_output(samples,1024);assert(mixer.gain==8192);
  bool varied=false;for(unsigned index=512;index<1024;++index)if(samples[index]!=250)varied=true;assert(varied);
  ouse_task_snapshot_t task;assert(ouse_task_get(&player,music,&task)&&task.state==OUSE_TASK_RUNNING&&!task.cancel_requested);
  assert(ouse_task_get(&alert,ring,&task)&&task.state==OUSE_TASK_RUNNING);
  assert(ouse_audio_submit(&other,OUSE_AUDIO_TEST,0,&rejected)==ESP_ERR_INVALID_STATE);
  assert(ouse_audio_submit(&other,OUSE_AUDIO_ALERT,0,&rejected)==ESP_ERR_INVALID_STATE);
  assert(ouse_audio_submit(&other,OUSE_AUDIO_CAPTURE,1,&rejected)==ESP_ERR_INVALID_STATE);
  assert(ouse_audio_submit(&other,OUSE_AUDIO_STOP,0,&stop)==ESP_OK);
  assert(!ouse_task_cancel_requested(music)&&!ouse_task_cancel_requested(ring));
  assert(ouse_audio_submit(&alert,OUSE_AUDIO_STOP,0,&stop)==ESP_OK);
  for(unsigned index=0;index<1024;++index)samples[index]=1000;
  mix_output(samples,1024);assert(mixer.gain==32768&&samples[1023]==1000);
  assert(ouse_task_get(&alert,ring,&task)&&task.state==OUSE_TASK_CANCELLED);
  assert(!ouse_task_cancel_requested(music));
  assert(ouse_audio_submit(&alert,OUSE_AUDIO_TEST,0,&ring)==ESP_OK);
  for(unsigned index=0;index<1024;++index)samples[index]=32767;
  mix_output(samples,1024);assert(mixer.gain==16384);
  for(unsigned index=0;index<1024;++index)assert(samples[index]>=0&&samples[index]<=32767);
  assert(ouse_task_cancel(&alert,ring,"confirmed")==ESP_OK);mix_output(samples,1024);
  assert(ouse_audio_submit(&alert,OUSE_AUDIO_ALERT,0,&ring)==ESP_OK);mix_output(samples,1024);
  ouse_task_owner_close(&alert);mix_output(samples,1024);
  assert(ouse_task_get(NULL,ring,&task)&&task.state==OUSE_TASK_CANCELLED);
  assert(!ouse_task_cancel_requested(music));
  ouse_task_finish(music,ESP_OK,false);
  assert(ouse_audio_submit(&other,OUSE_AUDIO_CAPTURE,1,&rejected)==ESP_OK);
  assert(ouse_audio_submit(&player,OUSE_AUDIO_TEST,0,&ring)==ESP_ERR_INVALID_STATE);
  assert(ouse_audio_submit(&player,OUSE_AUDIO_ALERT,0,&ring)==ESP_ERR_INVALID_STATE);
  puts("PASS mixing: production PCM overlay, 25/50 percent duck and smooth restoration, no clipping, music task preserved, priority, recording exclusion, owner-scoped stop/close");return 0;
 }

 if(argc>1 && !strcmp(argv[1],"--sd-recording")){
  ouse_task_owner_t owner;assert(ouse_task_owner_open("user.recorder",&owner)==ESP_OK);uint32_t identifier;
  assert(ouse_audio_submit(&owner,OUSE_AUDIO_DISK_CAPTURE,0,&identifier)==ESP_OK);
  uint32_t rejected;assert(ouse_audio_submit(&owner,OUSE_AUDIO_DISK_CAPTURE,0,&rejected)==ESP_ERR_INVALID_STATE);
  pump();assert(sd_done&&sd_samples==1024&&sd_error==ESP_OK&&opened==closed&&output_muted);
  sd_done=false;sd_samples=0;fail_open=1;assert(ouse_audio_submit(&owner,OUSE_AUDIO_DISK_CAPTURE,0,&identifier)==ESP_OK);pump();
  assert(sd_done&&sd_samples==0&&sd_error==ESP_FAIL&&opened==closed);
  release_codec();puts("PASS SD recorder codec ownership: PCM16 mono 16kHz, bounded frames, busy rejection, mute, cleanup-before-finalization, startup error propagation");return 0;
 }
 if(argc>1 && !strcmp(argv[1],"--recording-format")){
  ouse_audio_state_t state;ouse_audio_get(&state);assert(ouse_audio_record(3)==ESP_OK);ouse_audio_request_t request=take();
  int64_t started=esp_timer_get_time();assert(execute_request(request,&state)==ESP_OK&&recorded_samples==48000&&state.recorded_ms==3000);
  assert(esp_timer_get_time()-started>=3000000&&esp_timer_get_time()-started<3020000);
  state.mode=OUSE_AUDIO_IDLE;ouse_devices_publish_audio(&state,request.id);assert(ouse_audio_play_recording()==ESP_OK);request=take();started=esp_timer_get_time();
  assert(execute_request(request,&state)==ESP_OK&&state.position_ms==3000);
  assert(esp_timer_get_time()-started>=3000000&&esp_timer_get_time()-started<3020000);
  state.mode=OUSE_AUDIO_IDLE;ouse_devices_publish_audio(&state,request.id);short_io=true;assert(ouse_audio_record(1)==ESP_OK);request=take();assert(execute_request(request,&state)==ESP_FAIL);short_io=false;
  release_codec();free(recording);recording=NULL;assert(!allocated&&!invalid_transitions&&!format_changes);
  puts("PASS actual board capture/replay: TX mono duplication, RX left slot only, 48000 samples at 16kHz take 3s in each direction; short DMA transfer rejected; no driver churn");return 0;
 }
 if(argc>1 && !strcmp(argv[1],"--media-focus")){
  ouse_task_owner_t owner;assert(ouse_task_owner_open("user.player",&owner)==ESP_OK);uint32_t id;
  assert(ouse_task_create(&owner,OUSE_TASK_AUDIO,"play",OUSE_TASK_FINISH_ON_CLOSE,true,&id)==ESP_OK);
  media_fixture=true;media_state=(ouse_media_state_t){.active=true,.task_id=id};assert(ouse_devices_queue_media(id)==ESP_OK);pump();
  ouse_task_snapshot_t task;assert(ouse_task_get(&owner,id,&task)&&task.state==OUSE_TASK_SUCCEEDED&&!task.cancel_requested);
  assert(media_reads==4&&media_samples==512&&media_interrupts==0&&opened==2&&closed==2&&!media_state.active);
  release_codec();assert(!allocated&&!invalid_transitions&&!format_changes);
  puts("PASS actual codec media focus: overlay mixed without interrupting or cancelling media; one writer, remaining alert completes after media EOF");return 0;
 }
 if(argc>1){
  bool cleanup_failure=!strcmp(argv[1],"--cleanup-error");assert(cleanup_failure||!strcmp(argv[1],"--kernel-only"));if(cleanup_failure)fail_disable_rx=1;ouse_task_owner_t owner;assert(ouse_task_owner_open("user.audio",&owner)==ESP_OK);
  uint32_t id;assert(ouse_audio_submit(&owner,OUSE_AUDIO_TEST,0,&id)==ESP_OK);observed_task=id;cancel_after=2;pump();
  ouse_task_snapshot_t task;assert(observed_cancel_at_close && opened==closed && output_muted);
  assert(ouse_task_get(&owner,id,&task) && (cleanup_failure ? task.state==OUSE_TASK_FAILED&&task.error==ESP_FAIL : task.state==OUSE_TASK_CANCELLED) && task.result.present && task.result.value.audio.position_ms>0);
  if(cleanup_failure)assert(!allocated);
  observed_task=0;ouse_task_owner_close(&owner);release_codec();free(recording);
  assert(!allocated);puts("PASS actual overlay worker: cancellation stops further mixed samples, retained position result, codec cleanup and failure release");return 0;
 }
 ouse_audio_state_t state;ouse_audio_request_t req;
 fail_probe=1;assert(ouse_audio_test_tone()==0);req=take();ouse_audio_get(&state);assert(execute_request(req,&state)==ESP_ERR_NOT_FOUND && !allocated);fail_probe=0;
 fail_init=1;assert(ouse_audio_test_tone()==0);req=take();assert(execute_request(req,&state)==ESP_FAIL && !allocated);fail_init=0;
 fail_open=1;assert(ouse_audio_test_tone()==0);req=take();assert(execute_request(req,&state)==ESP_FAIL && !allocated);fail_open=0;
 assert(ouse_audio_test_tone()==0);req=take();assert(execute_request(req,&state)==0 && writes==32 && output_muted && opened==closed);
 printf("I2S state audit: invalid transitions=%u, redundant format changes=%u, configured DMA frames=%u\n",invalid_transitions,format_changes,dma_frames);fflush(stdout);
 assert(invalid_transitions==0 && format_changes==0 && dma_frames==64);
 assert(!active_tx->running&&!active_rx->running);
 assert(fixed_enable(&fixed_data_if,3,false)==0&&fixed_enable(&fixed_data_if,3,false)==0);
 esp_codec_dev_sample_info_t unsupported={.sample_rate=22050,.channel=1,.bits_per_sample=16};
 assert(fixed_format(&fixed_data_if,3,&unsupported)==ESP_CODEC_DEV_NOT_SUPPORT);
 assert(fixed_enable(&fixed_data_if,3,true)==ESP_CODEC_DEV_NOT_SUPPORT);
 assert(!active_tx->running&&!active_rx->running);
 unsigned resources=allocated;assert(resources==7 && output_volume==40);
 // The codec double intentionally ignores data_if callback failures like the
 // pinned component. The real board worker must still reject failed startup.
 fail_preload=1;assert(ouse_audio_test_tone()==0);req=take();unsigned before=writes;assert(execute_request(req,&state)==ESP_FAIL&&!allocated&&writes==before&&opened==closed);
 assert(ouse_audio_test_tone()==0);req=take();assert(execute_request(req,&state)==0&&allocated==resources);
 fail_enable_tx=1;assert(ouse_audio_test_tone()==0);req=take();before=writes;assert(execute_request(req,&state)==ESP_FAIL&&!allocated&&writes==before&&opened==closed);
 assert(ouse_audio_test_tone()==0);req=take();assert(execute_request(req,&state)==0&&allocated==resources);
 fail_enable_rx=1;assert(ouse_audio_test_tone()==0);req=take();before=writes;assert(execute_request(req,&state)==ESP_FAIL&&!allocated&&writes==before&&partial_rollbacks==1&&opened==closed);
 assert(ouse_audio_test_tone()==0);req=take();assert(execute_request(req,&state)==0&&allocated==resources);
 fail_disable_rx=1;assert(ouse_audio_test_tone()==0);req=take();assert(execute_request(req,&state)==ESP_FAIL&&!allocated&&opened==closed);
 assert(ouse_audio_test_tone()==0);req=take();assert(execute_request(req,&state)==0&&allocated==resources);
 fail_disable_tx=1;assert(ouse_audio_test_tone()==0);req=take();assert(execute_request(req,&state)==ESP_FAIL&&!allocated&&opened==closed);
 assert(ouse_audio_test_tone()==0);req=take();assert(execute_request(req,&state)==0&&allocated==resources);
 // Persistent cleanup failure retains the old channel instead of overwriting
 // its only handle. A later retry finishes cleanup and can recreate the codec.
 fail_disable_tx=3;assert(ouse_audio_test_tone()==0);req=take();assert(execute_request(req,&state)==ESP_FAIL&&allocated==1&&tx&&rx==NULL);
 assert(ouse_audio_test_tone()==0);req=take();assert(execute_request(req,&state)==ESP_FAIL&&allocated==1&&tx);
 assert(ouse_audio_test_tone()==0);req=take();assert(execute_request(req,&state)==0&&allocated==resources);
 fail_allocate=1;assert(ouse_audio_record(3)==0);req=take();assert(execute_request(req,&state)==ESP_ERR_NO_MEM && !allocated && opened==closed);fail_allocate=0;
 assert(ouse_audio_record(3)==0);req=take();assert(execute_request(req,&state)==0 && recorded_samples==48000 && state.recorded_ms==3000 && state.peak>0);
 int16_t *saved_recording=recording;
 state.mode=OUSE_AUDIO_IDLE;ouse_devices_publish_audio(&state,req.id);
 assert(ouse_audio_play_recording()==0);req=take();before=writes;assert(execute_request(req,&state)==0 && writes-before==188 && allocated==resources && recording==saved_recording && recording[0]==-12000 && recording[1]==12000);
 assert(ouse_audio_test_tone()==0);req=take();before=writes;cancel_after=before+2;assert(execute_request(req,&state)==0 && writes-before==2 && !ouse_devices_audio_current(req.id));cancel_after=0;
 req=take();assert(req.action==OUSE_AUDIO_STOP && execute_request(req,&state)==0);
 fail_io=1;assert(ouse_audio_record(1)==0);req=take();assert(execute_request(req,&state)==ESP_FAIL && !allocated && opened==closed && recording==saved_recording);fail_io=0;
 release_codec();free(recording);recording=NULL;
 assert(invalid_transitions==0 && format_changes==0 && !allocated && !active_codecs && !active_data && !active_tx && !active_rx);
 puts("PASS actual board audio: fixed-format transport, no invalid I2S transitions/reconfiguration; DMA silence before every TX start, preload/TX/RX startup and stop errors, rollback/retry, release order, partial initialization/open/PSRAM/I/O failures, preserved PCM, bounded 3s recording/replay and cancellation");return 0;
}
'''.replace("BOARD_AUDIO",(ROOT/"boards/waveshare_lcd35/board_audio.c").as_posix())
(OUT/"check.c").write_text(fixture,encoding="utf-8")
result=subprocess.run([str(compiler),"/nologo","/utf-8","/std:c11","/O2","/MD","/UNDEBUG","/I"+str(OUT),"/I"+str(ROOT/".tools/ouse-core-check"),"/I"+str(ROOT/"components/ouse_devices/include"),"/I"+str(ROOT/"components/ouse_tasks/include"),"/I"+str(ROOT/"components/ouse_media/include"),"/I"+str(ROOT/"components/ouse_recorder/include"),"/I"+str(ROOT/"components/ouse_usb/include"),"/I"+str(ROOT/"components/ouse_system/include"),str(ROOT/"components/ouse_tasks/ouse_tasks.c"),str(OUT/"check.c"),str(ROOT/"components/ouse_devices/ouse_devices.c"),"/Fe"+str(OUT/"check.exe")],cwd=OUT,env=env,capture_output=True,text=True,errors="replace")
assert result.returncode==0,(result.stdout,result.stderr)
if "--mixing" in sys.argv:
 subprocess.run([str(OUT/"check.exe"),"--mixing"],cwd=OUT,check=True,timeout=15)
elif "--sd-recording" in sys.argv:
 subprocess.run([str(OUT/"check.exe"),"--sd-recording"],cwd=OUT,check=True,timeout=15)
elif "--recording-format" in sys.argv:
 subprocess.run([str(OUT/"check.exe"),"--recording-format"],cwd=OUT,check=True,timeout=15)
elif "--cleanup-error" in sys.argv:
 subprocess.run([str(OUT/"check.exe"),"--cleanup-error"],cwd=OUT,check=True,timeout=15)
else:
 subprocess.run([str(OUT/"check.exe")]+(["--kernel-only"] if "--kernel-only" in sys.argv else []),cwd=OUT,check=True,timeout=15)
 subprocess.run([str(OUT/"check.exe"),"--media-focus"],cwd=OUT,check=True,timeout=15)
