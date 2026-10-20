"""Real QuickJS settings request gate plus the production UI routing function."""
from pathlib import Path
import os,subprocess
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'.tools/settings-runtime-check';OUT.mkdir(parents=True,exist_ok=True)
source=(ROOT/'components/ouse_runtime/ouse_runtime.c').read_text(encoding='utf-8')
implementation=source[source.index('static JSValue native_open_settings('):source.index('static JSValue lifecycle_snapshot(')]
assert 'ouse_core_open_settings(settings_target,app->id)' in source
core=(ROOT/'components/ouse_core/ouse_core.c').read_text(encoding='utf-8')
routing=core[core.index('esp_err_t ouse_core_open_settings('):core.index('static void refresh_status(')]
fixture=r'''
#include "quickjs.h"
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {bool validation_only,visible,closed,faulted,home_requested;char launch_requested[48];unsigned settings_requested;} instance_t;
static instance_t *from_js(JSContext *ctx){return JS_GetContextOpaque(ctx);}
static void model_lock(instance_t *a){}
static void model_unlock(instance_t *a){}
static bool copy_string(JSContext *ctx,JSValueConst v,char *out,size_t cap){if(!JS_IsString(v))return false;const char *s=JS_ToCString(ctx,v);bool ok=s&&strlen(s)<cap;if(ok)strcpy(out,s);JS_FreeCString(ctx,s);return ok;}
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
static char settings_origin[48],selected[48];static unsigned opened,page;static bool blocked;
static bool ouse_core_access_blocked(void){return blocked;}
static int ouse_app_open(const char *id){assert(!strcmp(id,"ouse.settings"));opened++;return 0;}
static void select_settings_page(unsigned p){page=p;}
static void ouse_manager_ui_select(const char *id){snprintf(selected,sizeof(selected),"%s",id);}
'''
tests=r'''
static bool evaluate(JSContext *ctx,const char *s){JSValue v=JS_Eval(ctx,s,strlen(s),"settings-test",JS_EVAL_TYPE_GLOBAL);assert(!JS_IsException(v));bool result=JS_ToBool(ctx,v)==1;JS_FreeValue(ctx,v);return result;}
int main(void){JSRuntime *rt=JS_NewRuntime();JSContext *ctx=JS_NewContext(rt);instance_t app={.visible=true};JS_SetContextOpaque(ctx,&app);JSValue global=JS_GetGlobalObject(ctx);JS_SetPropertyStr(ctx,global,"openSettings",JS_NewCFunction(ctx,native_open_settings,"openSettings",1));JS_FreeValue(ctx,global);
 assert(evaluate(ctx,"openSettings('permissions')"));assert(app.settings_requested==1&&!opened);
 assert(evaluate(ctx,"openSettings('bluetooth')"));assert(app.settings_requested==2);
 assert(evaluate(ctx,"openSettings('wifi')"));assert(app.settings_requested==3);
 assert(evaluate(ctx,"(()=>{try{openSettings('other-app');return false}catch(e){return e instanceof TypeError}})()"));
 app.settings_requested=0;app.visible=false;assert(!evaluate(ctx,"openSettings('permissions')"));assert(!app.settings_requested);
 app.visible=true;app.validation_only=true;assert(!evaluate(ctx,"openSettings('permissions')"));app.validation_only=false;app.closed=true;assert(!evaluate(ctx,"openSettings('bluetooth')"));app.closed=false;app.faulted=true;assert(!evaluate(ctx,"openSettings('wifi')"));
 assert(ouse_core_open_settings(1,"user.example")==0&&page==12&&!strcmp(selected,"user.example"));
 assert(ouse_core_open_settings(2,"user.example")==0&&page==13&&!strcmp(settings_origin,"user.example"));
 assert(ouse_core_open_settings(3,"user.example")==0&&page==7);
 unsigned before=opened;blocked=true;assert(ouse_core_open_settings(1,"user.example")!=0&&opened==before);blocked=false;assert(ouse_core_open_settings(1,"ouse.settings")!=0&&opened==before);assert(ouse_core_open_settings(4,"user.example")!=0);
 JS_FreeContext(ctx);JS_FreeRuntime(rt);puts("PASS real QuickJS settings API: validated routes, deferred/foreground/closed/fault gates, own-app target binding, lock gate and native settings routes");}
'''
(OUT/'check.c').write_text(fixture+implementation+routing+tests,encoding='utf-8')
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(ROOT/'.tools/ouse-core-check/environment.cmd')],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        k,v=line.split('=',1);env[k]=v
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
subprocess.run([str(compiler),'/nologo','/std:c11','/utf-8','/O2','/MD','/UNDEBUG','/D_CRT_SECURE_NO_WARNINGS','/I'+str(ROOT/'managed_components/espressif__quickjs-ng/quickjs-ng'),str(OUT/'check.c'),str(ROOT/'.tools/ouse-runtime-check/quickjs-native.lib'),'/Fe'+str(OUT/'check.exe')],cwd=OUT,env=env,check=True)
subprocess.run([str(OUT/'check.exe')],cwd=OUT,check=True,timeout=20)
