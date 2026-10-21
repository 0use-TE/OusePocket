// SPDX-License-Identifier: LGPL-2.1-or-later
// ESP-IDF task/DMA port of ILI9341_T4's _updateAsync / readDiff scheduling.
// Upstream copyright (c) 2020 Arvind Singh. See t4diff/README.md and LICENSE.
#include "lcd_diff.h"
#include "lcd_scan.h"
#include "lcd_frame.h"
#include "esp_lcd_panel_ops.h"
#include "bsp/esp-bsp.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <inttypes.h>

static struct {
    bool calibrated,allocated,refresh_set;
    unsigned lines,line_ns;
    int64_t retry_at,previous_zero,report_at;
    uint32_t frames,synced,unsynced,late,invalid,pixels,empty;
} sync_state;

static bool read_line(unsigned *line,int64_t *at)
{
    uint8_t raw[3]={0};int64_t a=esp_timer_get_time();
    bool ok=ouse_board_lcd_scan_raw(0x45,raw,3);
    *at=(a+esp_timer_get_time())/2;
    *line=((unsigned)raw[0]<<9 | (unsigned)raw[1]<<1 | raw[2]>>7)&0xffff;
    return ok && *line<560;
}

// Calibrate physical GSCAN counters including porch lines. Do not assume
// 480 equals the controller's full cycle, or accept/filter incoherent samples.
static bool calibrate(void)
{
    uint8_t id[4]={0};
    if(!ouse_board_lcd_scan_raw(0xd3,id,4) ||
       (((unsigned)id[1]<<9 | (unsigned)id[2]<<1 | id[3]>>7)&0xffff)!=0x7796)return false;
    if(!sync_state.refresh_set){
        // Equivalent of upstream setRefreshRate(~50), using ST7796 FRMCTR1
        // (datasheet 9.3.2), NOT the ILI9341 divider/register formula.
        // FRS=9, DIVA=0, RTNA=31. Measure the actual result below.
        const uint8_t unlock[]={0xc3,0x96},lock[]={0x3c,0x69},rate[]={0x90,0x1f};
        esp_lcd_panel_io_handle_t io=ouse_board_lcd_frame_io();
        esp_err_t e=esp_lcd_panel_io_tx_param(io,0xf0,&unlock[0],1);
        if(e==ESP_OK)e=esp_lcd_panel_io_tx_param(io,0xf0,&unlock[1],1);
        if(e==ESP_OK)e=esp_lcd_panel_io_tx_param(io,0xb1,rate,sizeof(rate));
        esp_err_t l1=esp_lcd_panel_io_tx_param(io,0xf0,&lock[0],1);
        esp_err_t l2=esp_lcd_panel_io_tx_param(io,0xf0,&lock[1],1);
        if(e!=ESP_OK || l1!=ESP_OK || l2!=ESP_OK)return false;
        sync_state.refresh_set=true;vTaskDelay(pdMS_TO_TICKS(40));
    }
    unsigned line[96];int64_t at[96];uint64_t dt_sum=0;unsigned dy_sum=0;
    for(unsigned n=0;n<96;n++){
        if(!read_line(&line[n],&at[n]))return false;
        if(n){
            int dy=(int)line[n]-(int)line[n-1];int64_t dt=at[n]-at[n-1];
            if(dt<=0 || dt>2500)return false;
            if(dy<0){if(line[n-1]<420 || line[n]>100)return false;}
            else{
                if(dy==0 || dy>100)return false;
                dt_sum+=(uint64_t)dt;dy_sum+=(unsigned)dy;
            }
        }
        vTaskDelay(1);
    }
    if(!dy_sum)return false;
    unsigned ns=(unsigned)(dt_sum*1000/dy_sum);
    if(ns<28000 || ns>50000)return false;
    unsigned total=0,wraps=0,min=1000,max=0;
    for(unsigned n=1;n<96;n++)if(line[n]<line[n-1]){
        unsigned count=line[n-1]-line[n]+(unsigned)(((at[n]-at[n-1])*1000+ns/2)/ns);
        if(count<min)min=count;
        if(count>max)max=count;
        total+=count;++wraps;
    }
    if(wraps<3 || max-min>3)return false;
    total=(total+wraps/2)/wraps;
    if(total<480 || total>540)return false;
    // Validate every adjacency against the inferred model, including wraps.
    for(unsigned n=1;n<96;n++){
        unsigned actual=(line[n]+total-line[n-1])%total;
        int predicted=(int)(((at[n]-at[n-1])*1000+ns/2)/ns);
        int error=(int)actual-predicted;
        if(error < -3 || error>3)return false;
    }
    sync_state.lines=total;sync_state.line_ns=ns;sync_state.previous_zero=0;
    ESP_LOGI("OuseLCDSync","ST7796 calibrated lines=%u line_ns=%u period_us=%u spacing=2",total,ns,total*ns/1000);
    return true;
}

static void wait_until(int64_t target)
{
    for(;;){
        int64_t left=target-esp_timer_get_time();if(left<=0)return;
        if(left>1500)vTaskDelay(1);
        else esp_rom_delay_us((unsigned)(left>100?100:left));
    }
}

static bool sample(unsigned *line,int64_t *at)
{
    for(unsigned retry=0;retry<3;retry++){
        unsigned a,b;int64_t ta,tb;
        if(read_line(&a,&ta) && read_line(&b,&tb) && a<sync_state.lines && b<sync_state.lines){
            unsigned delta=(b+sync_state.lines-a)%sync_state.lines;
            unsigned bound=(unsigned)((tb-ta)*1000/sync_state.line_ns)+3;
            if(delta<=bound){*line=b;*at=tb;return true;}
        }
        ++sync_state.invalid;
    }
    return false;
}

void ouse_board_lcd_sync_invalidate(void)
{ouse_lcd_diff_invalidate();sync_state.previous_zero=0;}

esp_err_t __real_esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t,int,int,int,int,const void *);

// Canonical portrait scan order for both orientations. In landscape a physical
// row becomes a logical column; pack a narrow vertical rectangle for MADCTL MV.
// No orientation-register changes occur during a frame.
static esp_err_t send_run(bool landscape,int x,int y,int pixels,uint8_t *dma)
{
    const uint16_t *src=ouse_lcd_diff_pixels();
    int rows=(x==0 && pixels>=320)?pixels/320:1;
    if(!landscape){
        memcpy(dma,src+y*320+x,(size_t)pixels*2);
        return __real_esp_lcd_panel_draw_bitmap(bsp_display_get_panel_handle(),x,y,x+(rows>1?320:pixels),y+rows,dma);
    }
    uint16_t *out=(uint16_t*)dma;
    int span=rows>1?320:pixels;
    for(int cx=x+span-1;cx>=x;cx--)for(int row=0;row<rows;row++)*out++=src[(y+row)*320+cx];
    return __real_esp_lcd_panel_draw_bitmap(bsp_display_get_panel_handle(),y,320-x-span,y+rows,320-x,dma);
}

esp_err_t ouse_board_lcd_sync_send(const void *image,bool landscape,uint8_t *dma[2])
{
    if(!sync_state.allocated){
        if(!ouse_lcd_diff_init())return ESP_ERR_NOT_SUPPORTED;
        sync_state.allocated=true;
    }
    int first=ouse_lcd_diff_prepare(image,landscape);
    if(first<0){++sync_state.empty;return ESP_OK;}
    int64_t now=esp_timer_get_time();
    if(!sync_state.calibrated && now>=sync_state.retry_at){
        sync_state.calibrated=calibrate();sync_state.retry_at=esp_timer_get_time()+2000000;
        if(!sync_state.calibrated)ESP_LOGW("OuseLCDSync","scan calibration rejected; differential DMA continues without sync");
    }
    unsigned line=0;int64_t anchor=0,zero=0;
    bool synced=sync_state.calibrated && sample(&line,&anchor);
    if(sync_state.calibrated && !synced){sync_state.calibrated=false;sync_state.retry_at=now+2000000;}
    const int64_t period=(int64_t)sync_state.lines*sync_state.line_ns/1000;
    if(synced){
        zero=anchor-(int64_t)line*sync_state.line_ns/1000;
        int64_t target=zero+(int64_t)(first+sync_state.lines-480)*sync_state.line_ns/1000;
        now=esp_timer_get_time();
        // The upstream resync path chooses a known scanline and spacing=2.
        // Missed starts move to the next cycle; do not spin on repeated reads.
        while(target<now || (sync_state.previous_zero && zero<sync_state.previous_zero+2*period-period/20)){
            zero+=period;target+=period;
        }
        wait_until(target);sync_state.previous_zero=zero;
    }
    bool late=false;int64_t previous_deadline=0;unsigned bounce=0;esp_err_t e=ESP_OK;
    for(;;){
        now=esp_timer_get_time();
        int64_t physical=synced?(now-zero)*1000/sync_state.line_ns:0;
        int visible=synced?(int)(physical%sync_state.lines)-(int)(sync_state.lines-480):0;
        if(visible<0)visible=0;
        int scan=synced?(int)((physical/sync_state.lines)*480)+visible:960;
        int x,y,len;int r=ouse_lcd_diff_next(scan,&x,&y,&len);
        if(r<0)break;
        if(r>0){
            // Like upstream's DMA-complete -> timer transition: finish the
            // preceding run before waiting. Its deadline must be checked at
            // completion, not after an intentional gap to a later dirty row.
            if(previous_deadline){
                e=esp_lcd_panel_io_tx_param(ouse_board_lcd_frame_io(),0x00,NULL,0);
                if(esp_timer_get_time()>=previous_deadline)late=true;
                previous_deadline=0;
                if(e!=ESP_OK)break;
            }
            wait_until(zero+(int64_t)(r+sync_state.lines-480+1)*sync_state.line_ns/1000);continue;
        }
        if(len<=0){e=ESP_FAIL;break;}
        while(len>0){
            // Upstream diffs can end part way through a row. Rectangular panel
            // writes must split the full rows from that final partial row.
            int count=(x==0 && len>=320)?(len/320)*320:len;
            now=esp_timer_get_time();
            if(synced){
                // Conservative chunk-completion deadline: even the first row
                // of a landscape strip must complete before its next scan.
                int64_t deadline=zero+(int64_t)(y+sync_state.lines-4)*sync_state.line_ns/1000;
                int64_t budget=(int64_t)count*2/10+100; // 80MHz payload + setup
                if(now+budget>=deadline)late=true;
            }
            e=send_run(landscape,x,y,count,dma[bounce]);if(e!=ESP_OK)break;
            if(synced){
                now=esp_timer_get_time();
                if(previous_deadline && now>=previous_deadline)late=true;
                previous_deadline=zero+(int64_t)(y+sync_state.lines-4)*sync_state.line_ns/1000;
                if(now+(int64_t)count*2/10+100>=previous_deadline)late=true;
            }
            bounce^=1;sync_state.pixels+=(uint32_t)count;
            // CASET of the next run drains the preceding DMA, so ping-pong
            // storage is never reused before completion. Final drain is below.
            len-=count;y+=count/320;x=0;
        }
        if(e!=ESP_OK)break;
    }
    esp_err_t drained=esp_lcd_panel_io_tx_param(ouse_board_lcd_frame_io(),0x00,NULL,0);
    if(synced && previous_deadline && esp_timer_get_time()>=previous_deadline)late=true;
    if(e==ESP_OK)e=drained;
    if(e!=ESP_OK)ouse_lcd_diff_invalidate();
    ++sync_state.frames;
    if(synced)++sync_state.synced;else ++sync_state.unsynced;
    if(late)++sync_state.late;
    now=esp_timer_get_time();
    if(now>=sync_state.report_at || e!=ESP_OK){
        ESP_LOGI("OuseLCDSync","frames=%" PRIu32 " synced=%" PRIu32 " unsynced=%" PRIu32 " late=%" PRIu32 " invalid=%" PRIu32 " pixels=%" PRIu32 " empty=%" PRIu32 " rc=%d",
                 sync_state.frames,sync_state.synced,sync_state.unsynced,sync_state.late,sync_state.invalid,sync_state.pixels,sync_state.empty,e);
        sync_state.report_at=now+5000000;
    }
    return e;
}
