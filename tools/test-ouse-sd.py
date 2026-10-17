"""Compile the actual board SD probe against read-only SDK test doubles."""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".tools/ouse-sd-check"
OUT.mkdir(parents=True, exist_ok=True)
sdk = r'''
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_ARG 0x102
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_DMA 2
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define SDMMC_HOST_SLOT_0 0
#define SDMMC_FREQ_DEFAULT 20000
#define SDMMC_SLOT_NO_CD -1
#define SDMMC_SLOT_NO_WP -1
#define SDMMC_HOST_DEFAULT() {.slot=0}
typedef void *sd_pwr_ctrl_handle_t;
typedef struct {int ldo_chan_id;} sd_pwr_ctrl_ldo_config_t;
typedef struct {int slot,max_freq_khz;sd_pwr_ctrl_handle_t pwr_ctrl_handle;} sdmmc_host_t;
typedef struct {int cd,wp,width;} sdmmc_slot_config_t;
typedef struct {sdmmc_host_t host;struct {uint32_t capacity,sector_size;} csd;struct {char name[8];uint32_t serial;} cid;} sdmmc_card_t;
typedef struct {bool mounted;} ouse_files_state_t;
void ouse_files_get(ouse_files_state_t *);
typedef struct {bool probing,detected,readable;uint64_t capacity_bytes;uint32_t sector_bytes;char name[8];esp_err_t error;} ouse_sd_state_t;
void ouse_sd_get(ouse_sd_state_t *);
esp_err_t ouse_files_format_unmounted(sdmmc_card_t *);
esp_err_t ouse_notifications_cancel(const char *,const char *);
typedef struct {const char *id,*title,*body,*payload;bool ongoing;} ouse_notification_request_t;
void ouse_devices_publish_sd(const ouse_sd_state_t *);
esp_err_t ouse_files_init(sdmmc_card_t *);
esp_err_t ouse_notifications_publish(const char *,const ouse_notification_request_t *);
esp_err_t sd_pwr_ctrl_new_on_chip_ldo(const sd_pwr_ctrl_ldo_config_t *,sd_pwr_ctrl_handle_t *);
esp_err_t sd_pwr_ctrl_del_on_chip_ldo(sd_pwr_ctrl_handle_t);
esp_err_t sdmmc_host_init(void);
esp_err_t sdmmc_host_init_slot(int,const sdmmc_slot_config_t *);
esp_err_t sdmmc_host_deinit_slot(int);
esp_err_t sdmmc_card_init(const sdmmc_host_t *,sdmmc_card_t *);
esp_err_t sdmmc_get_status(sdmmc_card_t *);
esp_err_t sdmmc_read_sectors(sdmmc_card_t *,void *,size_t,size_t);
void *heap_caps_aligned_alloc(size_t,size_t,unsigned);
void heap_caps_free(void *);
const char *esp_err_to_name(esp_err_t);
'''
(OUT / "sdk.h").write_text(sdk)
for name in ["ouse_device_driver.h", "ouse_notifications.h", "ouse_files.h", "driver/sdmmc_host.h", "sdmmc_cmd.h",
             "sd_pwr_ctrl_by_on_chip_ldo.h", "esp_heap_caps.h", "esp_log.h"]:
    path = OUT / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('#include "sdk.h"\n')
fixture = r'''
#include "sdk.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static int failure,published,notifications,reads,freed,removed,power_deleted;
static unsigned char sector[512];
static ouse_sd_state_t result;
esp_err_t sdmmc_get_status(sdmmc_card_t *card){(void)card;return ESP_OK;}
void ouse_sd_get(ouse_sd_state_t *out){*out=result;}
void ouse_files_get(ouse_files_state_t *out){out->mounted=true;}
esp_err_t ouse_files_format_unmounted(sdmmc_card_t *card){(void)card;assert(0);return ESP_FAIL;}
esp_err_t ouse_notifications_cancel(const char *owner,const char *id){(void)owner;(void)id;return ESP_OK;}
esp_err_t ouse_files_init(sdmmc_card_t *card){assert(card);return ESP_OK;}
void ouse_devices_publish_sd(const ouse_sd_state_t *state){++published;result=*state;}
esp_err_t ouse_notifications_publish(const char *owner,const ouse_notification_request_t *note){assert(!strcmp(owner,"ouse.settings") && note->title && note->body);++notifications;return ESP_OK;}
esp_err_t sd_pwr_ctrl_new_on_chip_ldo(const sd_pwr_ctrl_ldo_config_t *config,sd_pwr_ctrl_handle_t *out){assert(config->ldo_chan_id==4);if(failure==1)return ESP_FAIL;*out=(void *)1;return ESP_OK;}
esp_err_t sd_pwr_ctrl_del_on_chip_ldo(sd_pwr_ctrl_handle_t power){assert(power==(void *)1);++power_deleted;return ESP_OK;}
esp_err_t sdmmc_host_init(void){return failure==2 ? ESP_FAIL : ESP_OK;}
esp_err_t sdmmc_host_init_slot(int slot,const sdmmc_slot_config_t *config){assert(slot==0 && config->width==4 && config->cd==-1 && config->wp==-1);return failure==3 ? ESP_FAIL : ESP_OK;}
esp_err_t sdmmc_host_deinit_slot(int slot){assert(slot==0);++removed;return ESP_OK;}
esp_err_t sdmmc_card_init(const sdmmc_host_t *host,sdmmc_card_t *card){assert(host->slot==0 && host->max_freq_khz==20000 && host->pwr_ctrl_handle==(void *)1);if(failure==4)return ESP_ERR_TIMEOUT;card->csd.capacity=8388608;card->csd.sector_size=failure==7 ? 4096 : 512;memcpy(card->cid.name,"12345678",8);return ESP_OK;}
esp_err_t sdmmc_read_sectors(sdmmc_card_t *card,void *buffer,size_t start,size_t count){assert(card && buffer==sector && start==0 && count==1);++reads;memset(buffer,0x55,512);return failure==6 ? ESP_FAIL : ESP_OK;}
void *heap_caps_aligned_alloc(size_t alignment,size_t size,unsigned caps){assert(alignment==64 && size==512 && caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA));return failure==5 ? NULL : sector;}
void heap_caps_free(void *buffer){assert(buffer==sector);++freed;}
const char *esp_err_to_name(esp_err_t error){return error ? "TEST_ERROR" : "ESP_OK";}
void ouse_board_sd_probe(void);
int main(int argc,char **argv){
 assert(argc==2);failure=atoi(argv[1]);ouse_board_sd_probe();
 assert(published==2 && !result.probing);
 assert(result.detected==(failure==0 || failure>=5));
 assert(result.readable==(failure==0));
 assert(notifications==(failure!=0));
 assert(reads==(failure==0 || failure==6));
 assert(freed==reads);
 assert(removed==(failure>=4));
 assert(power_deleted==(failure>=2));
 if(result.detected){assert(!strcmp(result.name,"1234567"));assert(result.capacity_bytes==(uint64_t)8388608*result.sector_bytes);}
 puts("PASS SD read-only probe, truthful state, failure notification and cleanup");return 0;
}
'''
(OUT / "check.c").write_text(fixture)
vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
vs = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires",
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
script = OUT / "environment.cmd"
script.write_text(f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n', encoding="mbcs")
env = dict(os.environ)
for line in subprocess.check_output(["cmd.exe", "/d", "/c", str(script)], encoding="mbcs").splitlines():
    if "=" in line and not line.startswith("="):
        key, value = line.split("=", 1)
        env[key] = value
compiler = Path(env["VCToolsInstallDir"]) / "bin/Hostx64/x64/cl.exe"
subprocess.run([str(compiler), "/nologo", "/utf-8", "/std:c11", "/UNDEBUG", "/I"+str(OUT),
    str(ROOT/"boards/waveshare_lcd35/board_sd.c"), str(OUT/"check.c"), "/Fe"+str(OUT/"check.exe")],
    cwd=OUT, env=env, check=True)
for case in range(8):
    subprocess.run([str(OUT/"check.exe"), str(case)], check=True)
