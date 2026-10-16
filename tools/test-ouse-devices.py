"""Exercise the product service mailboxes and snapshots, without hardware I/O."""
from pathlib import Path
import os, subprocess
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/".tools/ouse-devices-check"
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
fixture=r'''
#include "ouse_device_driver.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static unsigned locked,notifications[3];
static void lock(void){assert(!locked);++locked;}
static void unlock(void){assert(locked==1);--locked;}
static void notify(unsigned n){assert(!locked && n<3);++notifications[n];}
int main(void){
 ouse_device_port_t port={lock,unlock,notify};ouse_device_config_t cfg={.sleep_seconds=60,.volume=40};
 assert(ouse_devices_init(NULL,&cfg)==ESP_ERR_INVALID_ARG);
 assert(ouse_devices_init(&port,&cfg)==ESP_OK);
 assert(ouse_devices_init(&port,&cfg)==ESP_ERR_INVALID_STATE);
 assert(ouse_wifi_scan()==ESP_ERR_NOT_SUPPORTED && ouse_audio_test_tone()==ESP_ERR_NOT_SUPPORTED);
 ouse_devices_set_available(1,true,ESP_OK);ouse_devices_set_available(2,true,ESP_OK);
 assert(ouse_power_set_timeout(14)==ESP_ERR_INVALID_ARG && ouse_power_set_timeout(30)==ESP_OK);
 assert(ouse_audio_set_volume(101)==ESP_ERR_INVALID_ARG && ouse_audio_set_volume(77)==ESP_OK && ouse_audio_set_muted(true)==ESP_OK);
 uint32_t id,oldid;assert(ouse_devices_take_save(&cfg,&id));assert(cfg.volume==77 && cfg.muted && cfg.sleep_seconds==30);
 assert(!ouse_devices_take_save(&cfg,&oldid));oldid=id;
 assert(ouse_audio_set_volume(78)==ESP_OK);ouse_devices_saved(oldid,ESP_FAIL);
 ouse_device_save_state_t save;ouse_devices_save_state(&save);assert(save.pending && save.error==ESP_OK);
 assert(ouse_devices_take_save(&cfg,&id) && id!=oldid);ouse_devices_saved(id,ESP_FAIL);
 ouse_devices_save_state(&save);assert(!save.pending && save.error==ESP_FAIL);
 ouse_devices_retry_save();assert(ouse_devices_take_save(&cfg,&id));ouse_devices_saved(id,ESP_OK);
 ouse_power_state_t power={.available=true,.valid=true,.present=true,.percent=73,.millivolts=4000};
 ouse_power_set_asleep(true);ouse_devices_publish_power(&power);ouse_power_get(&power);
 assert(power.asleep && power.percent==73 && power.sleep_seconds==30);ouse_power_set_asleep(false);
 assert(ouse_audio_record(4)==ESP_ERR_INVALID_ARG && ouse_audio_play_recording()==ESP_ERR_INVALID_STATE);
 assert(ouse_audio_record(3)==ESP_OK);ouse_audio_request_t audio;assert(ouse_devices_take_audio(&audio) && audio.seconds==3);
 ouse_audio_state_t a;ouse_audio_get(&a);a.mode=OUSE_AUDIO_RECORDING;a.recorded_ms=1500;ouse_devices_publish_audio(&a,audio.id);
 assert(ouse_audio_stop()==ESP_OK && !ouse_devices_audio_current(audio.id));
 a.mode=OUSE_AUDIO_ERROR;a.error=ESP_FAIL;ouse_devices_publish_audio(&a,audio.id);ouse_audio_get(&a);assert(a.error==ESP_OK);
 assert(ouse_devices_take_audio(&audio) && audio.action==OUSE_AUDIO_STOP);a.mode=OUSE_AUDIO_IDLE;a.completed_id=audio.id;ouse_devices_publish_audio(&a,audio.id);
 assert(ouse_audio_play_recording()==ESP_OK);assert(ouse_devices_take_audio(&audio) && audio.action==OUSE_AUDIO_REPLAY);
 assert(ouse_wifi_scan()==ESP_ERR_INVALID_STATE && ouse_wifi_set_enabled(true)==ESP_OK);
 ouse_wifi_request_t wifi;assert(ouse_devices_take_wifi(&wifi) && wifi.enabled);
 assert(ouse_wifi_connect("Office","short")==ESP_ERR_INVALID_ARG);
 assert(ouse_wifi_connect("Office","abcdefgh")==ESP_OK);assert(ouse_wifi_scan()==ESP_ERR_INVALID_STATE);
 assert(ouse_devices_take_wifi(&wifi) && !strcmp(wifi.password,"abcdefgh"));
 ouse_devices_get_config(&cfg);assert(!cfg.ssid[0]); // unsucceeded credentials never persist
 ouse_wifi_state_t w={.ready=true,.connected=true,.count=1};strcpy(w.ssid,"Office");strcpy(w.ip,"192.168.1.7");
 strcpy(w.aps[0].ssid,"Office");w.aps[0].secured=true;w.completed_id=wifi.id;
 ouse_devices_publish_wifi(&w,wifi.id);ouse_devices_remember_wifi(&wifi);
 ouse_devices_get_config(&cfg);assert(!strcmp(cfg.ssid,"Office") && !strcmp(cfg.password,"abcdefgh"));
 assert(ouse_wifi_scan()==ESP_OK);ouse_wifi_request_t scan;assert(ouse_devices_take_wifi(&scan));
 assert(ouse_wifi_set_enabled(false)==ESP_OK);w.error=ESP_FAIL;ouse_devices_publish_wifi(&w,scan.id);
 ouse_wifi_get(&w);assert(!w.enabled && !w.connected && !w.ip[0] && w.error==ESP_OK);
 assert(ouse_wifi_forget()==ESP_OK);assert(ouse_devices_take_wifi(&wifi) && wifi.action==OUSE_WIFI_FORGET && !wifi.enabled);
 ouse_devices_get_config(&cfg);assert(!cfg.ssid[0] && !cfg.password[0]);
 assert(!locked && notifications[0] && notifications[1] && notifications[2]);
 puts("PASS cached power, timeout, volume/mute, short recording, cancellation, coalesced persistence, stale completions, Wi-Fi credentials and off/forget");
 return 0;
}
'''
(OUT/"check.c").write_text(fixture,encoding="utf-8")
result=subprocess.run([str(compiler),"/nologo","/utf-8","/std:c11","/O2","/MD","/UNDEBUG","/I"+str(ROOT/".tools/ouse-core-check"),"/I"+str(ROOT/"components/ouse_devices/include"),str(OUT/"check.c"),str(ROOT/"components/ouse_devices/ouse_devices.c"),"/Fe"+str(OUT/"check.exe")],cwd=OUT,env=env,capture_output=True,text=True,errors="replace")
assert result.returncode==0,(result.stdout,result.stderr)
subprocess.run([str(OUT/"check.exe")],cwd=OUT,check=True)
