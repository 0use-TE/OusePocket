#include "sdmmc_cmd.h"
#include "board_sd_bus.h"
#include "driver/sdmmc_host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static StaticSemaphore_t bus_storage;
static SemaphoreHandle_t bus;

void ouse_board_sd_bus_init(void)
{bus=xSemaphoreCreateRecursiveMutexStatic(&bus_storage);configASSERT(bus);}
void ouse_board_sd_bus_lock(void){configASSERT(bus);xSemaphoreTakeRecursive(bus,portMAX_DELAY);}
void ouse_board_sd_bus_unlock(void){xSemaphoreGiveRecursive(bus);}

esp_err_t __real_sdmmc_host_do_transaction(int slot,sdmmc_command_t *command);
esp_err_t __real_sdmmc_write_sectors(sdmmc_card_t *card,const void *source,size_t start,size_t count);
esp_err_t __real_sdmmc_read_sectors(sdmmc_card_t *card,void *destination,size_t start,size_t count);
esp_err_t __real_sdmmc_card_init(const sdmmc_host_t *host,sdmmc_card_t *card);
esp_err_t ouse_board_sd_verified_sector(sdmmc_card_t *card,const void *source,size_t start,size_t count);

esp_err_t __wrap_sdmmc_host_do_transaction(int slot,sdmmc_command_t *command)
{
    if(!bus)return ESP_ERR_INVALID_STATE;
    xSemaphoreTakeRecursive(bus,portMAX_DELAY);
    esp_err_t error=__real_sdmmc_host_do_transaction(slot,command);
    xSemaphoreGiveRecursive(bus);return error;
}
esp_err_t __wrap_sdmmc_write_sectors(sdmmc_card_t *card,const void *source,size_t start,size_t count)
{
    if(!bus)return ESP_ERR_INVALID_STATE;
    xSemaphoreTakeRecursive(bus,portMAX_DELAY);
    esp_err_t error=ESP_OK;
    if(!card || !source || !count)error=__real_sdmmc_write_sectors(card,source,start,count);
    else if(start>=card->csd.capacity || count>card->csd.capacity-start)error=ESP_ERR_INVALID_SIZE;
    else {const unsigned char *bytes=source;
        for(size_t offset=0;offset<count;++offset){error=ouse_board_sd_verified_sector(card,bytes+offset*card->csd.sector_size,start+offset,1);if(error!=ESP_OK)break;}}
    xSemaphoreGiveRecursive(bus);return error;
}
esp_err_t __wrap_sdmmc_read_sectors(sdmmc_card_t *card,void *destination,size_t start,size_t count)
{
    if(!bus)return ESP_ERR_INVALID_STATE;
    xSemaphoreTakeRecursive(bus,portMAX_DELAY);
    esp_err_t error=__real_sdmmc_read_sectors(card,destination,start,count);
    xSemaphoreGiveRecursive(bus);return error;
}
esp_err_t __wrap_sdmmc_card_init(const sdmmc_host_t *host,sdmmc_card_t *card)
{
    if(!bus)return ESP_ERR_INVALID_STATE;
    xSemaphoreTakeRecursive(bus,portMAX_DELAY);
    esp_err_t error=__real_sdmmc_card_init(host,card);
    xSemaphoreGiveRecursive(bus);return error;
}
