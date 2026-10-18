#include "ouse_device_driver.h"
#include "board_sd_bus.h"
#include "ouse_notifications.h"
#include "ouse_files.h"
#include "driver/sdmmc_host.h"
#include "driver/sd_host.h"
#include "sdmmc_cmd.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static sdmmc_card_t card;
static sd_pwr_ctrl_handle_t power;
static bool host_ready,slot_ready;
static esp_err_t reprobe(void);
extern sd_host_slot_handle_t sdmmc_get_slot_handle(int slot_id);
static char layout[128]="not-probed";
static const char *filesystem_kind(const unsigned char *sector)
{
    if(!memcmp(sector+3,"EXFAT   ",8))return "exFAT";
    if(!memcmp(sector+3,"NTFS    ",8))return "NTFS";
    if(!memcmp(sector+82,"FAT32   ",8))return "FAT32";
    if(!memcmp(sector+54,"FAT16   ",8))return "FAT16";
    if(!memcmp(sector+54,"FAT12   ",8))return "FAT12";
    return "unknown";
}
esp_err_t ouse_board_sd_layout(char *out,size_t capacity)
{
    int length=snprintf(out,capacity,"%s",layout);
    return length<0 || (size_t)length>=capacity ? ESP_ERR_NO_MEM : ESP_OK;
}
esp_err_t ouse_board_sd_format(const char *confirmation)
{
    ouse_sd_state_t state;ouse_sd_get(&state);ouse_files_state_t files;ouse_files_get(&files);
    if(!state.detected || files.mounted)return ESP_ERR_INVALID_STATE;
    char expected[64];snprintf(expected,sizeof(expected),"%s:%llu",state.name,(unsigned long long)state.capacity_bytes);
    if(strcmp(confirmation,expected))return ESP_ERR_INVALID_ARG;
    if(memcmp(state.name,card.cid.name,strlen(state.name)) || state.capacity_bytes!=(uint64_t)card.csd.capacity*card.csd.sector_size)return ESP_ERR_INVALID_STATE;
    esp_err_t error=sdmmc_get_status(&card);if(error!=ESP_OK)return error;
    error=ouse_files_format_unmounted(&card);
    if(error==ESP_OK)error=ouse_files_init(&card);
    if(error==ESP_OK)(void)ouse_notifications_cancel("ouse.settings","sd-filesystem");
    return error;
}

void ouse_board_sd_probe(void)
{
    esp_err_t error=ouse_files_start(reprobe);
    if(error==ESP_OK)(void)reprobe();
    else ESP_LOGW("OuseSD","SD service startup failed: %s",esp_err_to_name(error));
}
static esp_err_t reprobe(void)
{
    ouse_sd_state_t state={.probing=true};
    ouse_devices_publish_sd(&state);
    sd_pwr_ctrl_ldo_config_t supply={.ldo_chan_id=4};
    esp_err_t error=power ? ESP_OK : sd_pwr_ctrl_new_on_chip_ldo(&supply,&power);
    sdmmc_host_t host=SDMMC_HOST_DEFAULT();
    host.slot=SDMMC_HOST_SLOT_0;host.max_freq_khz=10000;
    host.unaligned_multi_block_rw_max_chunk_size=8;
    if(error==ESP_OK) {
        host.pwr_ctrl_handle=power;
        if(!host_ready){error=sdmmc_host_init();if(error==ESP_OK)host_ready=true;}
    }
    if(error==ESP_OK){
        ouse_board_sd_bus_lock();
        if(slot_ready){
            error=sd_host_remove_slot(sdmmc_get_slot_handle(host.slot));
            if(error==ESP_OK)slot_ready=false;
        }
        if(error==ESP_OK){
            sdmmc_slot_config_t slot={.cd=SDMMC_SLOT_NO_CD,.wp=SDMMC_SLOT_NO_WP,.width=4};
            error=sdmmc_host_init_slot(host.slot,&slot);
            slot_ready=error==ESP_OK;
        }
        if(error==ESP_OK)error=sdmmc_card_init(&host,&card);
        ouse_board_sd_bus_unlock();
    }
    if(error==ESP_OK) {
        state.detected=true;
        state.sector_bytes=card.csd.sector_size;
        state.capacity_bytes=(uint64_t)card.csd.capacity*card.csd.sector_size;
        memcpy(state.name,card.cid.name,sizeof(card.cid.name));
        state.name[sizeof(state.name)-1]=0;
        if(state.sector_bytes!=512)error=ESP_ERR_NOT_SUPPORTED;
        else {
            void *sector=heap_caps_aligned_alloc(64,512,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
            if(!sector)error=ESP_ERR_NO_MEM;
            else {
                error=sdmmc_read_sectors(&card,sector,0,1);
                if(error==ESP_OK){
                    unsigned char *bytes=sector;uint32_t start=0;unsigned type=0;
                    bool mbr=bytes[510]==0x55 && bytes[511]==0xaa;
                    if(mbr && !strcmp(filesystem_kind(bytes),"unknown"))for(unsigned index=0;index<4;++index){
                        unsigned char *entry=bytes+446+16*index;if(!entry[4])continue;
                        type=entry[4];start=(uint32_t)entry[8]|((uint32_t)entry[9]<<8)|((uint32_t)entry[10]<<16)|((uint32_t)entry[11]<<24);break;
                    }
                    esp_err_t read=ESP_OK;
                    if(start && start<card.csd.capacity)read=sdmmc_read_sectors(&card,sector,start,1);
                    snprintf(layout,sizeof(layout),"signature=%u,partition_type=%02x,start=%lu,filesystem=%s,boot_read=%s",
                        mbr,type,(unsigned long)start,read==ESP_OK ? filesystem_kind(bytes) : "unknown",esp_err_to_name(read));
                }
                heap_caps_free(sector);
            }
        }
        state.readable=error==ESP_OK;
    }
    state.probing=false;state.error=error;
    ouse_devices_publish_sd(&state);
    if(error==ESP_OK) {
        esp_err_t mounted=ouse_files_init(&card);
        error=mounted;
        ESP_LOGI("OuseSD","Identified: name=%s capacity=%llu sector=%lu; filesystem=%s; never format",
            state.name,(unsigned long long)state.capacity_bytes,(unsigned long)state.sector_bytes,esp_err_to_name(mounted));
        if(mounted!=ESP_OK){
            ouse_notification_request_t notification={.id="sd-filesystem",.title="SD 卡文件系统不可用",.body="未格式化。请检查文件系统是否受支持。"};
            (void)ouse_notifications_publish("ouse.settings",&notification);
        }
        else{(void)ouse_notifications_cancel("ouse.settings","sd-filesystem");(void)ouse_notifications_cancel("ouse.settings","sd-probe");}
    } else {
        ESP_LOGW("OuseSD","Read-only probe failed: detected=%u error=%s; no format attempted",state.detected,esp_err_to_name(error));
        char body[128];snprintf(body,sizeof(body),"%s (%s). No format or data writes attempted.",
            state.detected ? "SD card identified but sector read failed" : "SD card could not be identified",esp_err_to_name(error));
        ouse_notification_request_t notification={.id="sd-probe",.title="SD 卡识别失败",.body=body};
        (void)ouse_notifications_publish("ouse.settings",&notification);
    }
    return error;
}
