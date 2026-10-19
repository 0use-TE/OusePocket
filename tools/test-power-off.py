"""Exercise PMIC shutdown and full-registry close with mocked hardware; never powers off a device."""
from pathlib import Path
import os
import subprocess

root = Path(__file__).resolve().parents[1]
output = root / '.tools/power-off-check'
output.mkdir(parents=True, exist_ok=True)
board = (root / 'boards/waveshare_lcd35/board_devices.c').read_text(encoding='utf-8')
shutdown = board[board.index('esp_err_t ouse_board_power_off('):board.index('static void sample_power(')]
source = (root / 'main/ouse_reliability.c').read_text(encoding='utf-8')
close = source[source.index('static void close_ui('):source.index('static esp_err_t close_request(')]
fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_FAIL -1
static bool read_ok,close_all,ui_done;
static unsigned writes,closed;
static uint8_t value;
static int pmic=1,result;
static char closing_app[48];
static bool read_reg(uint8_t address,uint8_t *bytes,size_t length){assert(address==0x10 && length==1);*bytes=value;return read_ok;}
static esp_err_t i2c_master_transmit(int device,const uint8_t *bytes,size_t length,int timeout){assert(device==pmic && length==2 && timeout==100 && bytes[0]==0x10 && bytes[1]==(value|1));++writes;return result;}
typedef struct {char id[48];} ouse_app_info_t;
static esp_err_t ouse_app_get_info(unsigned index,ouse_app_info_t *out){if(index>=41)return ESP_FAIL;if(!index)strcpy(out->id,"ouse.settings");else snprintf(out->id,sizeof(out->id),"user.app%u",index);return ESP_OK;}
static esp_err_t ouse_app_close_id(const char *id){assert(strcmp(id,"ouse.settings"));++closed;return ESP_OK;}
#define atomic_store(pointer,value) (*(pointer)=(value))
'''
tests = r'''
int main(void){
 assert(ouse_board_power_off()==ESP_ERR_INVALID_STATE && !writes);
 read_ok=true;value=0x24;assert(ouse_board_power_off()==ESP_OK && writes==1);
 result=ESP_FAIL;assert(ouse_board_power_off()==ESP_FAIL && writes==2);
 close_all=true;close_ui(NULL);assert(closed==40 && ui_done);
 close_all=false;strcpy(closing_app,"user.single");close_ui(NULL);assert(closed==41);
 puts("PASS shutdown: absent PMIC refusal, exact I2C read-modify-write/error propagation, every installed app closed beyond 24, settings retained");return 0;
}
'''
(output / 'check.c').write_text(fixture + shutdown + close + tests, encoding='utf-8')
setup = root / '.tools/ouse-system-check/environment.cmd'
assert setup.exists(), 'Run tools/test-ouse-system.py first'
environment = dict(os.environ)
for line in subprocess.check_output(['cmd.exe', '/d', '/c', str(setup)], encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key, value = line.split('=', 1)
        environment[key] = value
compiler = Path(environment['VCToolsInstallDir']) / 'bin/Hostx64/x64/cl.exe'
subprocess.run([str(compiler), '/nologo', '/utf-8', '/std:c11', '/UNDEBUG', '/D_CRT_SECURE_NO_WARNINGS', str(output / 'check.c'), '/Fe' + str(output / 'check.exe')], cwd=output, env=environment, check=True)
subprocess.run([str(output / 'check.exe')], check=True)
