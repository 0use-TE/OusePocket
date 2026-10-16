"""Run the production task/background bindings on genuine QuickJS, without LVGL or hardware."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".tools/ouse-kernel-check"
QJS = ROOT / "managed_components/espressif__quickjs-ng/quickjs-ng"
LIB = ROOT / ".tools/ouse-runtime-check/quickjs-native.lib"
assert (OUT/"esp_err.h").exists(), "Run test-ouse-kernel.py first"
env = dict(os.environ)
for line in subprocess.check_output(["cmd.exe", "/d", "/c", str(OUT/"environment.cmd")], encoding="mbcs").splitlines():
    if "=" in line and not line.startswith("="):
        key, value = line.split("=", 1)
        env[key] = value
compiler = Path(env["VCToolsInstallDir"]) / "bin/Hostx64/x64/cl.exe"
common = [str(compiler), "/nologo", "/std:c11", "/O2", "/MD", "/utf-8", "/D_CRT_SECURE_NO_WARNINGS",
    "/D_GNU_SOURCE", "/DQUICKJS_NG_BUILD", "/I"+str(OUT), "/I"+str(QJS),
    "/I"+str(ROOT/"components/ouse_tasks/include"), "/I"+str(ROOT/"components/ouse_runtime")]
if not LIB.exists():
    LIB = OUT / "quickjs-native.lib"
    sources = [QJS/name for name in ["quickjs.c", "dtoa.c", "libregexp.c", "libunicode.c"]]
    subprocess.run(common+["/c"]+list(map(str,sources)), cwd=OUT, env=env, check=True, timeout=180)
    subprocess.run([str(compiler.parent/"lib.exe"), "/nologo", "/OUT:"+str(LIB)]+[str(OUT/(p.stem+".obj")) for p in sources], cwd=OUT, env=env, check=True)
runtime = (ROOT/"components/ouse_runtime/ouse_runtime.c").read_text(encoding="utf-8")
bindings = runtime[runtime.index("static JSValue lifecycle_snapshot("):runtime.index("static JSValue system_info(")]
storage = runtime[runtime.index("static void storage_job_release("):runtime.index("static void storage_completed(")]
interrupt = runtime[runtime.index("static int interrupt("):runtime.index("static void begin_call(")]
fixture = r'''
#include "quickjs.h"
#include "ouse_tasks.h"
#include "ouse_budget.h"
#include <windows.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum {NAV_IDLE};
#define MAX_STORAGE_PENDING 8
typedef struct storage_job storage_job_t;
typedef struct {
    JSRuntime *runtime;JSContext *context;ouse_task_owner_t owner;ouse_budget_t budget;
    bool visible,background_active,closed,faulted,background_allowed,interrupted;
    unsigned lifecycle_revision,navigation_stage;
    char background_reason[64];int64_t background_since_us;
    JSValue lifecycle_handler,task_handler;
    unsigned references;storage_job_t *storage[MAX_STORAGE_PENDING];
} instance_t;
struct storage_job {instance_t *app;unsigned slot;uint32_t token;JSValue resolve,reject;char *result;esp_err_t error;bool completed;};
static unsigned storage_job_count;
static void release_instance(instance_t *app){assert(app->references>0);--app->references;}
/* __NATIVE_STORAGE__ */
static instance_t *from_js(JSContext *context){return JS_GetContextOpaque(context);}
static void model_lock(instance_t *app){(void)app;}
static void model_unlock(instance_t *app){(void)app;}
int64_t esp_timer_get_time(void){LARGE_INTEGER n,f;QueryPerformanceCounter(&n);QueryPerformanceFrequency(&f);return n.QuadPart/f.QuadPart*1000000+n.QuadPart%f.QuadPart*1000000/f.QuadPart;}
static bool copy_string(JSContext *context,JSValueConst value,char *out,size_t capacity){
    if(!JS_IsString(value))return false;size_t length;const char *text=JS_ToCStringLen(context,&length,value);if(!text)return false;
    bool ok=length<capacity && !memchr(text,0,length);if(ok)memcpy(out,text,length+1);JS_FreeCString(context,text);return ok;
}
const char *esp_err_to_name(esp_err_t error){return error==ESP_OK ? "ESP_OK" : "ESP_ERROR";}
static JSValue call_js(instance_t *app,const char *kind,const char *name,JSValueConst fn,JSValueConst self,int argc,JSValueConst *argv){(void)kind;(void)name;return JS_Call(app->context,fn,self,argc,argv);}
static bool result_ok(instance_t *app,JSValue result){bool ok=!JS_IsException(result);JS_FreeValue(app->context,result);if(!ok){JSValue error=JS_GetException(app->context);JS_FreeValue(app->context,error);app->faulted=true;}return ok;}
INTERRUPT
BINDINGS
static void evaluate(instance_t *app,const char *source){
    ouse_budget_begin(&app->budget,100000,true);
    JSValue result=JS_Eval(app->context,source,strlen(source),"contract.js",JS_EVAL_TYPE_GLOBAL);
    if(JS_IsException(result)){JSValue error=JS_GetException(app->context);const char *message=JS_ToCString(app->context,error);fprintf(stderr,"%s\n",message);assert(!"JS contract failed");}
    JS_FreeValue(app->context,result);
}
static void init(instance_t *app){
    memset(app,0,sizeof(*app));app->references=1;app->visible=true;app->background_allowed=true;
    assert(ouse_task_owner_open("user.kernel",&app->owner)==ESP_OK);
    app->runtime=JS_NewRuntime();JS_SetMemoryLimit(app->runtime,512*1024);app->context=JS_NewContext(app->runtime);
    app->task_handler=app->lifecycle_handler=JS_UNDEFINED;JS_SetContextOpaque(app->context,app);JS_SetInterruptHandler(app->runtime,interrupt,app);
    JSValue global=JS_GetGlobalObject(app->context),api=JS_NewObject(app->context),tasks=JS_NewObject(app->context),background=JS_NewObject(app->context);
    const char *methods[]={"list","get","cancel","onComplete"};
    for(int i=0;i<4;++i)JS_SetPropertyStr(app->context,tasks,methods[i],JS_NewCFunctionMagic(app->context,tasks_api,methods[i],i?1:0,JS_CFUNC_generic_magic,i));
    JS_SetPropertyStr(app->context,api,"tasks",tasks);
    JS_SetPropertyStr(app->context,api,"systemTasks",JS_NewCFunctionMagic(app->context,tasks_api,"systemTasks",0,JS_CFUNC_generic_magic,4));
    const char *bg[]={"end","begin","isActive","get"};
    for(int i=0;i<4;++i)JS_SetPropertyStr(app->context,background,bg[i],JS_NewCFunctionMagic(app->context,background_api,bg[i],0,JS_CFUNC_generic_magic,i));
    JS_SetPropertyStr(app->context,api,"background",background);JS_SetPropertyStr(app->context,global,"Ouse",api);JS_FreeValue(app->context,global);
    evaluate(app,"function assert(value){if(!value)throw Error('assert failed')} let events=[];Ouse.tasks.onComplete(task=>events.push(task));");
}
static void deliver(instance_t *app){ouse_budget_begin(&app->budget,12000,false);task_completion_events(app);}
static void destroy(instance_t *app){ouse_task_owner_close(&app->owner);JS_FreeValue(app->context,app->task_handler);JS_FreeValue(app->context,app->lifecycle_handler);JS_FreeContext(app->context);JS_FreeRuntime(app->runtime);}
int main(void){
    instance_t old,next;init(&old);
    evaluate(&old,"assert(Ouse.background.begin('test'));assert(Ouse.background.get().reason==='test');assert(Ouse.background.get().runnable);assert(!Ouse.background.end());assert(!Ouse.background.isActive());");
    old.background_allowed=false;evaluate(&old,"assert(!Ouse.background.begin('denied'));let threw=false;try{Ouse.tasks.list(0,17)}catch(e){threw=true}assert(threw);");old.background_allowed=true;
    uint32_t id;assert(ouse_task_create(&old.owner,OUSE_TASK_AUDIO,"record",OUSE_TASK_STOP_ON_CLOSE,true,&id)==ESP_OK);
    assert(ouse_task_start(id,true));ouse_task_result_t result={.present=true,.value.audio={.recorded_ms=123,.position_ms=456}};
    ouse_task_finish_result(id,ESP_OK,false,&result);
    old.visible=false;deliver(&old);
    evaluate(&old,"assert(events.length===0);assert(Ouse.tasks.list().length===1);assert(Ouse.tasks.list()[0].result.recordedMs===123);");
    old.visible=true;deliver(&old);deliver(&old);
    evaluate(&old,"assert(events.length===1);assert(events[0].result.positionMs===456);assert(events[0].state==='succeeded');");
    // A new same-app context cannot query/cancel/receive the old instance's work.
    assert(ouse_task_create(&old.owner,OUSE_TASK_STORAGE,"set",OUSE_TASK_FINISH_ON_CLOSE,true,&id)==ESP_OK);assert(ouse_task_start(id,false));
    storage_job_t *detached=calloc(1,sizeof(*detached));detached->app=&old;detached->slot=0;old.storage[0]=detached;
    JSValue handlers[2],promise=JS_NewPromiseCapability(old.context,handlers);detached->resolve=handlers[0];detached->reject=handlers[1];
    ++old.references;++storage_job_count;cancel_storage_jobs(&old);JS_FreeValue(old.context,promise);
    assert(old.references==1 && detached->app==NULL && storage_job_count==1);
    old.closed=true;old.visible=false;ouse_task_owner_close(&old.owner);init(&next);ouse_task_finish(id,ESP_OK,false);deliver(&next);
    char source[200];snprintf(source,sizeof(source),"assert(events.length===0);assert(Ouse.tasks.get(%u)===null);assert(!Ouse.tasks.cancel(%u));assert(Ouse.systemTasks().length>=2);",id,id);evaluate(&next,source);
    evaluate(&next,"let pageFailed=false;try{Ouse.tasks.get(-1)}catch(e){pageFailed=true}assert(pageFailed);");
    assert(ouse_task_create(&next.owner,OUSE_TASK_AUDIO,"testTone",OUSE_TASK_STOP_ON_CLOSE,true,&id)==ESP_OK);assert(ouse_task_start(id,true));ouse_task_finish(id,ESP_OK,false);
    evaluate(&next,"Ouse.tasks.onComplete(()=>{for(;;){}})");
    int64_t started=esp_timer_get_time();deliver(&next);assert(next.interrupted && next.faulted && esp_timer_get_time()-started<1000000);
    destroy(&old);storage_job_release(detached);assert(!storage_job_count);destroy(&next);
    puts("PASS genuine QuickJS: production bindings, typed results, background reason, suspended/exactly-once delivery, close/reopen ACL, pagination, detached storage release, infinite callback isolation");
    return 0;
}
'''.replace("INTERRUPT",interrupt).replace("BINDINGS",bindings).replace("/* __NATIVE_STORAGE__ */",storage)
(OUT/"js-check.c").write_text(fixture, encoding="utf-8")
sources=[OUT/"js-check.c",ROOT/"components/ouse_tasks/ouse_tasks.c",ROOT/"components/ouse_runtime/ouse_budget.c"]
result=subprocess.run(common+list(map(str,sources))+[str(LIB),"Ws2_32.lib","/Fe"+str(OUT/"js-check.exe")],cwd=OUT,env=env,capture_output=True,text=True,errors="replace")
assert result.returncode==0,(result.stdout,result.stderr)
subprocess.run([str(OUT/"js-check.exe")],cwd=OUT,check=True,timeout=15)
