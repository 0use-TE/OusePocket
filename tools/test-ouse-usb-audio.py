"""Focused checks for the pinned TinyUSB OUT/feedback overlay, no USB I/O."""
from pathlib import Path
import importlib.util,os,subprocess
ROOT=Path(__file__).resolve().parents[1]
script=ROOT/'components/ouse_usb/patch_audio_driver.py'
spec=importlib.util.spec_from_file_location('patch_audio',script);mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
original=(ROOT/'managed_components/espressif__tinyusb/src/class/audio/audio_device.c').read_text(encoding='utf-8')
source=mod.patched(original)
try:mod.patched(source)
except RuntimeError:pass
else:raise AssertionError('overlay must fail closed on a changed source')
def function(name):
 # Skip forward declarations; extract the actual production definition.
 import re
 match=re.search(r'static bool '+name+r'\([^;]+?\)\s*\{',source);assert match,name
 start=match.start();at=source.index('{',match.start());end=at+1;depth=1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
# Feedback is queued only after parameter preparation, not while scanning endpoints.
set_interface=function('audiod_set_interface')
assert set_interface.index('audiod_fb_params_prepare(func_id, alt)')<set_interface.index('audiod_fb_send(func_id, false)')
fixture=r'''
#include <assert.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#define CFG_TUD_EDPT_DEDICATED_HWFIFO 0
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP 1
#define TU_VERIFY(x) do{if(!(x))return false;}while(0)
#define TU_BREAKPOINT() abort()
enum{TUSB_SPEED_FULL,TUSB_SPEED_HIGH,AUDIO_FEEDBACK_METHOD_DISABLED,AUDIO_FEEDBACK_METHOD_FREQUENCY_FIXED,AUDIO_FEEDBACK_METHOD_FREQUENCY_FLOAT,AUDIO_FEEDBACK_METHOD_FREQUENCY_POWER_OF_2,AUDIO_FEEDBACK_METHOD_FIFO_COUNT};
typedef struct{unsigned method,sample_freq;struct{unsigned fifo_threshold;}fifo_count;struct{unsigned mclk_freq;}frequency;} audio_feedback_params_t;
typedef struct{unsigned ep_fb,ep_out,ep_out_sz,ep_out_alt,ep_out_ff;uint8_t lin_buf_out[34];struct{unsigned compute_method,min_value,max_value,value,frame_shift;struct{unsigned power_of_2;float float_const;struct{unsigned sample_freq,mclk_freq;}fixed;struct{unsigned fifo_lvl_thr,fifo_lvl_avg,nom_value;uint16_t rate_const[2];}fifo_count;}compute;}feedback;}audiod_function_t;
static _Atomic uint32_t ouse_rx_packets,ouse_rx_bytes;
static audiod_function_t _audiod_fct[1];static unsigned speed,arms,callbacks,writes;static bool fifo_full;
static unsigned tud_speed_get(void){return speed;}
static unsigned tu_fifo_depth(void*p){(void)p;return 1024;}
static unsigned tu_fifo_count(void*p){(void)p;return 0;}
static bool tu_is_power_of_two(unsigned n){return n&&!(n&(n-1));}
static unsigned tu_log2(unsigned n){unsigned r=0;while(n>>=1)++r;return r;}
static void tud_audio_feedback_params_cb(unsigned f,unsigned a,audio_feedback_params_t*p){(void)f;(void)a;p->method=AUDIO_FEEDBACK_METHOD_FIFO_COUNT;p->sample_freq=16000;}
static unsigned audiod_get_audio_fct_idx(audiod_function_t*p){(void)p;return 0;}
static unsigned tu_fifo_write_n(void*p,const void*d,unsigned n){(void)p;(void)d;++writes;return fifo_full?0:n;}
static bool usbd_edpt_xfer(unsigned r,unsigned ep,void*b,unsigned n,bool isr){(void)r;(void)b;assert(ep==1&&n==34&&isr);++arms;return true;}
static void audiod_fb_fifo_count_update(audiod_function_t*a,unsigned n){(void)a;(void)n;}
static bool tud_audio_rx_done_isr(unsigned r,unsigned n,unsigned f,unsigned e,unsigned a){(void)r;(void)n;(void)f;(void)e;(void)a;++callbacks;return true;}
/* FUNCTIONS */
int main(void){
 audiod_function_t*a=&_audiod_fct[0];a->ep_fb=0x82;a->ep_out=1;a->ep_out_sz=34;
 speed=TUSB_SPEED_HIGH;assert(audiod_fb_params_prepare(0,1));assert(a->feedback.value==(2u<<16));
 speed=TUSB_SPEED_FULL;assert(audiod_fb_params_prepare(0,1));assert(a->feedback.value==(16u<<16));
 assert(audiod_rx_xfer_isr(1,a,0));assert(arms==1&&callbacks==1&&writes==0);
 assert(audiod_rx_xfer_isr(1,a,32));assert(arms==2&&callbacks==2&&writes==1);
 fifo_full=true;assert(audiod_rx_xfer_isr(1,a,32));assert(arms==3&&callbacks==3&&writes==2);
 assert(audiod_rx_xfer_isr(1,a,0));assert(arms==4&&callbacks==4&&writes==2);
 puts("PASS pinned TinyUSB audio: HS/FS nonzero bootstrap feedback; empty/PCM/full FIFO OUT rearming; source/version guard");
}
'''.replace('/* FUNCTIONS */',function('audiod_rx_xfer_isr')+'\n'+function('audiod_fb_params_prepare'))
out=ROOT/'.tools/ouse-usb-audio-check';out.mkdir(exist_ok=True);(out/'check.c').write_text(fixture,encoding='utf-8')
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(ROOT/'.tools/ouse-kernel-check/environment.cmd')],encoding='mbcs').splitlines():
 if '=' in line and not line.startswith('='):
  k,v=line.split('=',1);env[k]=v
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
r=subprocess.run([str(compiler),'/nologo','/std:c11','/experimental:c11atomics','/O2','/MD','/UNDEBUG',str(out/'check.c'),'/Fe'+str(out/'check.exe')],cwd=out,env=env,capture_output=True,text=True,errors='replace');assert r.returncode==0,(r.stdout,r.stderr)
subprocess.run([str(out/'check.exe')],check=True,timeout=10)

