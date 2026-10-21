"""Exercise production async LCD staging/ownership and SPI callback isolation without hardware."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.tools/lcd-async-check'
OUT.mkdir(parents=True, exist_ok=True)

def put(name, content):
    path = OUT / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding='utf-8')

put('lvgl.h', r'''#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct { int width,height,format,last,ready; } lv_display_t;
#define LV_COLOR_FORMAT_RGB565 1
int lv_display_get_horizontal_resolution(lv_display_t *);
int lv_display_get_vertical_resolution(lv_display_t *);
int lv_display_get_color_format(lv_display_t *);
bool lv_display_flush_is_last(lv_display_t *);
void lv_display_flush_ready(lv_display_t *);
void *lv_display_get_screen_active(lv_display_t *);
void lv_obj_invalidate(void *);
''')
put('esp_lcd_panel_io.h', r'''#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -2
#define ESP_ERR_NO_MEM -3
typedef void *esp_lcd_panel_io_handle_t;
typedef struct { int unused; } esp_lcd_panel_io_event_data_t;
typedef bool (*esp_lcd_panel_io_color_trans_done_cb_t)(esp_lcd_panel_io_handle_t,esp_lcd_panel_io_event_data_t *,void *);
typedef struct { esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done; } esp_lcd_panel_io_callbacks_t;
typedef int esp_lcd_spi_bus_handle_t;
typedef struct { int cs_gpio_num; esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done;void *user_ctx; } esp_lcd_panel_io_spi_config_t;
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t,int,const void *,size_t);
esp_err_t esp_lcd_panel_io_rx_param(esp_lcd_panel_io_handle_t,int,void *,size_t);
esp_err_t esp_lcd_panel_io_tx_color(esp_lcd_panel_io_handle_t,int,const void *,size_t);
''')
put('esp_lcd_panel_ops.h', '#pragma once\n#include "esp_lcd_panel_io.h"\ntypedef void *esp_lcd_panel_handle_t;\n')
put('bsp/display.h', '#pragma once\n#include "lvgl.h"\n')
put('bsp/esp-bsp.h', '#pragma once\n#include "esp_lcd_panel_ops.h"\nesp_lcd_panel_handle_t bsp_display_get_panel_handle(void);\n')
put('esp_heap_caps.h', r'''#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_DMA 4
#define MALLOC_CAP_INTERNAL 8
void *heap_caps_calloc(size_t,size_t,unsigned);
void *heap_caps_malloc(size_t,unsigned);
void heap_caps_free(void *);
''')
put('esp_timer.h', '#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
put('esp_rom_sys.h', '#pragma once\nvoid esp_rom_delay_us(unsigned);\n')
put('esp_attr.h', '#define IRAM_ATTR\n')
put('esp_log.h', '#pragma once\nvoid test_log(const char *,...);\n#define ESP_LOGI(...) test_log(__VA_ARGS__)\n#define ESP_LOGW(...) test_log(__VA_ARGS__)\n#define ESP_LOGE(...) test_log(__VA_ARGS__)\n')
# Deterministic interleavings test ownership; target atomics are checked by the real firmware build.
put('stdatomic.h', '#pragma once\n#include <stdbool.h>\ntypedef bool atomic_bool;\n#define memory_order_release 0\n#define memory_order_acquire 0\n#define atomic_store_explicit(p,v,o) (*(p)=(v))\n#define atomic_load_explicit(p,o) (*(p))\n')
put('freertos/FreeRTOS.h', r'''#pragma once
#include <stdint.h>
typedef unsigned TickType_t;
typedef int BaseType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portMAX_DELAY (~0u)
#define pdTRUE 1
#define pdPASS 1
void enter_critical(void);
void exit_critical(void);
#define portENTER_CRITICAL(p) ((void)(p),enter_critical())
#define portEXIT_CRITICAL(p) ((void)(p),exit_critical())
#define pdMS_TO_TICKS(n) (n)
''')
put('freertos/task.h', r'''#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
void vTaskDelay(TickType_t);
void xTaskNotifyGive(TaskHandle_t);
unsigned ulTaskNotifyTake(int,unsigned);
int xTaskCreatePinnedToCore(void (*)(void *),const char *,unsigned,void *,int,TaskHandle_t *,int);
''')
put('freertos/semphr.h', r'''#pragma once
#include "FreeRTOS.h"
typedef int *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
int xSemaphoreTakeRecursive(SemaphoreHandle_t,unsigned);
int xSemaphoreGiveRecursive(SemaphoreHandle_t);
''')
put('driver/spi_master.h', r'''#pragma once
#include "esp_lcd_panel_io.h"
typedef int spi_host_device_t;
typedef int spi_dma_chan_t;
typedef void *spi_device_handle_t;
typedef struct { int mosi_io_num,sclk_io_num,miso_io_num; } spi_bus_config_t;
typedef struct { int spics_io_num,sample_point; } spi_device_interface_config_t;
typedef struct { uint32_t override_freq_hz; } spi_transaction_t;
#define SPI2_HOST 2
#define SPI_SAMPLING_POINT_PHASE_1 1
''')
source = '#include "' + (ROOT/'boards/waveshare_lcd35/board_lcd_frame_io.c').as_posix() + '"\n'
source += '#include "' + (ROOT/'boards/waveshare_lcd35/board_lcd_frame.c').as_posix() + '"\n'
source += r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static lv_display_t display={320,480,LV_COLOR_FORMAT_RGB565,1,0};
static int critical,io_depth,max_io_depth,allocs,fail_alloc,live_allocs,task_fail,notifications;
static int draws,original_done,drain_fail,draw_fail,inject,invalidations;
static int64_t clock_us;
static const uint8_t *active_dma;
static size_t active_bytes;
static uint8_t dma_copy[DMA_BYTES];
static const uint8_t *expected_image;
static esp_lcd_panel_io_callbacks_t registered;
static void *registered_context;
static uint8_t row_pixels[640];

void enter_critical(void){assert(critical==0);++critical;}
void exit_critical(void){assert(critical==1);--critical;}
void test_log(const char *tag,...){(void)tag;}
int64_t esp_timer_get_time(void){clock_us+=10;return clock_us;}
void esp_rom_delay_us(unsigned us){clock_us+=us;}
void vTaskDelay(TickType_t ticks){assert(!critical);clock_us+=ticks*1000;}
void xTaskNotifyGive(TaskHandle_t task){assert(task && !critical);++notifications;}
unsigned ulTaskNotifyTake(int clear,unsigned ticks){(void)clear;(void)ticks;return 1;}
int xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,unsigned stack,void *arg,int priority,TaskHandle_t *out,int core){
    (void)fn;(void)name;(void)arg;assert(stack==4096 && priority==7 && core==0);
    if(task_fail)return 0;*out=(void*)12;return pdPASS;
}
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void){static int mutex;return &mutex;}
int xSemaphoreTakeRecursive(SemaphoreHandle_t s,unsigned ticks){assert(s && ticks==portMAX_DELAY && !critical);++io_depth;if(io_depth>max_io_depth)max_io_depth=io_depth;return 1;}
int xSemaphoreGiveRecursive(SemaphoreHandle_t s){assert(s && io_depth>0);--io_depth;return 1;}
void *heap_caps_malloc(size_t size,unsigned caps){
    (void)caps;assert(!critical);if(++allocs==fail_alloc)return NULL;
    void *p=malloc(size);assert(p);++live_allocs;return p;
}
void *heap_caps_calloc(size_t count,size_t size,unsigned caps){void *p=heap_caps_malloc(count*size,caps);if(p)memset(p,0,count*size);return p;}
void heap_caps_free(void *p){if(p){--live_allocs;free(p);}}
int lv_display_get_horizontal_resolution(lv_display_t *d){return d->width;}
int lv_display_get_vertical_resolution(lv_display_t *d){return d->height;}
int lv_display_get_color_format(lv_display_t *d){return d->format;}
bool lv_display_flush_is_last(lv_display_t *d){return d->last!=0;}
void lv_display_flush_ready(lv_display_t *d){++d->ready;}
void *lv_display_get_screen_active(lv_display_t *d){return d;}
void lv_obj_invalidate(void *d){assert(d==&display);++invalidations;}
esp_lcd_panel_handle_t bsp_display_get_panel_handle(void){return (void*)2;}
static bool original_callback(esp_lcd_panel_io_handle_t io,esp_lcd_panel_io_event_data_t *ev,void *ctx){(void)io;(void)ev;assert(ctx==(void*)3);++original_done;lv_display_flush_ready(&display);return true;}

static void complete_dma(void){
    if(!active_dma)return;
    assert(!memcmp(active_dma,dma_copy,active_bytes)); // no buffer reuse before completion
    clock_us+=1280;
    active_dma=NULL;
    assert(registered.on_color_trans_done);
    registered.on_color_trans_done(frame_io,NULL,registered_context);
}
esp_err_t __real_esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io,int cmd,const void *data,size_t size){
    (void)io;(void)cmd;(void)data;(void)size;assert(!critical);
    if(drain_fail && cmd==0x00)return ESP_FAIL;
    complete_dma();clock_us+=15;return ESP_OK;
}
esp_err_t __real_esp_lcd_panel_io_rx_param(esp_lcd_panel_io_handle_t io,int cmd,void *data,size_t size){
    (void)io;(void)cmd;(void)data;(void)size;assert(!"MISO reads forbidden");return ESP_FAIL;
}
esp_err_t __real_esp_lcd_panel_io_tx_color(esp_lcd_panel_io_handle_t io,int cmd,const void *pixels,size_t size){(void)io;(void)cmd;(void)pixels;(void)size;return ESP_OK;}
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io,int cmd,const void *p,size_t n){return __wrap_esp_lcd_panel_io_tx_param(io,cmd,p,n);}
esp_err_t esp_lcd_panel_io_rx_param(esp_lcd_panel_io_handle_t io,int cmd,void *p,size_t n){return __wrap_esp_lcd_panel_io_rx_param(io,cmd,p,n);}
esp_err_t esp_lcd_panel_io_tx_color(esp_lcd_panel_io_handle_t io,int cmd,const void *p,size_t n){return __wrap_esp_lcd_panel_io_tx_color(io,cmd,p,n);}
esp_err_t __real_esp_lcd_panel_io_register_event_callbacks(esp_lcd_panel_io_handle_t io,const esp_lcd_panel_io_callbacks_t *cb,void *ctx){(void)io;registered=*cb;registered_context=ctx;return ESP_OK;}
esp_err_t __real_spi_bus_initialize(spi_host_device_t host,const spi_bus_config_t *cfg,spi_dma_chan_t dma){(void)host;(void)cfg;(void)dma;return ESP_OK;}
esp_err_t __real_spi_bus_add_device(spi_host_device_t host,const spi_device_interface_config_t *cfg,spi_device_handle_t *out){(void)host;(void)cfg;*out=(void*)4;return ESP_OK;}
esp_err_t __real_esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t bus,const esp_lcd_panel_io_spi_config_t *cfg,esp_lcd_panel_io_handle_t *out){(void)bus;(void)cfg;*out=(void*)1;return ESP_OK;}
esp_err_t __real_spi_device_polling_transmit(spi_device_handle_t dev,spi_transaction_t *t){(void)dev;assert(t->override_freq_hz==1000000 || t->override_freq_hz==80000000);return ESP_OK;}

esp_err_t __real_esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel,int x1,int y1,int x2,int y2,const void *pixels){
    assert(panel==(void*)2 && !critical);++draws;
    if(!worker_dma)return ESP_OK; // original landscape path
    assert(io_depth>=1 && x1==0 && x2==(int)frame.width && y2-y1==DMA_ROWS);
    size_t chunk_bytes=(size_t)frame.width*2*DMA_ROWS;
    esp_lcd_panel_io_tx_param(frame_io,0x2a,NULL,0); // same drain rule as pinned panel driver
    assert(!active_dma);
    if(draw_fail){draw_fail=0;return ESP_FAIL;}
    assert(!memcmp(pixels,expected_image+(size_t)y1*frame.width*2,chunk_bytes));
    active_dma=pixels;active_bytes=chunk_bytes;memcpy(dma_copy,pixels,chunk_bytes);
    if(inject){
        inject=0;
        int old_draws=draws,old_done=display.ready;
        memset(row_pixels,0x44,sizeof(row_pixels));
        assert(__wrap_esp_lcd_panel_draw_bitmap(panel,0,1,320,2,row_pixels)==ESP_OK);
        assert(draws==old_draws && display.ready==old_done+1 && active_dma==pixels);
        assert(!memcmp(active_dma,dma_copy,DMA_BYTES));
    }
    return ESP_OK;
}

static void reset_state(void){
    assert(!active_dma && !io_depth && !critical);
    free_frame_buffers();assert(live_allocs==0);
    memset(&frame,0,sizeof(frame));frame.pending=-1;
    allocs=fail_alloc=task_fail=notifications=draws=original_done=drain_fail=draw_fail=inject=0;
    display=(lv_display_t){320,480,LV_COLOR_FORMAT_RGB565,1,0};clock_us=5000;
    ouse_board_lcd_worker_dma(false);
}
static void init_sender(void){
    ouse_board_lcd_frame_init(&display);assert(frame.display==&display && live_allocs==5);
    assert(frame.dma[0]!=frame.dma[1]);
}
static void put_row(int y,uint8_t value){memset(row_pixels,value,sizeof(row_pixels));assert(__wrap_esp_lcd_panel_draw_bitmap((void*)2,0,y,320,y+1,row_pixels)==ESP_OK);}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    frame_io=(void*)1;
    esp_lcd_panel_io_callbacks_t cb={.on_color_trans_done=original_callback};
    assert(__wrap_esp_lcd_panel_io_register_event_callbacks(frame_io,&cb,(void*)3)==ESP_OK);
    assert(registered.on_color_trans_done(frame_io,NULL,NULL));assert(original_done==1);
    ouse_board_lcd_worker_dma(true);
    assert(!registered.on_color_trans_done(frame_io,NULL,NULL));assert(original_done==1);
    ouse_board_lcd_worker_dma(false);
    puts("PASS worker DMA completion is isolated; ordinary BSP callback still forwards");

    for(int n=1;n<=5;n++){
        reset_state();fail_alloc=n;ouse_board_lcd_frame_init(&display);
        assert(!frame.display && live_allocs==0);
    }
    reset_state();task_fail=1;ouse_board_lcd_frame_init(&display);assert(!frame.display && live_allocs==0);
    puts("PASS all buffer/task allocation failures clean up and preserve BSP fallback");

    reset_state();init_sender();
    uint8_t *input=malloc(FRAME_BYTES);assert(input);memset(input,0x11,FRAME_BYTES);
    int prior=draws;
    display.last=0;
    assert(__wrap_esp_lcd_panel_draw_bitmap((void*)2,0,0,320,240,input)==ESP_OK);
    assert(frame.pending==-1 && draws==prior);
    display.last=1;
    assert(__wrap_esp_lcd_panel_draw_bitmap((void*)2,0,240,320,480,input+240*640)==ESP_OK);
    assert(frame.published==1 && notifications==1 && display.ready==2 && draws==prior);
    uint32_t epoch;int sending=take_latest(&epoch);assert(sending>=0);
    expected_image=frame.image[sending];assert(!memcmp(expected_image,input,FRAME_BYTES));
    put_row(0,0x22);int pending=frame.pending;assert(pending>=0 && pending!=sending);
    put_row(479,0x33);assert(frame.pending==pending && frame.replaced==1);
    assert(!memcmp(frame.image[sending],input,FRAME_BYTES));
    expected_image=frame.image[pending]; // newest complete image wins at the IO acquisition
    int ready_before=display.ready;inject=1;
    send_snapshot(sending,epoch);
    assert(frame.frames==1 && frame.pending==sending && frame.owner[sending]==SLOT_READY && !active_dma && !worker_dma);
    assert(display.ready==ready_before+1 && original_done==0); // only injected UI staging signalled ready
    assert(frame.replaced==2 && max_io_depth>=2);
    puts("PASS staging returns before SPI; IO acquisition promotes newest complete frame; DMA image stays immutable");
    assert(frame.owner[pending]==SLOT_FREE);
    int latest_pending=frame.pending;assert(latest_pending>=0 && latest_pending!=pending);
    sending=take_latest(&epoch);assert(sending==latest_pending);
    memset(input,0x11,FRAME_BYTES);memset(input,0x22,640);memset(input+640,0x44,640);memset(input+479*640,0x33,640);
    assert(!memcmp(frame.image[sending],input,FRAME_BYTES));
    expected_image=frame.image[sending];send_snapshot(sending,epoch);
    assert(frame.frames==2 && !active_dma);
    puts("PASS worker sends the complete newest image with dedicated DMA slots and confirmed drain before reuse");

    int writing=reserve_publish_slot();assert(writing>=0 && frame.owner[writing]==SLOT_WRITING);
    assert(take_latest(&epoch)==-1);release_slot(writing);
    put_row(2,0x55);sending=take_latest(&epoch);assert(sending>=0);
    put_row(5,0x99);assert(frame.pending>=0);
    prior=draws;
    ouse_board_lcd_frame_orientation_begin();assert(frame.pending==-1);assert(io_depth==1);
    ouse_board_lcd_frame_orientation_end(true);assert(io_depth==0 && frame.enabled && frame.width==480 && frame.height==320);
    send_snapshot(sending,epoch);assert(draws==prior && frame.owner[sending]==SLOT_FREE);
    display.width=480;display.height=320;
    assert(__wrap_esp_lcd_panel_draw_bitmap((void*)2,0,0,480,320,input)==ESP_OK && draws==prior);
    sending=take_latest(&epoch);assert(sending>=0);
    expected_image=frame.image[sending];send_snapshot(sending,epoch);
    assert(draws==prior+16 && !active_dma && !worker_dma);
    puts("PASS half-copied image cannot be taken; orientation invalidates popped old work and sends complete landscape frames");

    display.width=320;display.height=480;
    ouse_board_lcd_frame_orientation_begin();ouse_board_lcd_frame_orientation_end(false);
    put_row(3,0x66);sending=take_latest(&epoch);assert(sending>=0);
    expected_image=frame.image[sending];draw_fail=1;send_snapshot(sending,epoch);
    assert(frame.errors==1 && !frame.fault && !active_dma && !worker_dma && frame.owner[sending]==SLOT_FREE);
    put_row(3,0x67);sending=take_latest(&epoch);assert(sending>=0);
    expected_image=frame.image[sending];send_snapshot(sending,epoch);
    assert(frame.errors==1 && !frame.fault && !active_dma && !worker_dma);
    puts("PASS a failed draw drains and releases ownership; the next complete snapshot can recover");
    put_row(3,0x68);sending=take_latest(&epoch);assert(sending>=0);
    expected_image=frame.image[sending];drain_fail=1;
    send_snapshot(sending,epoch);
    assert(frame.fault && frame.owner[sending]==SLOT_SENDING && worker_dma && active_dma);
    put_row(4,0x77);assert(frame.pending==-1);
    assert(!memcmp(active_dma,dma_copy,active_bytes));
    drain_fail=0;complete_dma();ouse_board_lcd_worker_dma(false);
    puts("PASS unconfirmed DMA drain retains ownership and stops publication without blocking UI staging");
    free(input);reset_state();
    assert(!critical && !io_depth && live_allocs==0);
    puts("PASS production asynchronous LCD tests; no serial port or hardware used");
}
'''
put('test.c', source)
vswhere = Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
vs = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
put('env.cmd', f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n')
env = dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(OUT/'env.cmd')],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1);env[key]=value
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
subprocess.run([str(compiler),'/nologo','/std:c11','/W4','/WX','/wd4127','/I'+str(OUT),str(OUT/'test.c'),'/Fe:'+str(OUT/'test.exe')],cwd=OUT,env=env,check=True)
subprocess.run([str(OUT/'test.exe')],check=True)
