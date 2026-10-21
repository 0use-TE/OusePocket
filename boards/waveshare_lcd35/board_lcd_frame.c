// SPDX-License-Identifier: MIT
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "lcd_frame.h"
#include "lcd_scan.h"
#include <string.h>
#include <inttypes.h>

#define FRAME_BYTES (320u*480u*2u)
#define DMA_ROWS 20u
#define DMA_BYTES (640u*DMA_ROWS) // 320 pixels x 20 physical scanlines; 12,800 bytes.
#define WORKER_STACK_BYTES 4096u

enum slot_owner { SLOT_FREE, SLOT_WRITING, SLOT_READY, SLOT_SENDING };
typedef struct {int x1,y1,x2,y2;bool valid;} damage_t;
static void damage_add(damage_t *d,int x1,int y1,int x2,int y2)
{
    if(!d->valid){*d=(damage_t){x1,y1,x2,y2,true};return;}
    if(x1<d->x1)d->x1=x1;
    if(y1<d->y1)d->y1=y1;
    if(x2>d->x2)d->x2=x2;
    if(y2>d->y2)d->y2=y2;
}
static void damage_merge(damage_t *a,const damage_t *b)
{if(b->valid)damage_add(a,b->x1,b->y1,b->x2,b->y2);}
static struct {
    lv_display_t *display;
    esp_lcd_panel_handle_t panel;
    TaskHandle_t worker;
    uint8_t *compose, *image[2], *dma[2];
    enum slot_owner owner[2];
    damage_t dirty,damage[2];
    bool force_full;
    int pending;
    bool enabled, fault;
    unsigned width, height;
    uint32_t epoch, published, replaced, frames, errors;
    uint32_t max_us, publish_max_us;
    uint32_t full_frames,partial_frames,synced,late;
    uint64_t bytes;
    int64_t report_at;
} frame;
static portMUX_TYPE frame_lock=portMUX_INITIALIZER_UNLOCKED;

esp_err_t __real_esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t,int,int,int,int,const void *);

// Only O(1) ownership changes occur inside this critical section. Copying a
// full PSRAM image, SPI waits and transfers NEVER hold the UI/queue critical section.
static int reserve_publish_slot(void)
{
    int slot=-1;
    portENTER_CRITICAL(&frame_lock);
    if(frame.enabled && !frame.fault){
        if(frame.pending>=0){
            slot=frame.pending;frame.pending=-1;++frame.replaced;
            damage_merge(&frame.dirty,&frame.damage[slot]);
        }else{
            for(int n=0;n<2;n++)if(frame.owner[n]==SLOT_FREE){slot=n;break;}
        }
        if(slot>=0)frame.owner[slot]=SLOT_WRITING;
    }
    portEXIT_CRITICAL(&frame_lock);
    return slot;
}

static void publish_snapshot(void)
{
    int slot=reserve_publish_slot();
    if(slot<0)return;
    frame.damage[slot]=frame.dirty;frame.dirty.valid=false;
    int64_t started=esp_timer_get_time();
    // UI-only compositor is never read by the worker. The reserved outbound
    // slot cannot be taken by the worker until this complete copy is published.
    memcpy(frame.image[slot],frame.compose,FRAME_BYTES);
    uint32_t elapsed=(uint32_t)(esp_timer_get_time()-started);
    portENTER_CRITICAL(&frame_lock);
    // A DMA drain can fail on the other core while this snapshot is copied.
    // Retire this reserved slot rather than publishing into a stopped sender.
    if(frame.fault){
        frame.owner[slot]=SLOT_FREE;
        portEXIT_CRITICAL(&frame_lock);
        return;
    }
    if(frame.force_full){frame.damage[slot]=(damage_t){0,0,(int)frame.width,(int)frame.height,true};frame.force_full=false;}
    frame.owner[slot]=SLOT_READY;frame.pending=slot;++frame.published;
    if(elapsed>frame.publish_max_us)frame.publish_max_us=elapsed;
    portEXIT_CRITICAL(&frame_lock);
    xTaskNotifyGive(frame.worker);
}

static int take_latest(uint32_t *epoch)
{
    int slot=-1;
    portENTER_CRITICAL(&frame_lock);
    if(frame.enabled && !frame.fault && frame.pending>=0){
        slot=frame.pending;frame.pending=-1;frame.owner[slot]=SLOT_SENDING;
        *epoch=frame.epoch;
    }
    portEXIT_CRITICAL(&frame_lock);
    return slot;
}

static void release_slot(int slot)
{
    portENTER_CRITICAL(&frame_lock);
    frame.owner[slot]=SLOT_FREE;
    frame.damage[slot].valid=false;
    portEXIT_CRITICAL(&frame_lock);
}

// Prefer the newest complete image once panel IO is available.
static int promote_latest(int current,uint32_t epoch)
{
    portENTER_CRITICAL(&frame_lock);
    if(frame.enabled && !frame.fault && frame.epoch==epoch && frame.pending>=0){
        int newest=frame.pending;frame.pending=-1;
        damage_merge(&frame.damage[newest],&frame.damage[current]);
        frame.owner[current]=SLOT_FREE;frame.owner[newest]=SLOT_SENDING;
        current=newest;++frame.replaced;
    }
    portEXIT_CRITICAL(&frame_lock);
    return current;
}

static bool epoch_active(uint32_t epoch)
{
    portENTER_CRITICAL(&frame_lock);
    bool active=frame.enabled && !frame.fault && frame.epoch==epoch;
    portEXIT_CRITICAL(&frame_lock);
    return active;
}

static void send_snapshot(int slot,uint32_t epoch)
{
    // Lock order: IO -> brief ownership lock. No code waits for IO while
    // holding frame_lock. Orientation invalidates epochs under the same IO lock.
    ouse_board_lcd_io_lock();
    if(!epoch_active(epoch)){
        ouse_board_lcd_io_unlock();release_slot(slot);return;
    }
    slot=promote_latest(slot,epoch);
    portENTER_CRITICAL(&frame_lock);
    if(frame.force_full){frame.damage[slot]=(damage_t){0,0,(int)frame.width,(int)frame.height,true};frame.force_full=false;}
    portEXIT_CRITICAL(&frame_lock);
    damage_t area=frame.damage[slot];
    bool full=area.valid && (uint32_t)(area.x2-area.x1)*(uint32_t)(area.y2-area.y1)>=320u*480u/4;
    int64_t started=esp_timer_get_time(),deadline=0;
    bool synced=full && ouse_board_lcd_frame_window(&deadline);
    // Prefer a frame published while the background entry wait was asleep.
    if(full)slot=promote_latest(slot,epoch);
    area=frame.damage[slot];
    if(full)area=(damage_t){0,0,(int)frame.width,(int)frame.height,true};
    esp_err_t e=ESP_OK;unsigned bounce=0;
    ouse_board_lcd_worker_dma(true);
    if(full && frame.width==480){
        // A physical scanline is a logical column in landscape. Exactly 24
        // bounded strips, packed directly from the existing logical image.
        for(unsigned x=0;x<480;x+=DMA_ROWS){
            for(unsigned y=0;y<320;y++)memcpy(frame.dma[bounce]+y*DMA_ROWS*2,
                frame.image[slot]+((size_t)y*480+x)*2,DMA_ROWS*2);
            e=__real_esp_lcd_panel_draw_bitmap(frame.panel,(int)x,0,(int)(x+DMA_ROWS),320,frame.dma[bounce]);
            if(e!=ESP_OK)break;
            frame.bytes+=320u*DMA_ROWS*2;bounce^=1;
        }
    }else if(area.valid){
        unsigned width=(unsigned)(area.x2-area.x1);
        unsigned max_rows=DMA_BYTES/(width*2);if(max_rows>DMA_ROWS)max_rows=DMA_ROWS;
        for(int y=area.y1;y<area.y2;y+=(int)max_rows){
            unsigned rows=(unsigned)(area.y2-y);if(rows>max_rows)rows=max_rows;
            for(unsigned row=0;row<rows;row++)memcpy(frame.dma[bounce]+(size_t)row*width*2,
                frame.image[slot]+((size_t)(y+(int)row)*frame.width+area.x1)*2,(size_t)width*2);
            e=__real_esp_lcd_panel_draw_bitmap(frame.panel,area.x1,y,area.x2,y+(int)rows,frame.dma[bounce]);
            if(e!=ESP_OK)break;
            frame.bytes+=(uint64_t)width*rows*2;bounce^=1;
        }
    }
    esp_err_t drained=esp_lcd_panel_io_tx_param(ouse_board_lcd_frame_io(),0x00,NULL,0);
    if(e==ESP_OK)e=drained;
    uint32_t elapsed=(uint32_t)(esp_timer_get_time()-started);
    if(elapsed>frame.max_us)frame.max_us=elapsed;
    ++frame.frames;
    if(full)++frame.full_frames;else ++frame.partial_frames;
    if(synced){++frame.synced;if(esp_timer_get_time()>deadline)++frame.late;}
    if(e!=ESP_OK){
        ++frame.errors;
        portENTER_CRITICAL(&frame_lock);frame.force_full=true;portEXIT_CRITICAL(&frame_lock);
    }
    if(drained==ESP_OK){
        // No worker DMA callback can arrive after this drain. Return this
        // outbound slot only now; neither DMA nor the UI ever share buffers.
        ouse_board_lcd_worker_dma(false);
        release_slot(slot);
    }else{
        // Retain all DMA/source ownership on an unconfirmed drain. UI remains
        // responsive; it must not recycle memory that may still be in flight.
        portENTER_CRITICAL(&frame_lock);
        frame.fault=true;
        if(frame.pending>=0){frame.owner[frame.pending]=SLOT_FREE;frame.pending=-1;}
        portEXIT_CRITICAL(&frame_lock);
        ESP_LOGE("OuseLCDFrame","DMA drain failed; sender stopped, buffers retained rc=%d",drained);
    }
    ouse_board_lcd_io_unlock();
    int64_t now=esp_timer_get_time();
    if(now>=frame.report_at || e!=ESP_OK){
        uint32_t published,replaced,publish_max;
        portENTER_CRITICAL(&frame_lock);
        published=frame.published;replaced=frame.replaced;publish_max=frame.publish_max_us;
        portEXIT_CRITICAL(&frame_lock);
        ESP_LOGI("OuseLCDFrame","async frames=%" PRIu32 " tx_us=%" PRIu32 " max_us=%" PRIu32
                 " published=%" PRIu32 " replaced=%" PRIu32
                 " ui_copy_max_us=%" PRIu32 " full=%" PRIu32 " partial=%" PRIu32 " synced=%" PRIu32 " late=%" PRIu32 " bytes=%" PRIu64 " errors=%" PRIu32 " rc=%d",
                 frame.frames,elapsed,frame.max_us,published,replaced,publish_max,frame.full_frames,frame.partial_frames,frame.synced,frame.late,frame.bytes,frame.errors,e);
        frame.report_at=now+5000000;
    }
}

static void frame_worker(void *argument)
{
    (void)argument;
    for(;;){
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        for(;;){
            uint32_t epoch;
            int slot=take_latest(&epoch);
            if(slot<0)break;
            send_snapshot(slot,epoch);
        }
    }
}

// The adapter still handles stride compaction and RGB565 byte order. The UI
// only stages/copies pixels, marks its own flush ready, and notifies the worker.
esp_err_t __wrap_esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel,int x1,int y1,int x2,int y2,const void *pixels)
{
    lv_display_t *display=frame.display;
    if(!display || panel!=frame.panel || lv_display_get_horizontal_resolution(display)!=(int)frame.width ||
       lv_display_get_vertical_resolution(display)!=(int)frame.height)
        return __real_esp_lcd_panel_draw_bitmap(panel,x1,y1,x2,y2,pixels);
    if(x1<0 || y1<0 || x2>(int)frame.width || y2>(int)frame.height || x1>=x2 || y1>=y2 || !pixels)return ESP_ERR_INVALID_ARG;
    const uint8_t *src=pixels;
    for(int row=y1;row<y2;row++){
        memcpy(frame.compose+((size_t)row*frame.width+x1)*2,src,(size_t)(x2-x1)*2);
        src+=(x2-x1)*2;
    }
    damage_add(&frame.dirty,x1,y1,x2,y2);
    if(lv_display_flush_is_last(display))publish_snapshot();
    // Only CPU staging owns these UI buffers. Background DMA completion is
    // filtered in the IO callback proxy; the sender never calls LVGL.
    lv_display_flush_ready(display);
    return ESP_OK;
}

void ouse_board_lcd_frame_orientation_begin(void)
{
    if(!frame.display)return;
    ouse_board_lcd_io_lock(); // rare orientation command waits for prior DMA
    portENTER_CRITICAL(&frame_lock);
    frame.enabled=false;++frame.epoch;
    if(frame.pending>=0){frame.owner[frame.pending]=SLOT_FREE;frame.pending=-1;}
    portEXIT_CRITICAL(&frame_lock);
    ouse_board_lcd_sync_invalidate();
}

void ouse_board_lcd_frame_orientation_end(bool landscape)
{
    if(!frame.display)return;
    memset(frame.compose,0,FRAME_BYTES);
    portENTER_CRITICAL(&frame_lock);
    frame.width=landscape?480:320;
    frame.height=landscape?320:480;
    frame.enabled=true;frame.force_full=true;frame.dirty.valid=false;
    portEXIT_CRITICAL(&frame_lock);
    ouse_board_lcd_io_unlock();
    // Called on the UI thread, including a failed orientation change. Ensure
    // the new compositor gets a complete repaint instead of stale old pixels.
    lv_obj_invalidate(lv_display_get_screen_active(frame.display));
}

static void free_frame_buffers(void)
{
    heap_caps_free(frame.compose);frame.compose=NULL;
    for(unsigned n=0;n<2;n++){
        heap_caps_free(frame.image[n]);frame.image[n]=NULL;
        heap_caps_free(frame.dma[n]);frame.dma[n]=NULL;
    }
}

void ouse_board_lcd_frame_init(lv_display_t *display)
{
    if(frame.display || !display || lv_display_get_color_format(display)!=LV_COLOR_FORMAT_RGB565 ||
       !ouse_board_lcd_frame_io() || !ouse_board_lcd_io_lock_init())return;
    int width=lv_display_get_horizontal_resolution(display),height=lv_display_get_vertical_resolution(display);
    if(!((width==320 && height==480) || (width==480 && height==320)))return;
    if(esp_lcd_panel_io_tx_param(ouse_board_lcd_frame_io(),0x00,NULL,0)!=ESP_OK)return;
    frame.compose=heap_caps_calloc(1,FRAME_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    for(unsigned n=0;n<2;n++){
        frame.image[n]=heap_caps_calloc(1,FRAME_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        frame.dma[n]=heap_caps_malloc(DMA_BYTES,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT);
    }
    if(!frame.compose || !frame.image[0] || !frame.image[1] || !frame.dma[0] || !frame.dma[1]){
        free_frame_buffers();
        ESP_LOGW("OuseLCDFrame","async allocation failed; original BSP flush retained");return;
    }
    frame.panel=bsp_display_get_panel_handle();frame.pending=-1;
    frame.width=(unsigned)lv_display_get_horizontal_resolution(display);
    frame.height=(unsigned)lv_display_get_vertical_resolution(display);
    frame.enabled=true;frame.force_full=true;
    // Priority 7 only runs short copies/command setup; DMA waits
    // block it. UI remains at priority 6 and keeps its original drawing buffers.
    if(xTaskCreatePinnedToCore(frame_worker,"ouse_lcd_tx",WORKER_STACK_BYTES,NULL,7,&frame.worker,0)!=pdPASS){
        free_frame_buffers();
        ESP_LOGW("OuseLCDFrame","sender task allocation failed; original BSP flush retained");return;
    }
    frame.display=display;
    lv_obj_invalidate(lv_display_get_screen_active(display));
    ESP_LOGI("OuseLCDFrame","bounded async rectangle sender enabled: staging PSRAM=%u DMA=%u stack=%u bytes; latest pending frame only",
             FRAME_BYTES*3,DMA_BYTES*2,WORKER_STACK_BYTES);
}
