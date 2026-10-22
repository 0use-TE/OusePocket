#include "platform.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "COORDINATOR_SOURCE"
static const char *scenario;
static jmp_buf end;
static bool restarted,storage_idle=true,wifi_stopped=true;
static unsigned contexts,closed,background_ended;
static uint32_t task_id;
static ouse_task_owner_t registered_owner;
static void (*late_ui)(void*);
static void after_resume(void);
esp_err_t ouse_app_get_info(unsigned i,ouse_app_info_t *out){if(i>1)return ESP_ERR_NOT_FOUND;strcpy(out->id,i ? "ouse.settings" : "user.a");return ESP_OK;}
esp_err_t ouse_app_close_id(const char *id){assert(!strcmp(id,"user.a"));++closed;contexts=0;ouse_task_owner_close(&registered_owner);return ESP_OK;}
esp_err_t ouse_core_post_ui(void (*fn)(void*),void *context){if(!strcmp(scenario,"ui-late"))late_ui=fn;else fn(context);return ESP_OK;}
void ouse_runtime_get_stats(ouse_runtime_stats_t *s){s->contexts=contexts;}
void ouse_runtime_get_diagnostics(ouse_runtime_diagnostics_t *s){memset(s,0,sizeof(*s));}
esp_err_t ouse_runtime_end_background(uint32_t id){background_ended=id;return ESP_OK;}
esp_err_t ouse_wifi_get(ouse_wifi_state_t *s){memset(s,0,sizeof(*s));s->available=true;s->stopped=wifi_stopped;return ESP_OK;}
esp_err_t ouse_audio_get(ouse_audio_state_t *s){memset(s,0,sizeof(*s));s->available=true;return ESP_OK;}
esp_err_t ouse_storage_get_stats(ouse_storage_stats_t *s){memset(s,0,sizeof(*s));s->ready=true;return ESP_OK;}
bool ouse_storage_idle(void){return storage_idle;}
void ouse_alarm_get_stats(ouse_alarm_stats_t *s){memset(s,0,sizeof(*s));s->ready=true;}
bool ouse_alarm_quiesced(void){return true;}
esp_err_t ouse_alarm_dismiss_all(void){return ESP_OK;}
void ouse_installer_get_state(ouse_installer_state_t *s){*s=(ouse_installer_state_t){.ready=true,.recovery_pending=!strcmp(scenario,"uninstall-pending")};}
esp_err_t ouse_system_get_settings_state(ouse_settings_state_t *s){memset(s,0,sizeof(*s));return ESP_OK;}
void ouse_devices_save_state(ouse_device_save_state_t *s){memset(s,0,sizeof(*s));}
bool ouse_net_available(void){return true;}
void ouse_system_sample_memory(void){}
void ouse_devices_wake_workers(void){}
bool ouse_camera_idle(void){return true;}
bool ouse_recorder_idle(void){return true;}
bool ouse_journal_flush(unsigned timeout){(void)timeout;return true;}
void ouse_usb_get(ouse_usb_state_t *s){memset(s,0,sizeof(*s));}
esp_err_t ouse_board_power_off(void){return ESP_FAIL;}
void ouse_dialog_show(const char *message,void *callback,void *context){(void)message;(void)callback;(void)context;}
void esp_restart(void){assert(!strcmp(scenario,"drain") || !strcmp(scenario,"queued-commit"));assert(test_clock>=(!strcmp(scenario,"drain") ? 5500000 : 2500000));assert(closed==1);restarted=true;longjmp(end,1);}
static void step(void)
{
    assert(ouse_tasks_quiescing());
    if(!strcmp(scenario,"queued-commit")){
        ouse_task_snapshot_t t;assert(ouse_task_get(NULL,task_id,&t));assert(!t.cancel_requested);
        if(test_clock<1000000)assert(t.state==OUSE_TASK_QUEUED);
        else if(test_clock==1000000)assert(ouse_task_start(task_id,false));
        else if(test_clock>=2000000)ouse_task_finish(task_id,ESP_OK,false);
        return;
    }
    if(!strcmp(scenario,"commit-failure") && test_clock>=1000000)ouse_task_finish(task_id,ESP_FAIL,false);
    if(!strcmp(scenario,"drain")){
        ouse_task_snapshot_t t;assert(ouse_task_get(NULL,task_id,&t));
        if(test_clock<2000000)assert(t.cancel_requested && t.state==OUSE_TASK_CANCELLING);
        else ouse_task_finish(task_id,ESP_OK,true);
        if(test_clock>=4000000)wifi_stopped=true;
        if(test_clock>=5000000)storage_idle=true;
    }else if(test_clock>=16000000){
        ouse_health_snapshot_t h;ouse_health_get(&h);assert(!strcmp(h.phase,"blocked") && !restarted && h.quiescing);
        if(!strcmp(scenario,"ui-late")){
            assert(!strcmp(h.blocker,"ui-close") && late_ui);
            assert(!atomic_load(&ui_done));late_ui(NULL);assert(atomic_load(&ui_done));
        }else if(!strcmp(scenario,"commit-failure"))assert(!strcmp(h.blocker,"storage-write-failed"));
        else if(!strcmp(scenario,"uninstall-pending"))assert(!strcmp(h.blocker,"uninstall-recovery-pending"));
        else if(!strcmp(scenario,"pending-launch"))assert(!strcmp(h.blocker,"javascript-instances"));
        else assert(!strcmp(h.blocker,"wifi-stop-confirmation"));
        assert(ouse_control_request(OUSE_CONTROL_RESUME,NULL,0,0)==ESP_OK);
        platform_hook=after_resume;
    }
}
static void after_resume(void){assert(!ouse_tasks_quiescing());longjmp(end,3);}
int main(int argc,char **argv)
{
    assert(argc==2);scenario=argv[1];ouse_health_enable();
    if(!strcmp(scenario,"native")){
        ouse_task_owner_t native={.app_id="system.native",.instance=0};uint32_t id;
        assert(ouse_task_create(&native,OUSE_TASK_NATIVE,"execute",OUSE_TASK_STOP_ON_CLOSE,true,&id)==ESP_OK);
        assert(ouse_task_start(id,true));assert(!strcmp(ouse_task_kind_name(OUSE_TASK_NATIVE),"native"));
        ouse_tasks_quiesce(true);cancel_transient_work();ouse_task_snapshot_t t;
        assert(ouse_task_get(NULL,id,&t) && t.cancel_requested && t.state==OUSE_TASK_CANCELLING);
        assert(!strcmp(drain_blocker(0),"native-tasks"));
        uint32_t rejected;assert(ouse_task_create(&native,OUSE_TASK_NATIVE,"execute",OUSE_TASK_STOP_ON_CLOSE,true,&rejected)==ESP_ERR_INVALID_STATE);
        ouse_task_finish(id,ESP_OK,true);assert(!drain_blocker(0));
        puts("PASS native: system drain requests cooperative stop, waits for actual return, rejects new execution");return 0;
    }
    ouse_task_owner_t owner;assert(ouse_task_owner_open("user.a",&owner)==ESP_OK);
    registered_owner=owner;
    if(!strcmp(scenario,"pending-launch")){ouse_task_owner_t pending;assert(ouse_task_owner_open("user.validation",&pending)==ESP_OK);}
    bool commit=!strcmp(scenario,"commit-failure"),queued=!strcmp(scenario,"queued-commit");
    assert(ouse_task_create(&owner,commit || queued ? OUSE_TASK_STORAGE : OUSE_TASK_AUDIO,commit || queued ? "set" : "play",OUSE_TASK_FINISH_ON_CLOSE,true,&task_id)==ESP_OK);
    if(!queued)assert(ouse_task_start(task_id,!commit));
    if(!strcmp(scenario,"owner")){
        assert(ouse_activity_stop_owned("user.b",task_id)==ESP_ERR_NOT_FOUND);
        ouse_task_owner_close(&owner);assert(ouse_task_owner_open("user.a",&owner)==ESP_OK);
        assert(ouse_activity_stop_owned("user.a",task_id)==ESP_OK);
        ouse_task_snapshot_t t;assert(ouse_task_get(NULL,task_id,&t)&&t.cancel_requested&&t.state==OUSE_TASK_CANCELLING);
        puts("PASS owner: foreign rejected; reopened same app can stop surviving native session, no false completion");return 0;
    }
    if(!strcmp(scenario,"admission")){
        assert(ouse_tasks_submission_begin());ouse_tasks_quiesce(true);ouse_task_stats_t s;ouse_tasks_get_stats(&s);assert(s.submitting==1);
        assert(!ouse_tasks_submission_begin());uint32_t id;assert(ouse_task_create(&owner,OUSE_TASK_NETWORK,"GET",OUSE_TASK_STOP_ON_CLOSE,true,&id)==ESP_ERR_INVALID_STATE);
        ouse_task_owner_t other;assert(ouse_task_owner_open("user.a",&other)==ESP_ERR_INVALID_STATE);
        ouse_tasks_submission_end();ouse_tasks_get_stats(&s);assert(!s.submitting);assert(!strcmp(drain_blocker(0),"native-tasks"));
        puts("PASS admission: drain rejects owners/requests and waits for already admitted setters");return 0;
    }
    wifi_stopped=commit || queued || !strcmp(scenario,"uninstall-pending") || !strcmp(scenario,"pending-launch");contexts=1;storage_idle=strcmp(scenario,"drain")!=0;
    if(strcmp(scenario,"drain") && !commit && !queued)ouse_task_finish(task_id,ESP_OK,false);
    assert(ouse_control_request(OUSE_CONTROL_RESTART,NULL,0,0)==ESP_OK);
    assert(ouse_control_request(OUSE_CONTROL_RESTART,NULL,0,0)==ESP_ERR_INVALID_STATE);
    platform_hook=step;int result=setjmp(end);
    if(!result)worker(NULL);
    if(!strcmp(scenario,"drain") || queued)assert(restarted);else assert(!restarted && !ouse_tasks_quiescing());
    printf("PASS coordinator %s: real policy loop with bounded deterministic RTOS/hardware doubles\n",scenario);
}
