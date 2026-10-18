"""Validate the diagnostic HCI parser without a controller or C6 firmware writes."""
from pathlib import Path
import os
import subprocess
root=Path(__file__).resolve().parents[1]
output=root/'.tools/ble-probe-check'
output.mkdir(parents=True,exist_ok=True)
source=(root/'boards/waveshare_lcd35/board_ble_probe.c').read_text(encoding='utf-8')
function=source[source.index('static portMUX_TYPE'):source.index('static esp_err_t command(')]
fixture=r'''
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define ESP_OK 0
'''
tests=r'''
int main(void){
    assert(hci_rx_handler(NULL,0)==ESP_OK);
    uint8_t version[]={4,0x0e,12,1,1,0x10,0,12,0,0,0,0,0,0,0};
    hci_rx_handler(version,sizeof(version));assert(!probe.done);
    probe.active=true;probe.opcode=0x1001;
    for(size_t size=0;size<sizeof(version);++size){hci_rx_handler(version,size);assert(!probe.done);}
    hci_rx_handler(version,sizeof(version));assert(probe.done&&probe.version==12&&!probe.status);
    probe.done=false;probe.opcode=0x2003;hci_rx_handler(version,sizeof(version));assert(!probe.done);
    uint8_t features[]={4,0x0e,12,1,3,0x20,0,1,2,3,4,5,6,7,8};hci_rx_handler(features,sizeof(features));
    assert(probe.done&&probe.features[0]==1&&probe.features[7]==8);
    uint8_t status[]={4,0x0f,4,0x0c,1,0x0c,0x20};probe.opcode=0x200c;probe.done=false;
    hci_rx_handler(status,sizeof(status));assert(probe.done&&probe.status==0x0c);
    uint8_t reports[]={4,0x3e,2,2,3};hci_rx_handler(reports,sizeof(reports));assert(probe.advertisements==3);
    probe.active=false;hci_rx_handler(reports,sizeof(reports));assert(probe.advertisements==3);
    puts("PASS diagnostic HCI parser: truncated/mismatched/inactive events ignored, version/features/status/report parsing, no C6 firmware write path");return 0;
}
'''
assert 'ota' not in source.lower() and 'flash' not in source.lower()
assert 'command(0x0c03' not in source
(output/'check.c').write_text(fixture+function+tests,encoding='utf-8')
setup=root/'.tools/ouse-system-check/environment.cmd'
assert setup.exists(),'Run tools/test-ouse-system.py first'
environment=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(setup)],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1);environment[key]=value
compiler=Path(environment['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
subprocess.run([str(compiler),'/nologo','/std:c11','/UNDEBUG',str(output/'check.c'),'/Fe'+str(output/'check.exe')],cwd=output,env=environment,check=True)
subprocess.run([str(output/'check.exe')],cwd=output,check=True)
