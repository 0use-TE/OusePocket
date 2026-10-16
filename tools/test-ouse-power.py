"""Exercise the product power/PMIC and device persistence worker, without hardware I/O."""
from pathlib import Path
import os, subprocess
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/".tools/ouse-power-check"
OUT.mkdir(exist_ok=True)
assert (ROOT/".tools/ouse-core-check/esp_err.h").exists(), "Run test-ouse-core.py first"
vswhere=Path(os.environ["ProgramFiles(x86)"])/"Microsoft Visual Studio/Installer/vswhere.exe"
vs=subprocess.check_output([str(vswhere),"-latest","-products","*","-requires","Microsoft.VisualStudio.Component.VC.Tools.x86.x64","-property","installationPath"],text=True).strip()
script=OUT/"environment.cmd"
script.write_text(f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n',encoding="mbcs")
env=dict(os.environ)
for line in subprocess.check_output(["cmd.exe","/d","/c",str(script)],encoding="mbcs").splitlines():
 if "=" in line and not line.startswith("="):
  k,v=line.split("=",1);env[k]=v
compiler=Path(env["VCToolsInstallDir"])/"bin/Hostx64/x64/cl.exe"

headers=r'''
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(n) (n)
#define I2C_ADDR_BIT_LEN_7 7
#define NVS_READWRITE 1
#define NVS_READONLY 0
#define ESP_LOGW(...) ((void)0)
typedef int portMUX_TYPE;
typedef void *TaskHandle_t;
typedef void *i2c_master_dev_handle_t;
typedef void *i2c_master_bus_handle_t;
typedef int nvs_handle_t;
typedef struct {int dev_addr_length,device_address,scl_speed_hz;} i2c_device_config_t;
void *bsp_i2c_get_handle(void);
int i2c_master_probe(void *,int,int);
int i2c_master_bus_add_device(void *,const i2c_device_config_t *,void **);
int i2c_master_transmit_receive(void *,const void *,size_t,void *,size_t,int);
int nvs_open(const char *,int,nvs_handle_t *);
int nvs_set_u16(int,const char *,uint16_t);
int nvs_set_u8(int,const char *,uint8_t);
int nvs_set_str(int,const char *,const char *);
int nvs_get_u16(int,const char *,uint16_t *);
int nvs_get_u8(int,const char *,uint8_t *);
int nvs_get_str(int,const char *,char *,size_t *);
int nvs_commit(int);
void nvs_close(int);
void xTaskNotifyGive(void *);
int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,void**);
unsigned ulTaskNotifyTake(int,unsigned);
int64_t esp_timer_get_time(void);
const char *esp_err_to_name(int);
'''
(OUT/"fakes.h").write_text(headers)
for name in ["bsp/esp-bsp.h","driver/i2c_master.h","esp_log.h","esp_timer.h","nvs.h","freertos/FreeRTOS.h","freertos/task.h"]:
 f=OUT/name;f.parent.mkdir(exist_ok=True,parents=True);f.write_text('#include "fakes.h"\n')
fixture=r'''
#include "fakes.h"
#include "ouse_device_driver.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>
static jmp_buf end;
static int64_t now;
static unsigned loops,commits,power_samples,tasks,notifications;
static unsigned stored_volume;
static bool fail_commit,fail_read;
void *bsp_i2c_get_handle(void){return (void*)1;}
int i2c_master_probe(void *b,int addr,int ms){assert(b && addr==0x34 && ms<=100);return 0;}
int i2c_master_bus_add_device(void *b,const i2c_device_config_t *c,void **p){assert(b&&c->device_address==0x34 && c->scl_speed_hz==100000);*p=(void*)2;return 0;}
int i2c_master_transmit_receive(void *d,const void *reg,size_t rn,void *dst,size_t n,int ms){
 assert(d && rn==1 && ms==50);if(fail_read)return ESP_FAIL;
 uint8_t address=*(uint8_t*)reg,*bytes=dst;
 if(address==0){assert(n==1);++power_samples;*bytes=0x28;}
 else if(address==1){assert(n==1);*bytes=0x21;}
 else if(address==0xa4){assert(n==1);*bytes=73;}
 else {assert(address==0x34 && n==2);bytes[0]=4030>>8;bytes[1]=4030&255;}
 return 0;
}
int nvs_open(const char *ns,int mode,nvs_handle_t *h){assert(!strcmp(ns,"ouse_devices"));*h=mode?2:1;return 0;}
int nvs_set_u16(int h,const char *k,uint16_t v){assert(h==2 && !strcmp(k,"sleep") && v==30);return 0;}
int nvs_set_u8(int h,const char *k,uint8_t v){assert(h==2);if(!strcmp(k,"volume"))stored_volume=v;return 0;}
int nvs_set_str(int h,const char *k,const char *v){assert(h==2 && k && v);return 0;}
int nvs_get_u16(int h,const char *k,uint16_t *v){assert(h==1 && !strcmp(k,"sleep"));*v=30;return 0;}
int nvs_get_u8(int h,const char *k,uint8_t *v){assert(h==1);*v=!strcmp(k,"volume")?30:!strcmp(k,"muted")?1:0;return 0;}
int nvs_get_str(int h,const char *k,char *v,size_t *n){assert(h==1);const char *s=!strcmp(k,"ssid")?"Office":"abcdefgh";assert(*n>strlen(s));strcpy(v,s);return 0;}
int nvs_commit(int h){assert(h==2);++commits;return fail_commit ? ESP_FAIL : 0;}
void nvs_close(int h){assert(h==1||h==2);}
void xTaskNotifyGive(void *h){assert(h);++notifications;}
int xTaskCreate(void(*f)(void*),const char *name,unsigned stack,void *arg,unsigned prio,void **h){assert(f&&name&&stack>=4096&&prio>=2&&!arg);*h=(void*)(uintptr_t)(++tasks);return pdPASS;}
int64_t esp_timer_get_time(void){return now;}
const char *esp_err_to_name(int e){(void)e;return "test";}
void ouse_board_audio_worker(void *p){(void)p;}
void ouse_board_wifi_worker(void *p){(void)p;}
unsigned ulTaskNotifyTake(int clear,unsigned ms){
 assert(clear && (ms==200 || ms==2000));now+=200000;++loops;
 if(loops<=5){assert(!commits);assert(ouse_audio_set_volume(69+loops)==0);}
 if(loops==8){assert(commits==1 && stored_volume==74);fail_commit=true;assert(ouse_audio_set_volume(75)==0);}
 if(loops==12){ouse_device_save_state_t s;ouse_devices_save_state(&s);assert(commits==2 && !s.pending && s.error==ESP_FAIL);fail_commit=false;ouse_devices_retry_save();}
 if(loops==16){assert(commits==3 && power_samples==2 && stored_volume==75);longjmp(end,1);}
 return 0;
}
#include "BOARD_POWER"
int main(void){
 assert(ouse_board_devices_init()==0);ouse_device_config_t cfg;ouse_devices_get_config(&cfg);assert(cfg.volume==30 && cfg.muted && cfg.sleep_seconds==30 && !cfg.wifi_enabled && !strcmp(cfg.ssid,"Office"));
 ouse_board_start_services();assert(tasks==3);if(!setjmp(end))power_worker(NULL);
 ouse_power_state_t p;ouse_power_get(&p);assert(p.valid&&p.present&&p.external&&p.charging&&!p.full&&p.percent==73&&p.millivolts==4030);
 fail_read=true;sample_power();ouse_power_get(&p);assert(!p.valid && p.percent==-1 && p.error==ESP_ERR_TIMEOUT);
 ouse_device_save_state_t save;ouse_devices_save_state(&save);assert(!save.pending && save.error==ESP_OK && notifications);
 puts("PASS actual power worker: NVS restoration, shared I2C read-only AXP2101 decode, 2s sampling, quiet-period autosave, failure/retry and unknown telemetry");return 0;
}
'''.replace("BOARD_POWER",(ROOT/"boards/waveshare_lcd35/board_devices.c").as_posix())
(OUT/"check.c").write_text(fixture,encoding="utf-8")
result=subprocess.run([str(compiler),"/nologo","/utf-8","/std:c11","/O2","/MD","/UNDEBUG","/I"+str(OUT),"/I"+str(ROOT/".tools/ouse-core-check"),"/I"+str(ROOT/"components/ouse_devices/include"),str(OUT/"check.c"),str(ROOT/"components/ouse_devices/ouse_devices.c"),"/Fe"+str(OUT/"check.exe")],cwd=OUT,env=env,capture_output=True,text=True,errors="replace")
assert result.returncode==0,(result.stdout,result.stderr)
subprocess.run([str(OUT/"check.exe")],cwd=OUT,check=True)
