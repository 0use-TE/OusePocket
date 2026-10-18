"""Exercise real board recovery with SDK doubles and guard the shared controller."""
from pathlib import Path
import os
import subprocess
root=Path(__file__).resolve().parents[1]
source=(root/'boards/waveshare_lcd35/board_sd.c').read_text(encoding='utf-8')
assert 'sdmmc_host_deinit' not in source
assert 'sd_pwr_ctrl_del_on_chip_ldo' not in source
start=source.index('ouse_board_sd_bus_lock();')
reset=source.index('sd_host_remove_slot(sdmmc_get_slot_handle(host.slot))',start)
add=source.index('sdmmc_host_init_slot(host.slot,&slot)',reset)
initialize=source.index('sdmmc_card_init(&host,&card)',reset)
unlock=source.index('ouse_board_sd_bus_unlock();',initialize)
assert start<reset<add<initialize<unlock
assert 'if(slot_ready)' in source[start:reset]
assert 'if(error==ESP_OK)slot_ready=false' in source[reset:add]
assert 'sdmmc_host_set_card_clk' not in source
print('PASS board source contract: isolated slot recreation, serialized re-identification, no shared-controller/power teardown or premature clock configuration')
output=root/'.tools/sd-board-check'
output.mkdir(parents=True,exist_ok=True)
headers=r'''
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_NO_MEM 0x101
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_DMA 2
#define SDMMC_HOST_SLOT_0 0
#define SDMMC_SLOT_NO_CD -1
#define SDMMC_SLOT_NO_WP -1
typedef void *sd_pwr_ctrl_handle_t;
typedef void *sd_host_slot_handle_t;
typedef struct {int ldo_chan_id;} sd_pwr_ctrl_ldo_config_t;
typedef struct {int slot,max_freq_khz,unaligned_multi_block_rw_max_chunk_size;void *pwr_ctrl_handle;} sdmmc_host_t;
#define SDMMC_HOST_DEFAULT() ((sdmmc_host_t){0})
typedef struct {int cd,wp,width;} sdmmc_slot_config_t;
typedef struct {struct {uint32_t capacity,sector_size;} csd;struct {char name[8];} cid;} sdmmc_card_t;
typedef struct {bool probing,detected,readable;uint64_t capacity_bytes;uint32_t sector_bytes;char name[16];esp_err_t error;} ouse_sd_state_t;
typedef struct {bool mounted;} ouse_files_state_t;
typedef struct {const char *id,*title,*body;} ouse_notification_request_t;
static ouse_sd_state_t published;
static esp_err_t (*recovery)(void);
static unsigned power_calls,host_calls,slot_calls,removed,initialized,locks,formats;
static int power_error,slot_error,card_error,mount_error,remove_error;
static bool live_slot,wifi_live=true;
void ouse_devices_publish_sd(const ouse_sd_state_t *state){published=*state;}
void ouse_sd_get(ouse_sd_state_t *state){*state=published;}
void ouse_files_get(ouse_files_state_t *state){state->mounted=false;}
esp_err_t ouse_files_start(esp_err_t (*callback)(void)){recovery=callback;return ESP_OK;}
esp_err_t ouse_files_init(sdmmc_card_t *card){assert(card->csd.sector_size==512);return mount_error;}
esp_err_t ouse_files_format_unmounted(sdmmc_card_t *card){(void)card;++formats;return ESP_OK;}
esp_err_t ouse_notifications_cancel(const char *owner,const char *id){(void)owner;(void)id;return ESP_OK;}
esp_err_t ouse_notifications_publish(const char *owner,const ouse_notification_request_t *request){(void)owner;(void)request;return ESP_OK;}
esp_err_t sd_pwr_ctrl_new_on_chip_ldo(const sd_pwr_ctrl_ldo_config_t *config,void **handle){assert(config->ldo_chan_id==4);++power_calls;if(!power_error)*handle=(void *)1;return power_error;}
esp_err_t sdmmc_host_init(void){++host_calls;return ESP_OK;}
sd_host_slot_handle_t sdmmc_get_slot_handle(int slot){assert(slot==0&&live_slot);return (void *)2;}
esp_err_t sd_host_remove_slot(sd_host_slot_handle_t handle){assert(locks==1&&handle==(void *)2&&live_slot&&wifi_live);++removed;if(!remove_error)live_slot=false;return remove_error;}
esp_err_t sdmmc_host_init_slot(int slot,const sdmmc_slot_config_t *config){assert(locks==1&&slot==0&&!live_slot&&config->width==4);++slot_calls;if(!slot_error)live_slot=true;return slot_error;}
esp_err_t sdmmc_card_init(const sdmmc_host_t *host,sdmmc_card_t *card){assert(locks==1&&live_slot&&host->slot==0&&wifi_live);++initialized;card->csd.capacity=1024;card->csd.sector_size=512;card->cid.name[0]='T';return card_error;}
esp_err_t sdmmc_get_status(sdmmc_card_t *card){(void)card;return ESP_OK;}
esp_err_t sdmmc_read_sectors(sdmmc_card_t *card,void *data,size_t start,size_t count){(void)card;(void)start;assert(count==1);memset(data,0,512);return ESP_OK;}
void *heap_caps_aligned_alloc(size_t alignment,size_t size,unsigned caps){(void)alignment;(void)caps;return malloc(size);}
void heap_caps_free(void *data){free(data);}
const char *esp_err_to_name(esp_err_t error){return error?"ESP_FAIL":"ESP_OK";}
void ouse_board_sd_bus_lock(void){assert(!locks);++locks;}
void ouse_board_sd_bus_unlock(void){assert(locks==1);--locks;}
'''
headers=headers.replace('#include <assert.h>','#include <assert.h>\n#include <string.h>')
(output/'sdk.h').write_text(headers,encoding='utf-8')
for header in ['ouse_device_driver.h','ouse_notifications.h','ouse_files.h','driver/sdmmc_host.h','driver/sd_host.h','sdmmc_cmd.h','sd_pwr_ctrl_by_on_chip_ldo.h','esp_heap_caps.h','esp_log.h']:
    path=output/header
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text('#include "sdk.h"\n',encoding='utf-8')
fixture=r'''
int main(void){
    power_error=ESP_FAIL;ouse_board_sd_probe();assert(recovery&&published.error==ESP_FAIL&&!host_calls);
    power_error=0;assert(recovery()==ESP_OK);assert(published.readable&&live_slot&&host_calls==1&&slot_calls==1&&initialized==1);
    for(unsigned cycle=0;cycle<8;++cycle){assert(recovery()==ESP_OK);assert(wifi_live&&!locks);}
    assert(removed==8&&host_calls==1&&power_calls==2&&slot_calls==9);
    card_error=ESP_FAIL;assert(recovery()==ESP_FAIL);assert(!published.readable&&wifi_live&&!locks);
    card_error=0;assert(recovery()==ESP_OK);
    slot_error=ESP_FAIL;assert(recovery()==ESP_FAIL);assert(!live_slot&&wifi_live&&!locks);
    slot_error=0;assert(recovery()==ESP_OK);
    remove_error=ESP_FAIL;unsigned previous=initialized;assert(recovery()==ESP_FAIL);assert(live_slot&&initialized==previous&&!locks);
    remove_error=0;assert(recovery()==ESP_OK);
    mount_error=ESP_FAIL;assert(recovery()==ESP_FAIL);mount_error=0;assert(recovery()==ESP_OK);
    assert(wifi_live&&host_calls==1&&!formats&&!locks);
    puts("PASS real board SD orchestration with SDK doubles: cold failure, repeated slot-only recreation, card/slot/remove/mount failure retry, retained controller/power, lock balance, no format");
    return 0;
}
'''
(output/'check.c').write_text('#include "'+(root/'boards/waveshare_lcd35/board_sd.c').as_posix()+'"\n'+fixture,encoding='utf-8')
setup=root/'.tools/ouse-system-check/environment.cmd'
assert setup.exists(),'Run tools/test-ouse-system.py first'
environment=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(setup)],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1)
        environment[key]=value
compiler=Path(environment['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
subprocess.run([str(compiler),'/nologo','/utf-8','/std:c11','/UNDEBUG','/I'+str(output),str(output/'check.c'),'/Fe'+str(output/'check.exe')],cwd=output,env=environment,check=True)
subprocess.run([str(output/'check.exe')],cwd=output,check=True)
