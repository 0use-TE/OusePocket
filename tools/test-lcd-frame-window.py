"""Test production board timing: no pixel diff, no busy wait, bounded reads/sleep."""
from pathlib import Path
import os,subprocess,shutil
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'.tools/lcd-frame-window-check';OUT.mkdir(parents=True,exist_ok=True)
subprocess.run([os.sys.executable,str(ROOT/'tools/test-lcd-async.py')],check=True,stdout=subprocess.DEVNULL)
for p in (ROOT/'.tools/lcd-async-check').rglob('*.h'):
 target=OUT/p.relative_to(ROOT/'.tools/lcd-async-check');target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
source=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "freertos/task.h"
#include "esp_lcd_panel_io.h"
static int64_t sim_clock=1000000;
static unsigned calls,sleeps,last_sleep;
static bool bad;
int64_t esp_timer_get_time(void){return sim_clock;}
void vTaskDelay(TickType_t ticks){sim_clock+=ticks*1000;++sleeps;last_sleep=ticks;}
void test_log(const char *format,...){(void)format;}
esp_lcd_panel_io_handle_t ouse_board_lcd_frame_io(void){return (void*)1;}
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io,int cmd,const void *p,size_t n){
 (void)io;if(cmd==0xb1)assert(n==2 && ((const unsigned char*)p)[0]==0x90 && ((const unsigned char*)p)[1]==0x1f);return ESP_OK;
}
bool ouse_board_lcd_scan_raw(int cmd,uint8_t *p,unsigned n){
 ++calls;memset(p,0,n);if(cmd==0xd3){p[1]=0x3b;p[2]=0xcb;return true;}
 assert(cmd==0x45 && n==3);
 unsigned line=bad?550:(unsigned)(sim_clock*1000/41000)%485;
 unsigned bits=line<<7;p[0]=(uint8_t)(bits>>16);p[1]=(uint8_t)(bits>>8);p[2]=(uint8_t)bits;sim_clock+=2;return true;
}
'''
source+='#include "'+(ROOT/'boards/waveshare_lcd35/board_lcd_sync.c').as_posix()+'"\n'
source+=r'''
int main(void){
 int64_t deadline=0;assert(ouse_board_lcd_frame_window(&deadline));
 assert(sync_state.calibrated && sync_state.refresh_set && sync_state.lines>=484 && sync_state.lines<=486);
 for(unsigned line=0;line<485;line+=7){
  sim_clock=((sim_clock/(485*41))+1)*(485*41)+line*41;
  unsigned c=calls,s=sleeps;int64_t before=sim_clock;
  assert(ouse_board_lcd_frame_window(&deadline));
  assert(calls-c<=4 && sleeps-s<=1 && sim_clock-before<=22000 && deadline-sim_clock>=32000);
  if(sleeps>s)assert(last_sleep<=22);
 }
 puts("PASS all start phases: at most four reads, one background sleep <=22ms, no polling loop");
 bad=true;unsigned c=calls,s=sleeps;
 assert(!ouse_board_lcd_frame_window(&deadline));assert(calls-c<=6 && sleeps==s);
 assert(!sync_state.calibrated);
 puts("PASS corrupt feedback has a bounded rejection path, without sleeping or spinning");
 bad=false;sim_clock+=2100000;assert(ouse_board_lcd_frame_window(&deadline));assert(sync_state.calibrated);
 puts("PASS calibration recovers after the retry interval");return 0;
}
'''
(OUT/'test.c').write_text(source,encoding='utf-8')
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(ROOT/'.tools/lcd-async-check/env.cmd')],encoding='mbcs').splitlines():
 if '=' in line and not line.startswith('='):
  k,v=line.split('=',1);env[k]=v
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
subprocess.run([str(compiler),'/nologo','/std:c11','/W4','/WX','/wd4127','/I'+str(OUT),str(OUT/'test.c'),'/Fe:'+str(OUT/'test.exe')],cwd=OUT,env=env,check=True)
subprocess.run([str(OUT/'test.exe')],check=True,timeout=10)
