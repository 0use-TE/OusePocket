"""Run production GAP callback against deterministic host events."""
from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parents[1];out=root/'.tools/ble-gap-check';out.mkdir(exist_ok=True)
s=(root/'components/ouse_ble/ouse_ble.c').read_text()
gap=s[s.index('static int gap_event(struct ble_gap_event *event,void *context)\n{'):s.index('static int access_link(')]
fixture=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define BLE_HS_CONN_HANDLE_NONE 65535
#define BLE_ERR_REM_USER_CONN_TERM 19
#define BLE_SM_IOACT_NUMCMP 5
#define BLE_GAP_REPEAT_PAIRING_IGNORE 2
#define BLE_GAP_REPEAT_PAIRING_RETRY 1
enum {BLE_GAP_EVENT_CONNECT,BLE_GAP_EVENT_DISCONNECT,BLE_GAP_EVENT_DISC,BLE_GAP_EVENT_DISC_COMPLETE,BLE_GAP_EVENT_ADV_COMPLETE,BLE_GAP_EVENT_ENC_CHANGE,BLE_GAP_EVENT_PASSKEY_ACTION,BLE_GAP_EVENT_REPEAT_PAIRING};
struct ble_gap_conn_desc {int peer_id_addr;uint16_t conn_handle;};
struct ble_gap_event {int type;struct {int status;uint16_t conn_handle;} connect,enc_change;struct {struct ble_gap_conn_desc conn;int reason;} disconnect;int disc;struct {struct {int action;unsigned numcmp;} params;uint16_t conn_handle;} passkey;struct {uint16_t conn_handle;} repeat_pairing;};
static struct {bool advertising,connected,encrypted,authenticated,bonded,pair_pending,scanning;int error;char peer[18];unsigned pair_id,number;} state;
static atomic_bool initiating;static bool secure_connect,plain_connection,reconnect_paused;static uint16_t connection=65535;
static unsigned connection_since,prompt_until,pair_until,next_pair_id,now=100;
static unsigned secure_calls,terminated,ads,deletes,app_events;static int delete_error;
static void changed(void){} static unsigned clock_ms(void){return now;}
static bool before(unsigned deadline){return deadline&&(int32_t)(deadline-now)>0;}
static void connected_refresh(uint16_t h){assert(h==7);state.connected=true;}
static int ble_gap_security_initiate(uint16_t h){assert(h==7);secure_calls++;return 0;}
static int advertise(void){ads++;return 0;}
static void discovered(void *p){(void)p;}
static void error_set(int e){state.error=e;}
static int ble_gap_terminate(uint16_t h,int reason){assert(h==7&&reason==19);terminated++;return 0;}
static int ble_gap_conn_find(uint16_t h,struct ble_gap_conn_desc *p){assert(h==7);p->peer_id_addr=42;return 0;}
static int ble_store_util_delete_peer(int *p){assert(*p==42);deletes++;return delete_error;}
static void app_gap_event(struct ble_gap_event *e){(void)e;app_events++;}
'''
tests=r'''
int main(void){
struct ble_gap_event e={.type=BLE_GAP_EVENT_CONNECT,.connect={.conn_handle=7}};
/* Incoming reconnect initiates encryption, even after the pairing window closed. */
gap_event(&e,0);assert(secure_calls==1&&!plain_connection&&connection==7);
/* Explicit plain application connections retain their policy. */
initiating=true;secure_connect=false;gap_event(&e,0);assert(plain_connection&&secure_calls==1);
initiating=true;secure_connect=true;gap_event(&e,0);assert(!plain_connection&&secure_calls==2);
e=(struct ble_gap_event){.type=BLE_GAP_EVENT_REPEAT_PAIRING,.repeat_pairing={.conn_handle=7}};
assert(gap_event(&e,0)==BLE_GAP_REPEAT_PAIRING_IGNORE&&!deletes);
pair_until=1000;assert(gap_event(&e,0)==BLE_GAP_REPEAT_PAIRING_RETRY&&deletes==1);
delete_error=6;assert(gap_event(&e,0)==BLE_GAP_REPEAT_PAIRING_IGNORE&&deletes==2);
e=(struct ble_gap_event){.type=BLE_GAP_EVENT_PASSKEY_ACTION,.passkey={.params={.action=5,.numcmp=123456},.conn_handle=7}};
gap_event(&e,0);assert(state.pair_pending&&state.number==123456&&!terminated);
pair_until=0;gap_event(&e,0);assert(terminated==1);
/* Never advertise inside disconnect callback: its sole host slot is still busy. */
e=(struct ble_gap_event){.type=BLE_GAP_EVENT_DISCONNECT};gap_event(&e,0);
assert(connection==65535&&!state.connected&&!state.pair_pending&&!ads);
/* An already queued inbound connect must not bypass explicit disconnect. */
reconnect_paused=true;unsigned prior_secure=secure_calls;
e=(struct ble_gap_event){.type=BLE_GAP_EVENT_CONNECT,.connect={.conn_handle=7}};
gap_event(&e,0);assert(terminated==2&&secure_calls==prior_secure&&!state.connected);
e=(struct ble_gap_event){.type=BLE_GAP_EVENT_DISCONNECT};gap_event(&e,0);
assert(connection==65535&&reconnect_paused&&!ads);
puts("PASS incoming security, secure/plain outgoing policy, gated stale-key repair, numeric confirmation and deferred advertising");
}
'''
(out/'check.c').write_text(fixture+gap+tests)
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(root/'.tools/ouse-kernel-check/environment.cmd')],encoding='mbcs').splitlines():
 if '=' in line and not line.startswith('='):
  k,v=line.split('=',1);env[k]=v
cl=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
subprocess.run([str(cl),'/nologo','/std:c11','/experimental:c11atomics','/utf-8','/UNDEBUG',str(out/'check.c'),'/Fe'+str(out/'check.exe')],cwd=out,env=env,check=True)
subprocess.run([str(out/'check.exe')],check=True)

