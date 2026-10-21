"""Run the actual upstream diff engine and production scan scheduler on a timed SPI model."""
from pathlib import Path
import os, subprocess, shutil

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/lcd-scan-sync-check';OUT.mkdir(parents=True,exist_ok=True)
# Reuse the same bounded ESP-IDF declarations as the ownership/fallback tests.
subprocess.run([os.sys.executable,str(ROOT/'tools/test-lcd-async.py')],check=True,stdout=subprocess.DEVNULL)
for p in (ROOT/'.tools/lcd-async-check').rglob('*.h'):
    target=OUT/p.relative_to(ROOT/'.tools/lcd-async-check');target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
BOARD=ROOT/'boards/waveshare_lcd35'
source=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/task.h"
static int64_t clock_us=1000000;
static unsigned physical_line_ns=36000;
static bool landscape,corrupt,read_failure,draw_failure,slow;
static int draw_count,read_count;
static uint16_t panel_pixels[320*480],image[320*480],dma[2][9600];
static const void *active;
static std::vector<uint8_t> active_copy;
int64_t esp_timer_get_time(void){return clock_us;}
void esp_rom_delay_us(unsigned us){clock_us+=us;}
void vTaskDelay(TickType_t ticks){clock_us+=ticks*1000;}
void test_log(const char *,...){}
void *heap_caps_malloc(size_t n,unsigned){return malloc(n);}
void heap_caps_free(void *p){free(p);}
esp_lcd_panel_io_handle_t ouse_board_lcd_frame_io(void){return (void*)1;}
esp_lcd_panel_handle_t bsp_display_get_panel_handle(void){return (void*)2;}
static void drain(){if(active){assert(!memcmp(active,active_copy.data(),active_copy.size()));clock_us+=(int64_t)active_copy.size()/(slow?5:10)+20;active=nullptr;}}
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t,int cmd,const void *p,size_t n){drain();if(cmd==0xb1){assert(n==2 && ((const uint8_t*)p)[0]==0x90 && ((const uint8_t*)p)[1]==0x1f);physical_line_ns=41000;}return ESP_OK;}
bool ouse_board_lcd_scan_raw(int cmd,uint8_t *out,unsigned n){
    drain();++read_count;memset(out,0,n);
    if(read_failure)return false;
    if(cmd==0xd3){assert(n==4);out[1]=0x3b;out[2]=0xcb;return true;}
    assert(cmd==0x45 && n==3);
    unsigned line=corrupt?550:(unsigned)((clock_us*1000/physical_line_ns)%485);
    unsigned bits=line<<7;out[0]=(uint8_t)(bits>>16);out[1]=(uint8_t)(bits>>8);out[2]=(uint8_t)bits;
    // Timestamp at the midpoint of the short transaction.
    clock_us+=2;return true;
}
esp_err_t __real_esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t,int x1,int y1,int x2,int y2,const void *p){
    drain();++draw_count;
    if(draw_failure){draw_failure=false;return ESP_FAIL;}
    assert(x1>=0 && y1>=0 && x2<=(landscape?480:320) && y2<=(landscape?320:480));
    size_t bytes=(size_t)(x2-x1)*(y2-y1)*2;assert(bytes>0 && bytes<=12800);
    active=p;active_copy.assign((const uint8_t*)p,(const uint8_t*)p+bytes);
    const uint16_t *pixels=(const uint16_t*)p;
    for(int y=y1;y<y2;y++)for(int x=x1;x<x2;x++){
        int cx=landscape?319-y:x,cy=landscape?x:y;
        panel_pixels[cy*320+cx]=*pixels++;
    }
    clock_us+=10;return ESP_OK;
}
'''
source+='#include "'+(BOARD/'board_lcd_diff.cpp').as_posix()+'"\n'
source+='#include "'+(BOARD/'board_lcd_sync.c').as_posix()+'"\n'
source+=r'''
static esp_err_t send(){uint8_t *buffers[2]={(uint8_t*)dma[0],(uint8_t*)dma[1]};return ouse_board_lcd_sync_send(image,landscape,buffers);}
static void check(){
    assert(!active);
    for(int y=0;y<480;y++)for(int x=0;x<320;x++){
        uint16_t expected=landscape?image[(319-x)*480+y]:image[y*320+x];
        assert(panel_pixels[y*320+x]==expected);
    }
}
int main(){
    setvbuf(stdout,nullptr,_IONBF,0);
    for(unsigned i=0;i<320*480;i++)image[i]=(uint16_t)(i*13u+7u);
    assert(send()==ESP_OK);check();assert(sync_state.calibrated && sync_state.lines>=484 && sync_state.lines<=486);
    assert(sync_state.line_ns>=40900 && sync_state.line_ns<=41100 && sync_state.refresh_set);
    assert(sync_state.synced==1 && sync_state.late==0);
    puts("PASS physical scan calibration and full-frame trailing upload across two refresh periods");
    int old_draws=draw_count,old_reads=read_count;
    assert(send()==ESP_OK);assert(draw_count==old_draws && read_count==old_reads && sync_state.empty==1);
    image[100*320+40]^=0x1111;image[101*320+240]^=0x2222;
    unsigned before=sync_state.pixels;assert(send()==ESP_OK);check();assert(sync_state.pixels-before<320);
    puts("PASS identical frame performs no SPI; exact sparse diff uploads only changed spans");
    landscape=true;ouse_board_lcd_sync_invalidate();assert(send()==ESP_OK);check();
    image[7*480+171]^=0x73;image[250*480+38]^=0x44;
    assert(send()==ESP_OK);check();assert(sync_state.late==0);
    puts("PASS landscape transpose, full and partial canonical runs, correct physical scan ordering");
    // Highly fragmented image exhausts the upstream 8KiB log. Its fallback
    // sends the entire remaining region; it must never omit changed pixels.
    landscape=false;ouse_board_lcd_sync_invalidate();assert(send()==ESP_OK);check();
    for(unsigned i=0;i<320*480;i+=16)image[i]^=0xffff;
    assert(send()==ESP_OK);check();assert(diff->statsOverflowRatio()>0);
    puts("PASS upstream diff-log overflow conservatively repaints the remainder");
    unsigned bad=sync_state.invalid;corrupt=true;image[50]^=1;
    assert(send()==ESP_OK);check();assert(sync_state.invalid>=bad+3 && sync_state.unsynced>0 && !sync_state.calibrated);
    corrupt=false;clock_us+=2100000;image[60]^=1;
    assert(send()==ESP_OK);check();assert(sync_state.calibrated);
    puts("PASS invalid feedback is rejected, unsynced transmission counted, recalibration recovers");
    draw_failure=true;image[70]^=1;assert(send()==ESP_FAIL);assert(!valid);
    assert(send()==ESP_OK);check();
    puts("PASS failed DMA invalidates mirror and forces complete recovery repaint");
    unsigned prior_late=sync_state.late;slow=true;ouse_board_lcd_sync_invalidate();
    assert(send()==ESP_OK);check();assert(sync_state.late>prior_late);slow=false;
    puts("PASS slow actual DMA completion is counted as a missed scan deadline");
    // Reader deadlines are explicit: a CPU/SPI overrun must be observable,
    // never silently reported as a guaranteed tear-free frame.
    sync_state.line_ns=1000;sync_state.lines=485;sync_state.calibrated=true;
    read_failure=true;image[80]^=1;assert(send()==ESP_OK);check();
    assert(!sync_state.calibrated && sync_state.unsynced>=2);
    puts("PASS unavailable register reads fail open without deadlock or stale pixels");
    return 0;
}
'''
(OUT/'test.cpp').write_text(source,encoding='utf-8')
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(ROOT/'.tools/lcd-async-check/env.cmd')],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        k,v=line.split('=',1);env[k]=v
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
subprocess.run([str(compiler),'/nologo','/EHsc','/std:c++17','/W3','/I'+str(OUT),'/I'+str(BOARD/'t4diff'),str(OUT/'test.cpp'),'/Fe:'+str(OUT/'test.exe')],cwd=OUT,env=env,check=True)
subprocess.run([str(OUT/'test.exe')],check=True,timeout=30)
print('PASS production upstream engine and scan/DMA scheduler (simulation, not optical verification)')
