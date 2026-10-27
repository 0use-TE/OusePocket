// Resolve an ambiguous single-sector CRC acknowledgement without replaying a
// write. The shared SD bus mutex is held by board_sd_bus.c throughout.
#include "sdmmc_cmd.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

esp_err_t __real_sdmmc_write_sectors(sdmmc_card_t *,const void *,size_t,size_t);
esp_err_t __real_sdmmc_read_sectors(sdmmc_card_t *,void *,size_t,size_t);
static void *verification_sector;

esp_err_t ouse_board_sd_verified_sector(sdmmc_card_t *card,const void *source,size_t start,size_t count)
{
    esp_err_t error=__real_sdmmc_write_sectors(card,source,start,count);
    if(error!=ESP_ERR_INVALID_CRC || !card || !source || count!=1 || card->csd.sector_size!=512 || card->host.slot!=0)return error;
    if(!verification_sector)verification_sector=heap_caps_aligned_alloc(64,512,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
    if(!verification_sector)return error;
    // CMD13 / SEND_STATUS: ready-for-data (bit 8), transfer state (bits 12:9=4).
    // The IDF single-block error path does not wait for programming to finish.
    bool ready=false;
    for(unsigned attempt=0;attempt<25;++attempt){
        sdmmc_command_t status={.opcode=13,.arg=(uint32_t)card->rca<<16,.flags=SCF_CMD_AC|SCF_RSP_R1,.timeout_ms=100};
        esp_err_t checked=card->host.do_transaction(card->host.slot,&status);
        if(checked!=ESP_OK || status.error!=ESP_OK)break;
        uint32_t response=status.response[0];
        if((response&(1u<<8)) && ((response>>9)&15)==4){ready=true;break;}
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if(!ready)return error;
    esp_err_t read=__real_sdmmc_read_sectors(card,verification_sector,start,1);
    if(read!=ESP_OK || memcmp(source,verification_sector,512))return error;
    ESP_LOGW("OuseSD","Sector %lu CRC acknowledgement verified by readback; write not replayed",(unsigned long)start);
    return ESP_OK;
}
