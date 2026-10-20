"""Exercise production BLE command policy with deterministic NimBLE/NVS doubles."""
from pathlib import Path
import os
import subprocess
root=Path(__file__).resolve().parents[1]
output=root/'.tools/ble-service-check';output.mkdir(parents=True,exist_ok=True)
source=(root/'components/ouse_ble/ouse_ble.c').read_text(encoding='utf-8')
types=source[source.index('enum {ENABLE'):source.index('static QueueHandle_t')]
parse=source[source.index('static bool address_parse('):source.index('static void error_set(')]
clock=source[source.index('static uint32_t clock_ms('):source.index('static void address_text(')]
process=source[source.index('static void process('):source.index('static void worker(')]
fixture=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ouse_ble.h"
#include "ouse_ble_app.h"
#define BLE_HS_CONN_HANDLE_NONE 65535
#define BLE_HS_ENOTSYNCED 22
#define BLE_HS_EALREADY 2
#define BLE_HS_EINVAL 3
#define BLE_ERR_REM_USER_CONN_TERM 19
#define BLE_SM_IOACT_NUMCMP 5
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
typedef struct {uint8_t type,val[6];} ble_addr_t;
struct ble_gap_disc_params {int passive,itvl,window,filter_duplicates;};
struct ble_sm_io {int action,numcmp_accept;};
static ouse_ble_state_t state;
static bool initialized,synced,enabled,initiating,discovering,secure_connect,reconnect_paused;
static uint8_t own_type;
static uint16_t connection=BLE_HS_CONN_HANDLE_NONE;
static uint32_t pair_until,prompt_until,test_clock;
static int lock,init_error;
static unsigned saves,advertisements,disconnects,scans,confirmations,refreshes,unpairs;
static bool saved,accepted,advertising;
static void hid_release(void){}
static int64_t esp_timer_get_time(void){return (int64_t)test_clock*1000;}
static void changed(void){++state.revision;}
static int initialize(void){if(!init_error){initialized=true;synced=true;state.ready=true;state.starting=false;}return init_error;}
static int advertise(void){if(reconnect_paused)return 0;advertising=true;++advertisements;return 0;}
static void save_enabled(bool value){saved=value;++saves;}
static void error_set(int error){state.error=error;}
static void bonds_refresh(void){++refreshes;}
static int ble_gap_disc_cancel(void){discovering=false;return 0;}
static int ble_gap_adv_stop(void){advertising=false;return 0;}
static int ble_gap_conn_cancel(void){return 0;}
static int ble_gap_terminate(uint16_t handle,int reason){assert(handle!=BLE_HS_CONN_HANDLE_NONE&&reason==19);++disconnects;return 0;}
static bool ble_gap_disc_active(void){return discovering;}
static int gap_event(void *event,void *argument){(void)event;(void)argument;return 0;}
static int ble_gap_disc(uint8_t type,int timeout,const struct ble_gap_disc_params *parameters,void *callback,void *context){(void)type;(void)callback;(void)context;assert(timeout==10000&&parameters->filter_duplicates==1&&!parameters->passive);discovering=true;++scans;return 0;}
static int ble_gap_connect(uint8_t type,const ble_addr_t *address,int timeout,void *parameters,void *callback,void *context){(void)type;(void)address;(void)parameters;(void)callback;(void)context;assert(timeout==10000);return 0;}
static int ble_gap_unpair(const ble_addr_t *address){assert(address->type<=3&&!advertising&&!discovering);++unpairs;return 0;}
static int ble_sm_inject_io(uint16_t handle,const struct ble_sm_io *input){assert(handle!=65535&&input->action==5);accepted=input->numcmp_accept!=0;++confirmations;return 0;}
'''
tests=r'''
int main(void){
    ble_addr_t address;assert(address_parse("01:23:45:67:89:AB",0,&address)&&address.val[0]==0xab&&address.val[5]==1);
    assert(address_parse("01:23:45:67:89:AB",2,&address)&&address.type==2);
    assert(!address_parse("bad",0,&address)&&!address_parse("01:23:45:67:89:AB",4,&address));
    test_clock=0xfffffff0;assert(before(20));test_clock=21;assert(!before(20));test_clock=100;
    command_t command={.operation=SCAN};process(&command);assert(state.error==BLE_HS_ENOTSYNCED&&!scans);
    command=(command_t){.operation=ENABLE,.enabled=true};process(&command);assert(enabled&&state.enabled&&state.ready&&saved&&saves==1&&advertisements==1&&before(pair_until));
    command=(command_t){.operation=SCAN};process(&command);assert(state.scanning&&scans==1);process(&command);assert(scans==1);
    command=(command_t){.operation=FORGET};process(&command);assert(unpairs==1&&!discovering&&advertising);
    connection=7;state.pair_pending=true;state.pair_id=9;prompt_until=200;
    command=(command_t){.operation=CONFIRM,.enabled=true,.pair_id=8};process(&command);assert(state.pair_pending&&!confirmations);
    command.pair_id=9;process(&command);assert(!state.pair_pending&&accepted&&confirmations==1);
    state.pair_pending=true;state.pair_id=10;prompt_until=99;command.pair_id=10;process(&command);assert(!state.pair_pending&&!accepted&&confirmations==2);
    state.pair_pending=true;state.pair_id=11;prompt_until=200;command.pair_id=11;command.enabled=false;process(&command);assert(!accepted&&confirmations==3);
    command=(command_t){.operation=ENABLE,.temporary=true};process(&command);assert(!enabled&&!state.enabled&&!state.scanning&&!state.advertising&&disconnects==1&&saves==1);
    connection=65535;command=(command_t){.operation=ENABLE,.enabled=true,.temporary=true};process(&command);assert(enabled&&!pair_until&&saves==1);
    connection=7;state.pair_pending=true;command=(command_t){.operation=DISCONNECT};process(&command);
    assert(reconnect_paused&&!advertising&&!state.advertising&&!state.pair_pending&&!pair_until&&!prompt_until&&disconnects==2);
    connection=65535;unsigned prior_ads=advertisements;
    for(unsigned n=0;n<10;n++)advertise();assert(advertisements==prior_ads&&!advertising);
    command=(command_t){.operation=SCAN};process(&command);assert(reconnect_paused);
    command=(command_t){.operation=DISCOVER};process(&command);assert(!reconnect_paused&&advertising&&before(pair_until));
    command=(command_t){.operation=DISCONNECT};process(&command);assert(reconnect_paused);
    command=(command_t){.operation=CONNECT};process(&command);assert(!reconnect_paused&&initiating);
    command=(command_t){.operation=DISCONNECT};process(&command);
    command=(command_t){.operation=ENABLE,.enabled=true};process(&command);assert(!reconnect_paused&&saves==2);
    command=(command_t){.operation=ENABLE};process(&command);assert(!enabled&&!saved&&saves==3);
    initialized=false;synced=false;init_error=0x101;command.enabled=true;process(&command);assert(!enabled&&!state.enabled&&!state.starting&&state.error==0x101&&!saved);
    puts("PASS real BLE command policy: explicit enable, bounded scan, stale pairing ID isolation, accept/reject/expiry, temporary shutdown keeps saved preference, failed init disables safely, address and wrap-safe deadlines");return 0;
}
'''
(output/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n',encoding='utf-8')
(output/'ouse_tasks.h').write_text('#pragma once\n#include <stdint.h>\n#include <stddef.h>\ntypedef struct {char app_id[48];uint32_t instance;} ouse_task_owner_t;\n',encoding='utf-8')
(output/'check.c').write_text(fixture+types+'\nstatic void app_process(command_t *command){(void)command;}\n'+clock+parse+process+tests,encoding='utf-8')
assert 'sm_sc_only=1' in source and 'sm_mitm=1' in source
assert 'BLE_GATT_CHR_F_READ_AUTHEN' in source and 'BLE_GATT_CHR_F_WRITE_AUTHEN' in source
assert 'CONFIG_BT_NIMBLE_CRYPTO_STACK_MBEDTLS=y' in (root/'sdkconfig.defaults').read_text(encoding='utf-8')
assert 'nvs_flash_erase' not in source and 'ota' not in source.lower()
assert 'if(reconnect_paused || !enabled' in source
environment=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(root/'.tools/ouse-kernel-check/environment.cmd')],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1);environment[key]=value
compiler=Path(environment['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
subprocess.run([str(compiler),'/nologo','/std:c11','/utf-8','/UNDEBUG','/I'+str(output),'/I'+str(root/'components/ouse_ble/include'),str(output/'check.c'),'/Fe'+str(output/'check.exe')],cwd=output,env=environment,check=True)
subprocess.run([str(output/'check.exe')],cwd=output,check=True)
