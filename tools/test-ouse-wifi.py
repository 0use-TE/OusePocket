"""Exercise the product actual Wi-Fi worker, with deterministic SDK events, without hardware I/O."""
from pathlib import Path
import os, subprocess, sys
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/".tools/ouse-wifi-check"
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
#include <time.h>
#define ESP_ERR_WIFI_NOT_CONNECT 0x300f
#define ESP_ERR_WIFI_NOT_INIT 0x3001
#define ESP_ERR_WIFI_NOT_STARTED 0x3002
#define WIFI_REASON_ASSOC_LEAVE 8
#define WIFI_REASON_AUTH_FAIL 202
#define WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT 15
#define WIFI_REASON_HANDSHAKE_TIMEOUT 204
#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_AUTH_OPEN 0
#define WIFI_MODE_STA 1
#define WIFI_IF_STA 0
#define WIFI_STORAGE_RAM 1
#define WIFI_EVENT_STA_STOP 6
#define WIFI_EVENT_SCAN_DONE 1
#define WIFI_EVENT_STA_DISCONNECTED 2
#define IP_EVENT_STA_GOT_IP 3
#define ESP_HOSTED_EVENT_TRANSPORT_DOWN 4
#define ESP_HOSTED_EVENT_TRANSPORT_FAILURE 5
#define ESP_EVENT_ANY_ID -1
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(p) ((p)->addr&255),(((p)->addr>>8)&255),(((p)->addr>>16)&255),(((p)->addr>>24)&255)
#define WIFI_INIT_CONFIG_DEFAULT() {0}
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(s) {s}
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdTRUE 1
#define portMAX_DELAY 0xffffffff
#define pdMS_TO_TICKS(n) (n)
#define setenv mock_setenv
#define tzset mock_tzset
typedef int portMUX_TYPE;
typedef void *TaskHandle_t;
typedef const char *esp_event_base_t;
extern const char *WIFI_EVENT,*IP_EVENT,*ESP_HOSTED_EVENT;
typedef struct {uint32_t addr;} esp_ip4_addr_t;
typedef struct {struct{esp_ip4_addr_t ip;} ip_info;} ip_event_got_ip_t;
typedef struct {uint8_t ssid[32],ssid_len;int reason;} wifi_event_sta_disconnected_t;
typedef struct {unsigned major1,minor1,patch1;} esp_hosted_coprocessor_fwver_t;
typedef struct {int unused;} esp_netif_t;
typedef struct {int unused;} wifi_init_config_t;
typedef struct {struct{uint8_t ssid[32],password[64];struct{int authmode;} threshold;struct{bool capable,required;} pmf_cfg;}sta;} wifi_config_t;
typedef struct {bool show_hidden;} wifi_scan_config_t;
typedef struct {uint8_t ssid[33];int rssi;uint8_t primary;int authmode;} wifi_ap_record_t;
struct timeval;
typedef struct {const char *server;void (*sync_cb)(struct timeval *);} esp_sntp_config_t;
int esp_netif_init(void);
int esp_event_loop_create_default(void);
int esp_event_handler_register(const char *,int,void(*)(void*,const char*,int,void*),void*);
int esp_hosted_init(void);
int esp_hosted_connect_to_slave(void);
int esp_hosted_get_coprocessor_fwversion(esp_hosted_coprocessor_fwver_t *);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
int esp_wifi_init(wifi_init_config_t *);
int esp_wifi_deinit(void);
int esp_wifi_set_storage(int);
int esp_wifi_set_mode(int);
int esp_wifi_set_config(int,wifi_config_t *);
int esp_wifi_connect(void);
int esp_wifi_disconnect(void);
int esp_wifi_start(void);
int esp_wifi_stop(void);
int esp_wifi_scan_stop(void);
int esp_wifi_scan_start(wifi_scan_config_t *,bool);
int esp_wifi_scan_get_ap_records(uint16_t *,wifi_ap_record_t *);
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *);
int esp_netif_sntp_init(esp_sntp_config_t *);
int64_t esp_timer_get_time(void);
void *xTaskGetCurrentTaskHandle(void);
void xTaskNotifyGive(void *);
unsigned ulTaskNotifyTake(int,unsigned);
const char *esp_err_to_name(int);
int mock_setenv(const char *,const char *,int);
void mock_tzset(void);
'''
(OUT/"fakes.h").write_text(headers)
for name in ["esp_hosted.h","esp_wifi.h","esp_event.h","esp_netif.h","esp_netif_sntp.h","esp_timer.h","esp_log.h","freertos/FreeRTOS.h","freertos/task.h"]:
 f=OUT/name;f.parent.mkdir(exist_ok=True,parents=True);f.write_text('#include "fakes.h"\n')
fixture=r'''
#include "fakes.h"
#include "ouse_device_driver.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
const char *WIFI_EVENT="wifi",*IP_EVENT="ip",*ESP_HOSTED_EVENT="hosted";
static jmp_buf boundary;
static int64_t now;
static unsigned host_inits,wifi_inits,connects,disconnects,starts,stops,scans,ntps,version_queries;
static int wifi_init_error,wifi_mode_error,wifi_deinit_error,stop_error,connect_error,ntp_error;
static bool missing_slave,sdk_initialized,sdk_started,associated,supersede_init;
static uint32_t assigned_ip=0x0701a8c0;
static int signal=-42;
static wifi_config_t configured;
int esp_netif_init(void){return 0;}
int esp_event_loop_create_default(void){return 0;}
int esp_event_handler_register(const char *b,int i,void(*f)(void*,const char*,int,void*),void *a){assert(b&&f);(void)i;(void)a;return 0;}
int esp_hosted_init(void){++host_inits;return 0;}
int esp_hosted_connect_to_slave(void){if(supersede_init){supersede_init=false;assert(ouse_wifi_set_enabled(false)==ESP_ERR_INVALID_STATE);}return missing_slave ? ESP_ERR_TIMEOUT : 0;}
int esp_hosted_get_coprocessor_fwversion(esp_hosted_coprocessor_fwver_t *v){(void)v;++version_queries;return ESP_ERR_TIMEOUT;}
esp_netif_t *esp_netif_create_default_wifi_sta(void){return (esp_netif_t *)1;}
int esp_wifi_init(wifi_init_config_t *c){assert(c);++wifi_inits;if(wifi_init_error)return wifi_init_error;if(sdk_initialized)return ESP_ERR_INVALID_STATE;sdk_initialized=true;return 0;}
int esp_wifi_deinit(void){if(wifi_deinit_error)return wifi_deinit_error;sdk_initialized=false;return 0;}
int esp_wifi_set_storage(int s){assert(s==WIFI_STORAGE_RAM);return 0;}
int esp_wifi_set_mode(int s){assert(s==WIFI_MODE_STA&&sdk_initialized);return wifi_mode_error;}
int esp_wifi_set_config(int n,wifi_config_t *c){assert(n==WIFI_IF_STA);configured=*c;return 0;}
int esp_wifi_connect(void){assert(sdk_started);++connects;return connect_error;}
int esp_wifi_disconnect(void){++disconnects;associated=false;return 0;}
int esp_wifi_start(void){++starts;assert(sdk_initialized);if(sdk_started)return ESP_ERR_INVALID_STATE;sdk_started=true;return 0;}
int esp_wifi_stop(void){++stops;if(stop_error)return stop_error;assert(sdk_started);sdk_started=false;associated=false;return 0;}
int esp_wifi_scan_stop(void){return 0;}
int esp_wifi_scan_start(wifi_scan_config_t *c,bool block){assert(c && !block);++scans;return 0;}
int esp_wifi_scan_get_ap_records(uint16_t *n,wifi_ap_record_t *a){assert(*n==12);*n=3;memset(a,0,sizeof(*a)*3);strcpy((char*)a[0].ssid,"Office");a[0].rssi=-42;a[0].primary=6;a[0].authmode=3;a[1]=a[0];strcpy((char*)a[2].ssid,"Guest");a[2].rssi=-65;return 0;}
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *a){if(!associated)return ESP_ERR_WIFI_NOT_CONNECT;memset(a,0,sizeof(*a));memcpy(a->ssid,configured.sta.ssid,32);a->rssi=signal;return 0;}
int esp_netif_sntp_init(esp_sntp_config_t *c){assert(c&&c->server);++ntps;return ntp_error;}
int64_t esp_timer_get_time(void){return now;}
void *xTaskGetCurrentTaskHandle(void){return (void*)1;}
void xTaskNotifyGive(void *p){assert(p);}
unsigned ulTaskNotifyTake(int n,unsigned ms){assert(n && (ms==250 || ms==5000 || ms==portMAX_DELAY));longjmp(boundary,1);return 0;}
const char *esp_err_to_name(int e){(void)e;return "test";}
int mock_setenv(const char *a,const char *b,int n){assert(a&&b&&n);return 0;}
void mock_tzset(void){}
void ouse_clock_note_sync(void) {}
#include "BOARD_WIFI"
static void pump(void){if(!setjmp(boundary))ouse_board_wifi_worker(NULL);}
static ouse_wifi_state_t snapshot(void){ouse_wifi_state_t w;assert(ouse_wifi_get(&w)==0);return w;}
static void queue_disconnect_from(const char *ssid,int reason){wifi_event_sta_disconnected_t e={.reason=reason};e.ssid_len=(uint8_t)strlen(ssid);assert(e.ssid_len<=32);memcpy(e.ssid,ssid,e.ssid_len);event(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&e);}
static void queue_disconnect(int reason){associated=false;queue_disconnect_from((char*)configured.sta.ssid,reason);}
static void queue_ip(void){associated=true;ip_event_got_ip_t ip={.ip_info.ip.addr=assigned_ip};event(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&ip);}
static void disconnected(int reason){queue_disconnect(reason);pump();}
static void setup(void){ouse_device_port_t p={0};ouse_device_config_t c={.volume=40};assert(ouse_devices_init(&p,&c)==0);ouse_devices_set_available(2,true,0);}
static void connect_office(void){assert(ouse_wifi_set_enabled(true)==0);pump();event(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,NULL);pump();assert(ouse_wifi_connect("Office","correctpass")==0);pump();}
static int regression(const char *name){
 setup();
 if(!strcmp(name,"kernel-quiesce-queued")){
  connect_office();queue_ip();pump();assert(snapshot().connected);
  assert(ouse_wifi_scan()==ESP_OK);uint32_t queued=snapshot().request_id;
  ouse_tasks_quiesce(true);ouse_tasks_cancel_active(NULL,"restart");pump();
  assert(awaiting_stop==EVENT_STOP && session.id==queued);
  event(NULL,WIFI_EVENT,WIFI_EVENT_STA_STOP,NULL);pump();assert(snapshot().stopped);
  ouse_task_stats_t stats;ouse_tasks_get_stats(&stats);assert(!stats.active);
  puts("PASS quiesce queued: queued newer ID retired before physical STOP, confirmed cleanup remains observable");return 0;
 }else if(!strcmp(name,"kernel-recover") || !strcmp(name,"kernel-recover-not-started")){
  assert(ouse_wifi_set_enabled(true)==ESP_OK);pump();uint32_t id=snapshot().request_id;
  now+=16000000;pump();assert(awaiting_stop==EVENT_SCAN);
  if(!strcmp(name,"kernel-recover-not-started")){stop_error=ESP_ERR_WIFI_NOT_STARTED;sdk_started=false;}
  now+=6000000;pump();assert(awaiting_stop==EVENT_STOP && state.recovering && state.recovery_attempts==1);
  now+=6000000;pump();assert(state.recovery_failed && !state.recovering);
  ouse_task_snapshot_t task;assert(ouse_task_get(NULL,id,&task)&&task.state<OUSE_TASK_SUCCEEDED);
  assert(ouse_wifi_scan()==ESP_ERR_INVALID_STATE);
  event(NULL,WIFI_EVENT,WIFI_EVENT_STA_STOP,NULL);pump();assert(!awaiting_stop && !started && !state.recovery_failed);
  assert(ouse_task_get(NULL,id,&task)&&task.state==OUSE_TASK_FAILED);
  puts("PASS missing stop: one full STA recovery, failure remains fenced, late STOP safely unlocks");return 0;
 }else if(!strcmp(name,"kernel-drain")){
  assert(ouse_wifi_set_enabled(true)==ESP_OK);pump();uint32_t id=snapshot().request_id;
  ouse_task_snapshot_t task;assert(ouse_task_get(NULL,id,&task) && task.state==OUSE_TASK_RUNNING);
  assert(ouse_wifi_connect("Office","correctpass")==ESP_ERR_INVALID_STATE);
  now+=16000000;pump();assert(awaiting_stop==EVENT_SCAN);
  assert(ouse_task_get(NULL,id,&task) && task.state==OUSE_TASK_RUNNING);
  assert(ouse_wifi_connect("Office","correctpass")==ESP_ERR_INVALID_STATE);
  event(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,NULL);pump();
  assert(ouse_task_get(NULL,id,&task) && task.state==OUSE_TASK_FAILED && task.error==ESP_ERR_TIMEOUT);
  assert(ouse_wifi_connect("Office","correctpass")==ESP_OK);pump();queue_ip();pump();assert(snapshot().connected);
 }else if(!strcmp(name,"kernel-disconnect")){
  connect_office();queue_ip();pump();
  assert(ouse_wifi_disconnect()==ESP_OK);pump();uint32_t id=snapshot().request_id;
  ouse_task_snapshot_t task;assert(ouse_task_get(NULL,id,&task) && task.state==OUSE_TASK_RUNNING);
  assert(ouse_wifi_scan()==ESP_ERR_INVALID_STATE);
  queue_disconnect(WIFI_REASON_ASSOC_LEAVE);pump();
  assert(ouse_task_get(NULL,id,&task) && task.state==OUSE_TASK_SUCCEEDED);
  assert(ouse_wifi_set_enabled(false)==ESP_OK);pump();id=snapshot().request_id;
  assert(ouse_task_get(NULL,id,&task) && task.state==OUSE_TASK_RUNNING);
  event(NULL,WIFI_EVENT,WIFI_EVENT_STA_STOP,NULL);pump();
  assert(ouse_task_get(NULL,id,&task) && task.state==OUSE_TASK_SUCCEEDED);
 }else if(!strcmp(name,"kernel-owner-close")){
  ouse_task_owner_t owner;assert(ouse_task_owner_open("user.wifi",&owner)==ESP_OK);uint32_t id;
  assert(ouse_wifi_submit(&owner,OUSE_WIFI_ENABLE,true,NULL,NULL,&id)==ESP_OK);pump();
  ouse_task_owner_close(&owner);ouse_task_owner_t reopened;assert(ouse_task_owner_open("user.wifi",&reopened)==ESP_OK);
  event(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,NULL);pump();ouse_task_snapshot_t task;
  assert(ouse_task_get(NULL,id,&task) && task.state==OUSE_TASK_SUCCEEDED && task.detached && !task.delivery_pending);
  assert(!ouse_task_get(&reopened,id,&task));ouse_task_owner_close(&reopened);
 }else if(!strcmp(name,"merged-events")){
  connect_office();unsigned before=connects;queue_disconnect(1);queue_ip();pump();ouse_wifi_state_t w=snapshot();
  printf("merged disconnect->IP: connected=%d connecting=%d connects=%u expected=%u\n",w.connected,w.connecting,connects,before);fflush(stdout);
  assert(w.connected&&!w.connecting&&connects==before&&ntps==1);
  // Opposite ordering must reject stale IP, even when it arrived in this batch.
  assert(ouse_wifi_connect("Office","correctpass")==0);pump();before=connects;queue_ip();queue_disconnect(1);pump();w=snapshot();
  assert(!w.connected&&w.connecting&&connects==before+1&&ntps==1);
  queue_disconnect(WIFI_REASON_AUTH_FAIL);queue_ip();pump();assert(snapshot().connected&&!snapshot().connecting);
 }else if(!strcmp(name,"stop-retry")){
  assert(ouse_wifi_set_enabled(true)==0);pump();event(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,NULL);pump();stop_error=ESP_FAIL;assert(ouse_wifi_set_enabled(false)==0);pump();assert(snapshot().error==ESP_FAIL&&sdk_started);
  stop_error=0;assert(ouse_wifi_set_enabled(false)==0);pump();event(NULL,WIFI_EVENT,WIFI_EVENT_STA_STOP,NULL);pump();
  printf("failed stop then retry: calls=%u physical_started=%d worker_started=%d\n",stops,sdk_started,started);fflush(stdout);
  assert(stops==2&&!sdk_started&&!started&&snapshot().error==0);
  assert(ouse_wifi_set_enabled(true)==0);pump();assert(sdk_started&&snapshot().ready&&starts==2);
 }else if(!strcmp(name,"reconnect-failure")){
  connect_office();connect_error=ESP_FAIL;disconnected(1);ouse_wifi_state_t w=snapshot();
  printf("reconnect immediate failure: error=%d completed=%u request=%u\n",w.error,w.completed_id,w.request_id);fflush(stdout);
  assert(w.error==ESP_FAIL&&!w.connecting&&w.completed_id==w.request_id);
  connect_error=0;assert(ouse_wifi_connect("Office","correctpass")==0);pump();queue_ip();pump();assert(snapshot().connected);
 }else if(!strcmp(name,"init-cleanup-retry")){
  wifi_mode_error=ESP_FAIL;wifi_deinit_error=ESP_FAIL;assert(ouse_wifi_set_enabled(true)==0);pump();assert(snapshot().error==ESP_FAIL&&sdk_initialized&&!initialized);
  wifi_mode_error=0;wifi_deinit_error=0;assert(ouse_wifi_set_enabled(true)==0);pump();ouse_wifi_state_t w=snapshot();
  printf("failed init cleanup then retry: wifi_inits=%u ready=%d error=%d\n",wifi_inits,w.ready,w.error);fflush(stdout);
  assert(w.ready&&w.scanning&&w.error==0&&wifi_inits==1);
 }else if(!strcmp(name,"sntp-retry")){
  connect_office();ntp_error=ESP_FAIL;queue_ip();pump();assert(snapshot().connected&&!sntp_started&&ntps==1);
  ntp_error=0;assert(ouse_wifi_connect("Office","correctpass")==0);pump();queue_ip();pump();assert(snapshot().connected&&sntp_started&&ntps==2);
  assert(ouse_wifi_connect("Office","correctpass")==0);pump();queue_ip();pump();assert(snapshot().connected&&ntps==2);
 }else if(!strcmp(name,"dhcp-ip-change")){
  connect_office();queue_ip();pump();assert(snapshot().connected);assigned_ip=0x0801a8c0;queue_ip();pump();ouse_wifi_state_t w=snapshot();
  printf("connected DHCP IP change: IP=%s expected=192.168.1.8\n",w.ip);fflush(stdout);assert(w.connected&&!strcmp(w.ip,"192.168.1.8")&&ntps==1);
  unsigned before=connects;queue_disconnect(1);assigned_ip=0x0901a8c0;queue_ip();pump();w=snapshot();assert(w.connected&&!strcmp(w.ip,"192.168.1.9")&&connects==before&&ntps==1);
 }else if(!strcmp(name,"superseded-disconnect")){
  connect_office();queue_ip();pump();assert(ouse_wifi_connect("Guest","guestpass")==0);pump();unsigned before=connects;
  queue_disconnect_from("Office",WIFI_REASON_AUTH_FAIL);pump();ouse_wifi_state_t w=snapshot();
  printf("old SSID disconnect after switch: connecting=%d error=%d connects=%u\n",w.connecting,w.error,connects);fflush(stdout);
  assert(w.connecting&&w.error==0&&connects==before);
  // A late old-SSID event must not erase the new request's queued IP event.
  queue_ip();queue_disconnect_from("Office",1);pump();assert(snapshot().connected&&!strcmp(snapshot().ssid,"Guest")&&connects==before);
 }else if(!strcmp(name,"untagged-disconnect")){
  connect_office();unsigned before=connects;queue_disconnect_from("",1);pump();assert(snapshot().connecting&&connects==before+1);
 }else if(!strcmp(name,"transport-before-ip")){
  connect_office();queue_ip();event(NULL,ESP_HOSTED_EVENT,ESP_HOSTED_EVENT_TRANSPORT_DOWN,NULL);pump();ouse_wifi_state_t w=snapshot();ouse_device_config_t c;ouse_devices_get_config(&c);
  assert(!w.ready&&!w.connected&&!w.connecting&&w.error==ESP_ERR_TIMEOUT&&ntps==0&&!c.ssid[0]);
 }else if(!strcmp(name,"stop-reenable")){
  assert(ouse_wifi_set_enabled(true)==0);pump();event(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,NULL);pump();stop_error=ESP_FAIL;assert(ouse_wifi_set_enabled(false)==0);pump();
  stop_error=0;assert(ouse_wifi_set_enabled(true)==0);pump();assert(snapshot().ready&&snapshot().error==0&&sdk_started&&starts==1);
 }else if(!strcmp(name,"superseded-init")){
  supersede_init=true;assert(ouse_wifi_set_enabled(true)==0);pump();assert(snapshot().enabled&&starts==1&&scans==1&&sdk_started);
 }else assert(!"Unknown regression");
 printf("PASS actual Wi-Fi worker regression: %s\n",name);return 0;
}
int main(int argc,char **argv){
 if(argc==2)return regression(argv[1]);
 ouse_device_port_t port={0};ouse_device_config_t cfg={.volume=40};assert(ouse_devices_init(&port,&cfg)==0);ouse_devices_set_available(2,true,0);
 pump();assert(!host_inits && !wifi_inits); // Wi-Fi off does not start Hosted
 missing_slave=true;assert(ouse_wifi_set_enabled(true)==0);pump();ouse_wifi_state_t w=snapshot();assert(w.error==ESP_ERR_TIMEOUT && !w.ready && w.completed_id==w.request_id);
 missing_slave=false;
 wifi_init_error=ESP_ERR_TIMEOUT;assert(ouse_wifi_set_enabled(true)==0);pump();w=snapshot();
 assert(w.error==ESP_ERR_TIMEOUT && !w.ready && !scans && wifi_inits==1 && !initialized);
 wifi_inits=0;wifi_init_error=ESP_OK;
 assert(ouse_wifi_set_enabled(true)==0);pump();w=snapshot();assert(host_inits==1 && wifi_inits==1 && starts==1 && scans==1 && w.scanning);
 event(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,NULL);pump();w=snapshot();assert(w.count==2 && !w.scanning && !strcmp(w.aps[0].ssid,"Office"));
 assert(ouse_wifi_connect("Office","wrongpass")==0);pump();assert(snapshot().connecting && connects==1 && configured.sta.threshold.authmode==WIFI_AUTH_WPA2_PSK);
 disconnected(WIFI_REASON_AUTH_FAIL);w=snapshot();assert(!w.connecting && w.error==ESP_FAIL && connects==1);
 ouse_devices_get_config(&cfg);assert(!cfg.ssid[0]);
 assert(ouse_wifi_connect("Office","correctpass")==0);pump();
 queue_ip();pump();w=snapshot();
 assert(w.connected && !w.connecting && !strcmp(w.ip,"192.168.1.7") && ntps==1);
 ouse_devices_get_config(&cfg);assert(!strcmp(cfg.password,"correctpass") && !strcmp(cfg.ssid,"Office"));
 signal=-53;now+=5000000;pump();assert(snapshot().rssi==-53);
 unsigned before=connects;disconnected(1);assert(snapshot().connecting && connects==before+1);
 disconnected(1);assert(snapshot().connecting && connects==before+2);disconnected(1);assert(!snapshot().connecting && connects==before+2);
 assert(ouse_wifi_connect("Office","correctpass")==0);pump();now+=16000000;pump();w=snapshot();assert(w.error==ESP_ERR_TIMEOUT && !w.connecting);queue_disconnect(WIFI_REASON_ASSOC_LEAVE);pump();
 assert(ouse_wifi_set_enabled(true)==0);pump();queue_ip();pump();assert(snapshot().connected);
 event(NULL,ESP_HOSTED_EVENT,ESP_HOSTED_EVENT_TRANSPORT_DOWN,NULL);pump();assert(!snapshot().ready && snapshot().error==ESP_ERR_TIMEOUT);
 assert(ouse_wifi_set_enabled(true)==0);pump();assert(snapshot().ready && wifi_inits==1);
 assert(ouse_wifi_set_enabled(false)==0);pump();w=snapshot();assert(!w.enabled && !w.connected && stops==1);event(NULL,WIFI_EVENT,WIFI_EVENT_STA_STOP,NULL);pump();
 assert(ouse_wifi_forget()==0);pump();ouse_devices_get_config(&cfg);assert(!cfg.ssid[0] && !cfg.password[0] && host_inits==1);
 assert(!version_queries); // unsupported version RPC must never gate first init or recovery
 puts("PASS optional C6 version RPC omitted; actual Wi-Fi initialization failure still propagated");
 puts("PASS actual Wi-Fi worker: no startup while off, missing C6, scan dedupe, auth failure, successful persistence/IP/NTP, bounded reconnect/timeout, transport retry and off/forget");return 0;
}
'''.replace("BOARD_WIFI",(ROOT/"boards/waveshare_lcd35/board_wifi.c").as_posix())
(OUT/"check.c").write_text(fixture,encoding="utf-8")
result=subprocess.run([str(compiler),"/nologo","/utf-8","/std:c11","/O2","/MD","/UNDEBUG","/I"+str(OUT),"/I"+str(ROOT/".tools/ouse-core-check"),"/I"+str(ROOT/"components/ouse_devices/include"),"/I"+str(ROOT/"components/ouse_tasks/include"),"/I"+str(ROOT/"components/ouse_media/include"),"/I"+str(ROOT/"components/ouse_system/include"),str(ROOT/"components/ouse_tasks/ouse_tasks.c"),str(OUT/"check.c"),str(ROOT/"components/ouse_devices/ouse_devices.c"),"/Fe"+str(OUT/"check.exe")],cwd=OUT,env=env,capture_output=True,text=True,errors="replace")
assert result.returncode==0,(result.stdout,result.stderr)
if "--kernel-only" in sys.argv:
 for case in ("kernel-drain","kernel-disconnect","kernel-owner-close","kernel-recover","kernel-recover-not-started","kernel-quiesce-queued"):
  subprocess.run([str(OUT/"check.exe"),case],cwd=OUT,check=True,timeout=15)
 sys.exit(0)
subprocess.run([str(OUT/"check.exe")],cwd=OUT,check=True)
failures=[]
for case in ("merged-events","stop-retry","stop-reenable","reconnect-failure","init-cleanup-retry","sntp-retry","dhcp-ip-change","superseded-disconnect","untagged-disconnect","transport-before-ip","superseded-init"):
 result=subprocess.run([str(OUT/"check.exe"),case],cwd=OUT,capture_output=True,text=True,errors="replace")
 print(result.stdout,end="",flush=True)
 if result.returncode:
  print(result.stderr,end="",flush=True);failures.append(case)
assert not failures,failures
