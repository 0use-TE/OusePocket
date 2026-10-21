// SPDX-License-Identifier: MIT
#include "driver/spi_master.h"
#include "lcd_frame.h"
#include "lcd_scan.h"

static spi_device_handle_t device;
static bool reading,restore;
esp_err_t __real_spi_bus_initialize(spi_host_device_t,const spi_bus_config_t *,spi_dma_chan_t);
esp_err_t __wrap_spi_bus_initialize(spi_host_device_t host,const spi_bus_config_t *cfg,spi_dma_chan_t dma)
{
    if(host!=SPI2_HOST || !cfg || cfg->mosi_io_num!=20 || cfg->sclk_io_num!=21)
        return __real_spi_bus_initialize(host,cfg,dma);
    spi_bus_config_t tuned=*cfg;tuned.miso_io_num=22;
    return __real_spi_bus_initialize(host,&tuned,dma);
}
esp_err_t __real_spi_bus_add_device(spi_host_device_t,const spi_device_interface_config_t *,spi_device_handle_t *);
esp_err_t __wrap_spi_bus_add_device(spi_host_device_t host,const spi_device_interface_config_t *cfg,spi_device_handle_t *out)
{
    if(!cfg || !out)return __real_spi_bus_add_device(host,cfg,out);
    spi_device_interface_config_t tuned=*cfg;
    if(host==SPI2_HOST && cfg->spics_io_num==23)tuned.sample_point=SPI_SAMPLING_POINT_PHASE_1;
    esp_err_t e=__real_spi_bus_add_device(host,&tuned,out);
    if(e==ESP_OK && host==SPI2_HOST && cfg->spics_io_num==23)device=*out;
    return e;
}
esp_err_t __real_spi_device_polling_transmit(spi_device_handle_t,spi_transaction_t *);
esp_err_t __wrap_spi_device_polling_transmit(spi_device_handle_t dev,spi_transaction_t *t)
{
    if(dev!=device || (!reading && !restore))return __real_spi_device_polling_transmit(dev,t);
    uint32_t saved=t->override_freq_hz;t->override_freq_hz=reading?1000000:80000000;
    esp_err_t e=__real_spi_device_polling_transmit(dev,t);t->override_freq_hz=saved;
    if(!reading && e==ESP_OK)restore=false;
    return e;
}
bool ouse_board_lcd_scan_raw(int command,uint8_t *data,unsigned size)
{
    if(!device || !ouse_board_lcd_frame_io())return false;
    reading=true;
    esp_err_t e=esp_lcd_panel_io_rx_param(ouse_board_lcd_frame_io(),command,data,size);
    reading=false;restore=true;
    esp_err_t drained=esp_lcd_panel_io_tx_param(ouse_board_lcd_frame_io(),0x00,NULL,0);
    return e==ESP_OK && drained==ESP_OK;
}
