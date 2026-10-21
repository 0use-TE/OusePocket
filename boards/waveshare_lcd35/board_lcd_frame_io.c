// SPDX-License-Identifier: MIT
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_attr.h"
#include <stdatomic.h>
#include "lcd_frame.h"

static esp_lcd_panel_io_handle_t frame_io;
esp_lcd_panel_io_handle_t ouse_board_lcd_frame_io(void){return frame_io;}

// A recursive mutex protects an entire worker frame, including command setup and
// queued DMA drain. Ordinary panel commands take the same mutex per call.
static SemaphoreHandle_t lcd_io_lock;
static atomic_bool worker_dma;
static esp_lcd_panel_io_callbacks_t original_callbacks;
static void *original_callback_context;

bool ouse_board_lcd_io_lock_init(void)
{
    if(!lcd_io_lock)lcd_io_lock=xSemaphoreCreateRecursiveMutex();
    return lcd_io_lock!=NULL;
}
void ouse_board_lcd_io_lock(void)
{if(lcd_io_lock)xSemaphoreTakeRecursive(lcd_io_lock,portMAX_DELAY);}
void ouse_board_lcd_io_unlock(void)
{if(lcd_io_lock)xSemaphoreGiveRecursive(lcd_io_lock);}
void ouse_board_lcd_worker_dma(bool active)
{atomic_store_explicit(&worker_dma,active,memory_order_release);}

static bool IRAM_ATTR lcd_color_done(esp_lcd_panel_io_handle_t io,esp_lcd_panel_io_event_data_t *event,void *context)
{
    (void)context;
    // A worker DMA owns ONLY its private bounce buffers. Its completion must
    // never mark an unrelated UI flush ready or touch LVGL from the worker.
    if(atomic_load_explicit(&worker_dma,memory_order_acquire))return false;
    if(original_callbacks.on_color_trans_done)
        return original_callbacks.on_color_trans_done(io,event,original_callback_context);
    return false;
}

esp_err_t __real_esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t,int,const void *,size_t);
esp_err_t __wrap_esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io,int cmd,const void *param,size_t size)
{
    bool target=io==frame_io;
    if(target)ouse_board_lcd_io_lock();
    esp_err_t e=__real_esp_lcd_panel_io_tx_param(io,cmd,param,size);
    if(target)ouse_board_lcd_io_unlock();
    return e;
}
esp_err_t __real_esp_lcd_panel_io_rx_param(esp_lcd_panel_io_handle_t,int,void *,size_t);
esp_err_t __wrap_esp_lcd_panel_io_rx_param(esp_lcd_panel_io_handle_t io,int cmd,void *param,size_t size)
{
    bool target=io==frame_io;
    if(target)ouse_board_lcd_io_lock();
    esp_err_t e=__real_esp_lcd_panel_io_rx_param(io,cmd,param,size);
    if(target)ouse_board_lcd_io_unlock();
    return e;
}
esp_err_t __real_esp_lcd_panel_io_tx_color(esp_lcd_panel_io_handle_t,int,const void *,size_t);
esp_err_t __wrap_esp_lcd_panel_io_tx_color(esp_lcd_panel_io_handle_t io,int cmd,const void *pixels,size_t size)
{
    bool target=io==frame_io;
    if(target)ouse_board_lcd_io_lock();
    esp_err_t e=__real_esp_lcd_panel_io_tx_color(io,cmd,pixels,size);
    if(target)ouse_board_lcd_io_unlock();
    return e;
}
esp_err_t __real_esp_lcd_panel_io_register_event_callbacks(esp_lcd_panel_io_handle_t,const esp_lcd_panel_io_callbacks_t *,void *);
esp_err_t __wrap_esp_lcd_panel_io_register_event_callbacks(esp_lcd_panel_io_handle_t io,const esp_lcd_panel_io_callbacks_t *callbacks,void *context)
{
    if(io!=frame_io || !callbacks)return __real_esp_lcd_panel_io_register_event_callbacks(io,callbacks,context);
    ouse_board_lcd_io_lock();
    esp_err_t e=ESP_OK;
    if(lcd_io_lock)e=__real_esp_lcd_panel_io_tx_param(io,0x00,NULL,0);
    if(e==ESP_OK){
        esp_lcd_panel_io_callbacks_t proxy=*callbacks;
        proxy.on_color_trans_done=lcd_color_done;
        e=__real_esp_lcd_panel_io_register_event_callbacks(io,&proxy,NULL);
        if(e==ESP_OK){original_callbacks=*callbacks;original_callback_context=context;}
    }
    ouse_board_lcd_io_unlock();
    return e;
}


esp_err_t __real_esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t bus,const esp_lcd_panel_io_spi_config_t *cfg,esp_lcd_panel_io_handle_t *out);
esp_err_t __wrap_esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t bus,const esp_lcd_panel_io_spi_config_t *cfg,esp_lcd_panel_io_handle_t *out)
{
    if(!cfg || !out || (spi_host_device_t)bus!=SPI2_HOST || cfg->cs_gpio_num!=23)
        return __real_esp_lcd_new_panel_io_spi(bus,cfg,out);
    esp_lcd_panel_io_spi_config_t tuned=*cfg;
    tuned.on_color_trans_done=lcd_color_done;tuned.user_ctx=NULL;
    esp_err_t e=__real_esp_lcd_new_panel_io_spi(bus,&tuned,out);
    if(e==ESP_OK){
        frame_io=*out;
        original_callbacks.on_color_trans_done=cfg->on_color_trans_done;
        original_callback_context=cfg->user_ctx;
    }
    return e;
}
