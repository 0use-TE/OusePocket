// SPDX-License-Identifier: MIT
// Board-specific, bounded frame-entry timing. No pixel diff or busy waiting.
#include "lcd_scan.h"
#include "lcd_frame.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static struct {
 bool calibrated,refresh_set;
 unsigned lines,line_ns;
 int64_t retry_at;
 uint32_t invalid;
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
        // Configure the measured board near 50Hz using ST7796 FRMCTR1
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
    sync_state.lines=total;sync_state.line_ns=ns;
    ESP_LOGI("OuseLCDSync","ST7796 calibrated lines=%u line_ns=%u period_us=%u bounded entry",total,ns,total*ns/1000);
    return true;
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

void ouse_board_lcd_sync_invalidate(void){}

bool ouse_board_lcd_frame_window(int64_t *deadline)
{
 int64_t now=esp_timer_get_time();
 if(!sync_state.calibrated && now>=sync_state.retry_at){
  sync_state.calibrated=calibrate();sync_state.retry_at=esp_timer_get_time()+2000000;
 }
 if(!sync_state.calibrated)return false;
 unsigned line;int64_t at;
 if(!sample(&line,&at)){sync_state.calibrated=false;return false;}
 int64_t period=(int64_t)sync_state.lines*sync_state.line_ns/1000;
 int64_t zero=at-(int64_t)line*sync_state.line_ns/1000;
 unsigned start=24+sync_state.lines-480;
 if(line<start || line>100){
  int64_t target=zero+(int64_t)start*sync_state.line_ns/1000;
  if(line>100)target+=period;
  int64_t remaining=target-esp_timer_get_time();
  if(remaining>0)vTaskDelay(pdMS_TO_TICKS((unsigned)((remaining+999)/1000)+1));
  // Exactly one sleep and one verification pair; no polling/retry-until-window.
  if(!sample(&line,&at))return false;
  zero=at-(int64_t)line*sync_state.line_ns/1000;
 }
 if(line<start || line>120)return false;
 *deadline=zero+2*period-(int64_t)4*sync_state.line_ns/1000;
 return true;
}
