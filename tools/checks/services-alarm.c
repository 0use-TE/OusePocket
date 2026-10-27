#include "platform.h"
#include "ouse_alarm.h"
#include "ouse_device_driver.h"
#include "ouse_notifications.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <io.h>
#include <direct.h>
static int64_t wall=1704067200;
static bool fail_rename;
static unsigned published,cancelled;
static time_t fake_time(time_t *out){if(out)*out=(time_t)wall;return (time_t)wall;}
static int fake_rename(const char *from,const char *to)
{if(fail_rename)return -1;if(MoveFileExA(from,to,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return 0;
 fprintf(stderr,"Host alarm rename failed: winerror=%lu %s -> %s\n",GetLastError(),from,to);return -1;}
esp_err_t ouse_notifications_publish(const char *app,const ouse_notification_request_t *r)
{assert(app&&r&&r->ongoing&&!strncmp(r->id,"alarm:",6));++published;return ESP_OK;}
esp_err_t ouse_notifications_cancel(const char *app,const char *id)
{assert(app&&id);++cancelled;return ESP_OK;}
#define time fake_time
#define rename fake_rename
#include "ALARM_SOURCE"
#undef time
#undef rename
static void pump(void){platform_hook=NULL;break_on_empty=true;if(!setjmp(worker_exit))worker(NULL);break_on_empty=false;}
static void silence(void)
{if(ring_task){ouse_task_snapshot_t t;assert(ouse_task_get(NULL,ring_task,&t));if(t.cancel_requested)ouse_task_finish(ring_task,ESP_OK,true);}}
static command_t set_command(const char *app,const char *id,int64_t at)
{command_t c={.action=OUSE_ALARM_SET,.spec={.at_utc=at}};strcpy(c.app_id,app);strcpy(c.spec.id,id);strcpy(c.spec.title,"Native alarm");return c;}
int main(void)
{
    _mkdir("alarm-store");_unlink(FILE_NAME);_unlink(STAGING);
    ouse_device_port_t port={0};ouse_device_config_t config={.volume=40,.sleep_seconds=60};assert(ouse_devices_init(&port,&config)==ESP_OK);ouse_devices_set_available(1,true,ESP_OK);
    const char *apps[]={"user.alarm","user.other"};assert(ouse_alarm_start(apps,2)==ESP_OK);
    ouse_task_owner_t owner;assert(ouse_task_owner_open(apps[0],&owner)==ESP_OK);
    ouse_alarm_spec_t spec={.id="probe",.title="Native alarm",.at_utc=wall+10};uint32_t id;
    assert(ouse_alarm_submit(&owner,OUSE_ALARM_SET,&spec,0,&id)==ESP_OK);
    ouse_alarm_snapshot_t list[4];assert(ouse_alarm_list(owner.app_id,list,4)==0); // submit has no I/O/model commit
    ouse_task_owner_close(&owner);pump(); // accepted FINISH_ON_CLOSE persists after instance close
    assert(db.count==1&&ouse_alarm_list(apps[0],list,4)==1);ouse_task_snapshot_t task;
    assert(ouse_task_get(NULL,id,&task)&&task.detached&&task.state==OUSE_TASK_SUCCEEDED&&!task.delivery_pending);
    database_t saved=db;memset(&db,0,sizeof(db));assert(load()==ESP_OK&&db.count==1&&db.records[0].next_utc==wall+10);
    command_t toggle=set_command(apps[0],"probe",0);toggle.action=OUSE_ALARM_DISABLE;
    fail_rename=true;assert(execute(&toggle)==ESP_FAIL&&db.records[0].enabled);fail_rename=false;
    assert(execute(&toggle)==ESP_OK&&!db.records[0].enabled);
    memset(&db,0,sizeof(db));assert(load()==ESP_OK&&!db.records[0].enabled);
    toggle.action=OUSE_ALARM_ENABLE;assert(execute(&toggle)==ESP_OK&&db.records[0].enabled&&db.records[0].next_utc==wall+10);
    wall+=10;test_clock+=100000;tick();assert(published==1&&db.records[0].last_utc==wall&&!db.records[0].enabled&&ringing_until[0]);
    tick();assert(published==1); // consumed occurrence cannot replay
    assert(execute(&toggle)==ESP_ERR_INVALID_ARG); // expired one-shot needs an explicit new schedule
    command_t c=set_command(apps[0],"probe",0);c.action=OUSE_ALARM_SNOOZE;c.snooze=1;
    assert(execute(&c)==ESP_OK&&db.records[0].next_utc==wall+60&&!ringing_until[0]);tick();silence();
    wall+=60;tick();assert(published==2);c.action=OUSE_ALARM_DISMISS;assert(execute(&c)==ESP_OK&&!ringing_until[0]);tick();silence();
    c=set_command(apps[0],"late",wall+1);assert(execute(&c)==ESP_OK);wall+=200;tick();assert(published==2&&db.records[1].missed&&!db.records[1].enabled);
    c=set_command(apps[0],"weekly",0);c.spec.weekdays=127;c.spec.minute_of_day=(unsigned)((wall/60+1)%1440);assert(execute(&c)==ESP_OK);
    toggle=set_command(apps[0],"weekly",0);toggle.action=OUSE_ALARM_DISABLE;assert(execute(&toggle)==ESP_OK&&!db.records[2].enabled);
    toggle.action=OUSE_ALARM_ENABLE;assert(execute(&toggle)==ESP_OK&&db.records[2].enabled);
    wall=db.records[2].next_utc;tick();unsigned rings=published;int64_t last=db.records[2].last_utc;
    wall-=86400;tick();assert(published==rings&&db.records[2].last_utc==last);
    wall+=86400;tick();assert(published==rings); // backward/forward clock does not replay last occurrence
    c=set_command(apps[0],"four",wall+100);assert(execute(&c)==ESP_OK);c=set_command(apps[0],"five",wall+100);assert(execute(&c)==ESP_ERR_NO_MEM);
    c=set_command(apps[1],"other",wall+10);assert(execute(&c)==ESP_OK);
    database_t before=db;fail_rename=true;c=set_command(apps[1],"not-committed",wall+15);assert(execute(&c)==ESP_FAIL&&db.count==before.count);fail_rename=false;
    memset(&db,0,sizeof(db));assert(load()==ESP_OK&&db.count==before.count);
    ouse_alarm_register_app(apps[1],false);wall+=20;rings=published;tick();assert(published==rings); // ineligible app cannot trigger
    assert(ouse_task_owner_open(apps[0],&owner)==ESP_OK);spec.at_utc=wall+100;
    assert(ouse_alarm_submit(&owner,OUSE_ALARM_SET,&spec,0,&id)==ESP_OK);assert(ouse_task_cancel(&owner,id,"test")==ESP_OK);pump();
    assert(ouse_task_get(&owner,id,&task)&&task.state==OUSE_TASK_CANCELLED);
    // Purge waits for the same native FIFO, including previously accepted set.
    platform_hook=pump;assert(ouse_alarm_purge(apps[0])==ESP_OK);assert(db.count==1&&!strcmp(db.records[0].app_id,apps[1]));
    FILE *file=fopen(FILE_NAME,"r+b");assert(file);fseek(file,-1,SEEK_END);fputc(0x7f,file);fclose(file);
    assert(load()==ESP_ERR_INVALID_STATE);file=fopen(FILE_NAME,"rb");assert(file);fseek(file,0,SEEK_END);assert(ftell(file)==DISK_SIZE);fclose(file);
    assert(save(&saved)==ESP_OK); // only explicit test restoration overwrites corruption
    puts("PASS alarms: real disk schema/CRC/atomic commit failure, close survival, due/dismiss/snooze/missed, backward clock dedup, quotas, revoked authorization, cancel and FIFO purge; corrupt file preserved");
}
