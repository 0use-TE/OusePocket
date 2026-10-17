#include "quickjs.h"
#include "platform.h"
#include "ouse_tasks.h"
#include "ouse_net.h"
#include "ouse_media.h"
#include "ouse_alarm.h"
#include "ouse_clock.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_NET_PENDING 2
#define MAX_NET_JOBS 16
#define NAV_IDLE 0
#define OUSE_SCRIPT_ASSET_BINARY 1
#define atomic_fetch_add(p,v) (*(p)+=(v))
typedef struct net_job net_job_t;
typedef struct instance {
    JSContext *context;ouse_task_owner_t owner;
    char id[48];bool network_allowed,alarms_allowed,media_allowed,background_allowed,notifications_allowed,validation_only;
    bool closed,faulted,visible,background_active;
    unsigned references,navigation_stage;int budget;
    net_job_t *network[MAX_NET_PENDING];
    unsigned asset_count;
    struct {const char *id;int type;const unsigned char *data;size_t size;} assets[1];
} instance_t;
struct net_job {instance_t *app;unsigned slot;uint32_t token;JSValue resolve,reject;ouse_net_result_t *result;bool completed,bytes;};
static QueueHandle_t net_completions;
static unsigned net_job_count;
static unsigned native_count,media_calls,alarm_calls;
static struct {ouse_net_complete_t fn;void *context;uint32_t id;} native[32];
static instance_t *from_js(JSContext *context){return JS_GetContextOpaque(context);}
static void release_instance(instance_t *app){assert(app->references>1);--app->references;}
static void model_lock(instance_t *app){(void)app;}
static void model_unlock(instance_t *app){(void)app;}
static void wake_worker(void){}
static bool ouse_budget_slice_done(int *budget){(void)budget;return false;}
static JSValue call_js(instance_t *app,const char *kind,const char *event,JSValueConst fn,JSValueConst self,int argc,JSValueConst *argv)
{(void)kind;(void)event;return JS_Call(app->context,fn,self,argc,argv);}
static bool result_ok(instance_t *app,JSValue value)
{bool ok=!JS_IsException(value);if(!ok){JSValue e=JS_GetException(app->context);const char *text=JS_ToCString(app->context,e);fprintf(stderr,"unexpected JS: %s\n",text);JS_FreeCString(app->context,text);JS_FreeValue(app->context,e);}JS_FreeValue(app->context,value);return ok;}
/* COPY_STRING */
/* ERROR_HELPERS */
bool ouse_net_available(void){return true;}
void ouse_net_result_free(ouse_net_result_t *r){if(r){free(r->body);free(r);}}
esp_err_t ouse_net_submit(const ouse_task_owner_t *owner,const ouse_http_request_t *q,uint32_t *id,ouse_net_complete_t fn,void *context)
{
    assert(q->timeout_ms>=1000&&q->timeout_ms<=30000&&q->body_size<=8192&&q->header_count<=12);
    assert(native_count<32);esp_err_t e=ouse_task_create(owner,OUSE_TASK_NETWORK,q->method,OUSE_TASK_STOP_ON_CLOSE,true,id);
    if(e==ESP_OK){ouse_task_hold_result(*id,true);native[native_count].fn=fn;native[native_count].context=context;native[native_count++].id=*id;}
    return e;
}
void ouse_clock_get(ouse_clock_state_t *out){*out=(ouse_clock_state_t){.utc_ms=1704067200000,.valid=true,.synced_this_boot=false};}
void ouse_alarm_get_stats(ouse_alarm_stats_t *out){*out=(ouse_alarm_stats_t){.ready=true};}
bool ouse_alarm_spec_valid(const ouse_alarm_spec_t *s){return s&&s->id[0]&&s->title[0]&&(s->weekdays||s->at_utc>=OUSE_CLOCK_MIN_UTC);}
unsigned ouse_alarm_list(const char *id,ouse_alarm_snapshot_t *out,unsigned capacity)
{assert(id&&out&&capacity==4);return 0;}
esp_err_t ouse_alarm_submit(const ouse_task_owner_t *owner,ouse_alarm_action_t a,const ouse_alarm_spec_t *s,unsigned minutes,uint32_t *id)
{assert(owner&&s&&a<=OUSE_ALARM_SNOOZE);if(a==OUSE_ALARM_SNOOZE)assert(minutes>=1&&minutes<=60);++alarm_calls;return ouse_task_create(owner,OUSE_TASK_ALARM,"set",OUSE_TASK_FINISH_ON_CLOSE,true,id);}
bool ouse_media_available(void){return true;}
bool ouse_media_get(const char *id,ouse_media_state_t *out){(void)id;(void)out;return false;}
esp_err_t ouse_media_pause(const char *id,bool paused){(void)paused;assert(id);return ESP_ERR_NOT_FOUND;}
esp_err_t ouse_media_stop(const char *id){assert(id);return ESP_ERR_NOT_FOUND;}
esp_err_t ouse_media_play(const ouse_task_owner_t *owner,const char *url,const void *data,size_t size,uint32_t *id)
{assert(owner&&((url&&!data&&!size)||(!url&&data&&size==4&&!memcmp(data,"RIFF",4))));++media_calls;return ouse_task_create(owner,OUSE_TASK_AUDIO,"play",OUSE_TASK_FINISH_ON_CLOSE,true,id);}
static const char *ouse_core_version(void){return "test-services";}
#include "SERVICES_SOURCE"
static void evaluate(instance_t *app,const char *script)
{
    JSValue result=JS_Eval(app->context,script,strlen(script),"services-test",JS_EVAL_TYPE_GLOBAL);
    if(!result_ok(app,result)){fprintf(stderr,"Failing script: %s\n",script);abort();}
    JSContext *context=NULL;int jobs=0,ret;
    while((ret=JS_ExecutePendingJob(JS_GetRuntime(app->context),&context))>0)assert(++jobs<100);
    if(ret<0){JSValue error=JS_GetException(context);const char *text=JS_ToCString(context,error);fprintf(stderr,"JS job failed: %s\n",text);abort();}
}
static void setup(instance_t *app,JSRuntime *runtime)
{
    memset(app,0,sizeof(*app));strcpy(app->id,"user.service");assert(ouse_task_owner_open(app->id,&app->owner)==ESP_OK);
    app->context=JS_NewContext(runtime);app->references=1;app->visible=true;
    app->network_allowed=app->media_allowed=app->alarms_allowed=true;
    app->asset_count=1;app->assets[0].id="sample";app->assets[0].type=1;app->assets[0].data=(const unsigned char*)"RIFF";app->assets[0].size=4;
    JS_SetContextOpaque(app->context,app);JSValue global=JS_GetGlobalObject(app->context),api=JS_NewObject(app->context);
    JS_SetPropertyStr(app->context,api,"request",JS_NewCFunction(app->context,net_request,"request",1));
    JS_SetPropertyStr(app->context,api,"clock",JS_NewCFunction(app->context,clock_get,"clock",0));
    JS_SetPropertyStr(app->context,api,"caps",JS_NewCFunction(app->context,system_capabilities,"caps",0));
    const char *alarms[]={"alarmSet","alarmRemove","alarmDismiss","alarmSnooze","alarmList"};
    const char *media[]={"play","pause","resume","stop","mediaGet"};
    for(int i=0;i<5;++i){JS_SetPropertyStr(app->context,api,alarms[i],JS_NewCFunctionMagic(app->context,alarms_api,alarms[i],1,JS_CFUNC_generic_magic,i));
        JS_SetPropertyStr(app->context,api,media[i],JS_NewCFunctionMagic(app->context,media_api,media[i],1,JS_CFUNC_generic_magic,i));}
    JS_SetPropertyStr(app->context,global,"Ouse",api);JS_FreeValue(app->context,global);
    evaluate(app,"function check(v){if(!v)throw Error('check failed');}");
}
static void complete_native(unsigned n)
{
    assert(n<native_count&&native[n].fn);ouse_net_result_t *r=calloc(1,sizeof(*r));r->response.status=200;r->response.body_size=5;r->body=malloc(6);memcpy(r->body,"hello",6);
    (void)ouse_task_start(native[n].id,true);ouse_task_finish(native[n].id,ESP_OK,false);
    native[n].fn(native[n].context,native[n].id,r);native[n].fn=NULL;
}
int main(void)
{
    JSRuntime *runtime=JS_NewRuntime();assert(runtime);JS_SetMemoryLimit(runtime,512*1024);instance_t app;setup(&app,runtime);
    net_completions=xQueueCreate(16,sizeof(net_job_t*));
    evaluate(&app,"check(Ouse.clock().valid&&!Ouse.clock().syncedThisBoot);check(Ouse.caps().network.responseBodyMax===32768);var done=0,received=null,networkError=null;var p=Ouse.request({url:'https://example.test/'});check(p.taskId>0);p.then(r=>{received=JSON.stringify(r);check(r.body==='hello'&&r.status===200);done++;},e=>networkError=String(e));");
    assert(net_job_count==1&&app.references==2);complete_native(0);drain_net_completions();app.visible=false;process_net_results(&app);evaluate(&app,"check(done===0);");
    app.visible=true;process_net_results(&app);evaluate(&app,"");evaluate(&app,"if(done!==1)throw Error(JSON.stringify({done,received,networkError}));");assert(!net_job_count&&app.references==1);
    evaluate(&app,"var rejected=0;Ouse.request({url:'x',responseType:'invalid'}).catch(e=>rejected++);Ouse.request({get url(){throw Error('getter');}}).catch(e=>{check(e.message==='getter');rejected++;});");evaluate(&app,"check(rejected===2);");assert(native_count==1&&!net_job_count);
    evaluate(&app,"var busy=0;Ouse.request({get url(){Ouse.request({url:'https://example.test/a'}).catch(()=>{});Ouse.request({url:'https://example.test/b'}).catch(()=>{});return 'https://example.test/c';}}).catch(e=>busy++);");
    assert(net_job_count==2&&native_count==3);evaluate(&app,"check(busy===1);");complete_native(1);complete_native(2);drain_net_completions();process_net_results(&app);evaluate(&app,"");
    evaluate(&app,"check(Ouse.alarmSet({id:'weekly',title:'Morning',weekdays:62,minuteOfDay:450,utcOffsetMinutes:480})>0);check(Ouse.alarmSnooze('weekly',5)>0);check(Ouse.alarmList().length===0);check(Ouse.play({asset:'sample'})>0);");
    assert(alarm_calls==2&&media_calls==1);app.network_allowed=false;evaluate(&app,"check(Ouse.play({url:'https://example.test/song.wav'})===false);var denied=0;Ouse.request({url:'x'}).catch(e=>denied++);");evaluate(&app,"check(denied===1);");assert(media_calls==1);app.network_allowed=true;
    evaluate(&app,"Ouse.request({url:'https://example.test/late'}).then(()=>{throw Error('old instance result');});");assert(app.references==2);
    ouse_task_owner_close(&app.owner);cancel_net_jobs(&app);assert(app.references==1&&net_job_count==1);JS_FreeContext(app.context);
    setup(&app,runtime);complete_native(3);drain_net_completions();assert(!net_job_count&&app.references==1);evaluate(&app,"check(Ouse.mediaGet()===null);");
    ouse_task_owner_close(&app.owner);JS_FreeContext(app.context);JS_FreeRuntime(runtime);vQueueDelete(net_completions);
    puts("PASS genuine QuickJS services: promise taskId/payload, suspended delivery, slot release, throwing/reentrant getters, capability gating, clock/alarms/media contracts, old-context disposal and reopened isolation");
}
