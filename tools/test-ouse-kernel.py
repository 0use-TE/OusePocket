"""Focused native state-machine/budget checks. No serial, network or device access."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".tools/ouse-kernel-check"
OUT.mkdir(parents=True, exist_ok=True)
vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
vs = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires",
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
setup = OUT / "environment.cmd"
setup.write_text(f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n', encoding="mbcs")
env = dict(os.environ)
for line in subprocess.check_output(["cmd.exe", "/d", "/c", str(setup)], encoding="mbcs").splitlines():
    if "=" in line and not line.startswith("="):
        key, value = line.split("=", 1)
        env[key] = value
compiler = Path(env["VCToolsInstallDir"]) / "bin/Hostx64/x64/cl.exe"
headers = {
    "esp_err.h": """#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
""",
    "esp_timer.h": "#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n",
    "freertos/FreeRTOS.h": """#pragma once
#include <windows.h>
typedef SRWLOCK portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED SRWLOCK_INIT
#define portENTER_CRITICAL(lock) AcquireSRWLockExclusive(lock)
#define portEXIT_CRITICAL(lock) ReleaseSRWLockExclusive(lock)
#define configGENERATE_RUN_TIME_STATS 1
#define pdFALSE 0
""",
    "freertos/task.h": """#pragma once
#include <stdint.h>
typedef struct {uint64_t ulRunTimeCounter;} TaskStatus_t;
#define eInvalid 0
void vTaskDelay(unsigned ticks);
void vTaskGetInfo(void *task,TaskStatus_t *out,int stack,int state);
""",
}
for name, content in headers.items():
    path = OUT / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ouse_tasks.h"
#include "ouse_device_driver.h"
#include "ouse_budget.h"
#include "freertos/task.h"
static int64_t wall;
static uint64_t allocated,switched;
int64_t esp_timer_get_time(void){return wall;}
void vTaskDelay(unsigned ticks){switched=allocated;wall+=ticks*1000;}
void vTaskGetInfo(void *task,TaskStatus_t *out,int stack,int state){(void)task;(void)stack;(void)state;out->ulRunTimeCounter=switched;}
static void run(unsigned us){wall+=us;allocated+=us;}
static unsigned notices;
static void notified(void){++notices;}
static unsigned locked;
static void lock(void){assert(!locked);locked=1;}
static void unlock(void){assert(locked);locked=0;}
static void notify(unsigned worker){assert(!locked && worker<3);}
static void registry_tests(void){
    ouse_task_owner_t a,b;
    assert(ouse_task_owner_open("user.same",&a)==ESP_OK);
    assert(ouse_task_owner_open("user.other",&b)==ESP_OK);
    uint32_t id,ids[OUSE_TASK_OWNER_LIMIT];ouse_task_snapshot_t snapshot;
    assert(ouse_task_create(&a,OUSE_TASK_AUDIO,"record",OUSE_TASK_STOP_ON_CLOSE,true,&id)==ESP_OK);
    assert(ouse_task_start(id,true));
    assert(ouse_task_cancel(&b,id,"foreign")==ESP_ERR_NOT_FOUND);
    assert(ouse_task_cancel(&a,id,"stop")==ESP_OK);
    assert(ouse_task_get(&a,id,&snapshot) && snapshot.state==OUSE_TASK_CANCELLING && !snapshot.finished_us);
    run(250);ouse_task_finish(id,ESP_OK,true);
    assert(ouse_task_get(&a,id,&snapshot) && snapshot.state==OUSE_TASK_CANCELLED && snapshot.delivery_pending);
    ouse_task_ack(&a,id);
    assert(ouse_task_create(&a,OUSE_TASK_STORAGE,"set",OUSE_TASK_FINISH_ON_CLOSE,true,&id)==ESP_OK);
    assert(ouse_task_start(id,false));
    assert(ouse_task_cancel(&a,id,"stop")==ESP_ERR_NOT_SUPPORTED);
    uint32_t old=id;ouse_task_owner_close(&a);
    assert(ouse_task_create(&a,OUSE_TASK_STORAGE,"set",OUSE_TASK_FINISH_ON_CLOSE,true,&id)==ESP_ERR_INVALID_STATE);
    assert(ouse_task_owner_open("user.same",&a)==ESP_OK);
    ouse_task_finish(old,ESP_OK,false);
    assert(!ouse_task_get(&a,old,&snapshot));
    assert(ouse_task_get(NULL,old,&snapshot) && snapshot.detached && !snapshot.delivery_pending);
    for(unsigned i=0;i<OUSE_TASK_OWNER_LIMIT;++i){
        assert(ouse_task_create(&a,OUSE_TASK_STORAGE,"get",OUSE_TASK_FINISH_ON_CLOSE,true,&ids[i])==ESP_OK);
        assert(ouse_task_start(ids[i],false));ouse_task_finish(ids[i],ESP_OK,false);
    }
    // Undelivered terminal events remain pinned, just like active work.
    assert(ouse_task_create(&a,OUSE_TASK_AUDIO,"test",OUSE_TASK_STOP_ON_CLOSE,true,&id)==ESP_ERR_NO_MEM);
    for(unsigned i=0;i<OUSE_TASK_OWNER_LIMIT;++i){ouse_task_hold_result(ids[i],true);ouse_task_ack(&a,ids[i]);}
    assert(ouse_task_create(&a,OUSE_TASK_AUDIO,"test",OUSE_TASK_STOP_ON_CLOSE,true,&id)==ESP_ERR_NO_MEM);
    for(unsigned i=0;i<OUSE_TASK_OWNER_LIMIT;++i)ouse_task_hold_result(ids[i],false);
    for(unsigned i=0;i<OUSE_TASK_CAPACITY+4;++i){
        assert(ouse_task_create(&a,OUSE_TASK_AUDIO,"test",OUSE_TASK_STOP_ON_CLOSE,true,&id)==ESP_OK);
        assert(ouse_task_cancel(&a,id,"queued")==ESP_OK);
        assert(!ouse_task_start(id,true));ouse_task_finish(id,ESP_OK,true);ouse_task_ack(&a,id);
    }
    ouse_task_stats_t stats;ouse_tasks_get_stats(&stats);
    assert(stats.records==OUSE_TASK_CAPACITY && stats.recycled && !stats.active && !stats.pending_delivery);
    ouse_task_owner_close(&a);ouse_task_owner_close(&b);
    assert(notices);
    puts("PASS registry: ownership, cancel acknowledgement, close/reopen isolation, delivery pinning, bounded recycling");
}
static void device_tests(void){
    ouse_device_port_t port={lock,unlock,notify};ouse_device_config_t cfg={.volume=40,.sleep_seconds=60,.wifi_enabled=true};
    assert(ouse_devices_init(&port,&cfg)==ESP_OK);ouse_devices_set_available(1,true,ESP_OK);ouse_devices_set_available(2,true,ESP_OK);
    ouse_task_owner_t owner;assert(ouse_task_owner_open("user.device",&owner)==ESP_OK);
    uint32_t first,second;ouse_audio_request_t request;ouse_task_snapshot_t task;
    assert(ouse_audio_submit(&owner,OUSE_AUDIO_TEST,0,&first)==ESP_OK);
    assert(ouse_devices_take_overlay(&request) && request.id==first && ouse_task_start(first,true));
    assert(ouse_audio_submit(&owner,OUSE_AUDIO_CAPTURE,1,&second)==ESP_OK);
    assert(!ouse_devices_audio_current(first));
    assert(ouse_task_get(&owner,first,&task) && task.state==OUSE_TASK_CANCELLING);
    ouse_task_finish(first,ESP_OK,true); // fake driver confirms codec closure
    assert(ouse_devices_take_audio(&request) && request.id==second && ouse_task_start(second,true));
    ouse_task_owner_close(&owner);assert(!ouse_devices_audio_current(second));ouse_task_finish(second,ESP_OK,true);
    assert(ouse_task_owner_open("user.device",&owner)==ESP_OK);
    assert(ouse_wifi_submit(&owner,OUSE_WIFI_SCAN,false,NULL,NULL,&first)==ESP_OK);
    assert(ouse_wifi_submit(&owner,OUSE_WIFI_CONNECT,false,"test","password",&second)==ESP_ERR_INVALID_STATE);
    ouse_wifi_request_t wifi;assert(ouse_devices_take_wifi(&wifi) && wifi.id==first && ouse_task_start(first,false));
    assert(ouse_task_cancel(&owner,first,"cancel")==ESP_ERR_NOT_SUPPORTED);
    ouse_task_owner_close(&owner);ouse_task_finish(first,ESP_OK,false);
    assert(ouse_task_get(NULL,first,&task) && task.detached && task.state==OUSE_TASK_SUCCEEDED);
    puts("PASS devices: audio cooperative stop and close, Wi-Fi serialization and detached completion");
}
static void focus_tests(void){
    ouse_task_owner_t media,alarm={.app_id="ouse.alarm"};uint32_t song,ring,other;ouse_audio_request_t request;ouse_task_snapshot_t task;
    assert(ouse_task_owner_open("user.player",&media)==ESP_OK);
    assert(ouse_task_create(&media,OUSE_TASK_AUDIO,"play",OUSE_TASK_FINISH_ON_CLOSE,true,&song)==ESP_OK);
    assert(ouse_devices_queue_media(song)==ESP_OK);
    assert(ouse_audio_submit(&media,OUSE_AUDIO_TEST,0,&other)==ESP_OK);
    assert(ouse_audio_submit(&alarm,OUSE_AUDIO_RING,60,&ring)==ESP_OK);
    assert(!ouse_task_cancel_requested(song)); // queued consumer preserved
    assert(ouse_devices_take_audio(&request)&&request.id==song&&ouse_task_start(song,true));
    assert(ouse_devices_take_overlay(&request)&&request.id==ring&&ouse_task_start(ring,true));
    assert(ouse_audio_submit(&media,OUSE_AUDIO_TEST,0,&other)==ESP_ERR_INVALID_STATE);
    ouse_audio_state_t state;ouse_audio_get(&state);assert(state.request_id==song&&state.alert_active);
    ouse_task_finish(ring,ESP_OK,false);
    assert(ouse_devices_audio_current(song));
    assert(ouse_audio_submit(&alarm,OUSE_AUDIO_RING,60,&ring)==ESP_OK);
    assert(ouse_devices_take_overlay(&request)&&request.id==ring&&ouse_task_start(ring,true));
    assert(ouse_task_get(&media,song,&task)&&task.state==OUSE_TASK_RUNNING&&!task.cancel_requested);
    ouse_task_finish(ring,ESP_OK,false);
    assert(!ouse_devices_take_audio(&request)&&ouse_devices_audio_current(song));
    ouse_task_owner_close(&media);assert(!ouse_task_cancel_requested(song));ouse_task_finish(song,ESP_OK,false);
    puts("PASS focus: queued/running media remain current while alarm overlay plays, short commands cannot steal ring, no resumed/replaced consumer or JS lifetime dependency");
    uint32_t usb;
    assert(ouse_devices_queue_usb(&usb)==ESP_OK);
    assert(ouse_devices_take_audio(&request) && request.action==OUSE_AUDIO_USB && request.id==usb);
    assert(ouse_task_start(usb,true));
    assert(ouse_audio_submit(NULL,OUSE_AUDIO_TEST,0,&other)==ESP_ERR_INVALID_STATE);
    assert(ouse_audio_submit(NULL,OUSE_AUDIO_CAPTURE,1,&other)==ESP_ERR_INVALID_STATE);
    assert(ouse_audio_submit(NULL,OUSE_AUDIO_USB,0,&other)==ESP_ERR_INVALID_ARG);
    assert(ouse_task_create(&alarm,OUSE_TASK_AUDIO,"other",OUSE_TASK_STOP_ON_CLOSE,true,&other)==ESP_OK);
    assert(ouse_devices_queue_media(other)==ESP_ERR_INVALID_STATE);ouse_task_finish(other,ESP_OK,true);
    assert(ouse_audio_submit(&alarm,OUSE_AUDIO_RING,2,&ring)==ESP_OK);
    assert(ouse_task_get(NULL,usb,&task) && task.state==OUSE_TASK_CANCELLING && !strcmp(task.cancel_reason,"alarm_preempt"));
    assert(ouse_devices_queue_usb(&other)==ESP_ERR_INVALID_STATE);
    ouse_task_finish(usb,ESP_OK,true); // codec cleanup acknowledgement
    assert(ouse_devices_take_audio(&request) && request.id==ring && ouse_task_start(ring,true));
    ouse_task_finish(ring,ESP_OK,false);
    assert(ouse_devices_queue_usb(&usb)==ESP_OK && ouse_devices_take_audio(&request));
    assert(ouse_task_start(usb,true));
    assert(ouse_audio_submit(NULL,OUSE_AUDIO_STOP,0,&other)==ESP_OK);
    assert(ouse_task_get(NULL,usb,&task) && task.state==OUSE_TASK_CANCELLING);
    ouse_task_finish(usb,ESP_OK,true);
    assert(ouse_devices_take_audio(&request) && request.id==other && ouse_task_start(other,true));
    ouse_task_finish(other,ESP_OK,false);
    puts("PASS USB focus: native-only admission, exclusive duplex, alarm preemption, cancellation requires codec acknowledgement");
}
static void budget_tests(void){
    ouse_budget_t budget;ouse_budget_begin(&budget,12000,false);
    run(1500);assert(!ouse_budget_exceeded(&budget) && budget.scheduled_us==1500);
    wall+=150000; // another task/blocked time; no JS-worker allocation
    assert(!ouse_budget_exceeded(&budget) && budget.scheduled_us==1500);
    assert(ouse_budget_slice_done(&budget)); // defer more callbacks without fault
    run(11000);assert(ouse_budget_exceeded(&budget) && !budget.wall_exceeded);
    ouse_budget_begin(&budget,4000,false);
    for(unsigned i=0;i<3;++i){run(1000);assert(!ouse_budget_exceeded(&budget));}
    run(1000);assert(ouse_budget_exceeded(&budget));
    ouse_budget_begin(&budget,12000,false);wall+=1000001;
    assert(ouse_budget_exceeded(&budget) && budget.wall_exceeded);
    // Crossing the old 32-bit microsecond boundary must not reset protection.
    allocated=UINT64_C(0xffffffff)-2000;ouse_budget_begin(&budget,4000,false);run(4500);
    assert(ouse_budget_exceeded(&budget) && budget.scheduled_us==4500);
    puts("PASS budget: preemption/blocked-time exclusion, foreground/background allocation limits, wall fuse, 64-bit counter");
}
int main(void){ouse_tasks_set_notify(notified);registry_tests();device_tests();focus_tests();budget_tests();return 0;}
'''
(OUT / "check.c").write_text(fixture, encoding="utf-8")
includes = [OUT, ROOT/"components/ouse_tasks/include", ROOT/"components/ouse_devices/include", ROOT/"components/ouse_runtime"]
sources = [OUT/"check.c", ROOT/"components/ouse_tasks/ouse_tasks.c", ROOT/"components/ouse_devices/ouse_devices.c", ROOT/"components/ouse_runtime/ouse_budget.c"]
command = [str(compiler), "/nologo", "/std:c11", "/O2", "/utf-8", "/D_CRT_SECURE_NO_WARNINGS", "/DOUSE_BUDGET_TEST_RTOS"]
command += ["/I"+str(p) for p in includes]
result = subprocess.run(command + list(map(str,sources)) + ["/Fe"+str(OUT/"check.exe")], cwd=OUT, env=env, capture_output=True, text=True, errors="replace")
assert result.returncode==0, (result.stdout,result.stderr)
subprocess.run([str(OUT/"check.exe")], cwd=OUT, check=True, timeout=15)
