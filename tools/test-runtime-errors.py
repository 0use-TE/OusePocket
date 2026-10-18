"""Exercise production timer bindings, exception formatting and fault page with real QuickJS/LVGL."""
from pathlib import Path
import os
import subprocess
root=Path(__file__).resolve().parents[1]
output=root/'.tools/runtime-error-check';output.mkdir(parents=True,exist_ok=True)
source=(root/'components/ouse_runtime/ouse_runtime.c').read_text(encoding='utf-8')
timers=source[source.index('static JSValue timer_create('):source.index('static JSValue theme_snapshot(')]
exception=source[source.index('static void exception('):source.index('static bool result_ok(')]
page=source[source.index('static void show_fault('):source.index('static void enforce_permissions(')]
bindings=source[source.index('    JSValue timers=JS_NewObject(context);'):source.index('    JS_SetPropertyStr(context,api,"apiVersion"')]
fixture=r'''
#include "quickjs.h"
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#define MAX_TIMERS 8
typedef struct {uint32_t id,period,due,remaining;bool interval;JSValue handler;} script_timer_t;
typedef struct {JSContext *context;bool interrupted;char id[48],fault_message[1024];script_timer_t timers[8];uint32_t next_timer;lv_obj_t *root;} instance_t;
static instance_t *from_js(JSContext *context){return JS_GetContextOpaque(context);}
static uint32_t now_ms(void){return 0;}
static void begin_call(instance_t *app){app->interrupted=false;}
static void fault(instance_t *app,const char *message){snprintf(app->fault_message,sizeof(app->fault_message),"%s",message);}
static void model_lock(instance_t *app){(void)app;}
static void model_unlock(instance_t *app){(void)app;}
static int ouse_ui_dp(int value){return value;}
static void ouse_ime_hide_root(lv_obj_t *root){(void)root;}
static void ouse_theme_apply(lv_obj_t *object,int role,int selector){(void)object;(void)role;(void)selector;}
static void ouse_ui_use_font(lv_obj_t *object,unsigned size){(void)size;lv_obj_set_style_text_font(object,&lv_font_montserrat_16,0);}
static void ouse_ui_text(lv_obj_t *label,const char *text){lv_label_set_text(label,text);}
#define OUSE_STYLE_APP 0
#define OUSE_STYLE_BUTTON 0
static void home_event(lv_event_t *event){(void)event;}
static lv_obj_t *find_label(lv_obj_t *object,const char *text){
    if(lv_obj_check_type(object,&lv_label_class) && strstr(lv_label_get_text(object),text))return object;
    for(unsigned index=0;index<lv_obj_get_child_count(object);++index){lv_obj_t *found=find_label(lv_obj_get_child(object,index),text);if(found)return found;}
    return NULL;
}
'''
tests=r'''
int main(void){
    JSRuntime *runtime=JS_NewRuntime();JSContext *context=JS_NewContext(runtime);assert(runtime&&context);
    instance_t app={.context=context,.id="user.error-test"};for(unsigned index=0;index<8;++index)app.timers[index].handler=JS_UNDEFINED;
    JS_SetContextOpaque(context,&app);JSValue api=JS_NewObject(context);
    /* BINDINGS */
    JSValue global=JS_GetGlobalObject(context);JS_SetPropertyStr(context,global,"Ouse",api);JS_FreeValue(context,global);
    const char *script="let a=Ouse.timers.setInterval(()=>{},100);Ouse.timers.clearInterval(a);let b=Ouse.timers.setTimeout(()=>{},100);Ouse.timers.clearTimeout(b);let c=Ouse.timers.setInterval(()=>{},100);Ouse.timers.clear(c);";
    JSValue value=JS_Eval(context,script,strlen(script),"timer-aliases.js",JS_EVAL_TYPE_GLOBAL);assert(!JS_IsException(value));JS_FreeValue(context,value);
    for(unsigned index=0;index<8;++index)assert(!app.timers[index].id);
    script="function afterPermission(){throw new TypeError('permission callback regression');} afterPermission();";
    value=JS_Eval(context,script,strlen(script),"user.error-test/app.js",JS_EVAL_TYPE_GLOBAL);assert(JS_IsException(value));JS_FreeValue(context,value);exception(&app);
    assert(strstr(app.fault_message,"TypeError")&&strstr(app.fault_message,"permission callback regression")&&strstr(app.fault_message,"afterPermission")&&strstr(app.fault_message,"app.js"));
    lv_init();lv_display_t *display=lv_display_create(320,480);assert(display);app.root=lv_obj_create(lv_screen_active());lv_obj_set_size(app.root,320,416);
    show_fault(&app);lv_obj_update_layout(app.root);
    assert(find_label(app.root,"应用运行出错")&&find_label(app.root,"user.error-test")&&find_label(app.root,"afterPermission")&&find_label(app.root,"回到桌面"));
    lv_obj_t *details=lv_obj_get_parent(find_label(app.root,"afterPermission"));assert(lv_obj_has_flag(details,LV_OBJ_FLAG_SCROLLABLE)&&lv_obj_get_scroll_dir(details)==LV_DIR_VER);
    char long_error[3000];memset(long_error,'x',sizeof(long_error)-1);long_error[sizeof(long_error)-1]=0;
    JS_ThrowInternalError(context,"%s",long_error);exception(&app);assert(strlen(app.fault_message)<sizeof(app.fault_message));
    lv_display_delete(display);JS_FreeContext(context);JS_FreeRuntime(runtime);
    puts("PASS real QuickJS timer clear/clearInterval/clearTimeout aliases; bounded error + function/file stack; real LVGL scrollable diagnostic page and Home button");return 0;
}
'''
fixture+=timers+exception+page+tests.replace('/* BINDINGS */',bindings)
(output/'check.c').write_text(fixture,encoding='utf-8')
environment=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(root/'.tools/ouse-kernel-check/environment.cmd')],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1);environment[key]=value
compiler=Path(environment['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
host=root/'.tools/ouse-core-check';lvgl=root/'managed_components/lvgl__lvgl';quickjs=root/'managed_components/espressif__quickjs-ng/quickjs-ng'
command=[str(compiler),'/nologo','/std:c11','/utf-8','/O2','/MD','/UNDEBUG','/D_CRT_SECURE_NO_WARNINGS','/DLV_KCONFIG_IGNORE','/DLV_LVGL_H_INCLUDE_SIMPLE','/DLV_CONF_PATH="'+(host/'lv_conf.h').as_posix()+'"']
command+=['/I'+str(path) for path in [host,lvgl,quickjs]]
command+=[str(output/'check.c'),str(host/'lvgl-native.lib'),str(root/'.tools/ouse-runtime-check/quickjs-native.lib'),'/Fe'+str(output/'check.exe')]
subprocess.run(command,cwd=output,env=environment,check=True)
subprocess.run([str(output/'check.exe')],cwd=output,check=True,timeout=20)
