"""Test the production bounded image staging with real QuickJS and LVGL."""
from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parents[1]
out=root/'.tools/image-updates-check';out.mkdir(exist_ok=True)
source=(root/'components/ouse_runtime/ouse_runtime.c').read_text(encoding='utf-8')
functions=source[source.index('static JSValue ui_set_image_data('):source.index('static JSValue assets_api(')]
fixture=r'''
#include "quickjs.h"
#include "lvgl.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
enum {IMAGE};
typedef struct {struct {int type;char image[32];unsigned image_revision;}model;}node_t;
typedef struct {bool closed,faulted,validation_only;node_t nodes[1];unsigned node_count,image_count;char image_ids[16][32];lv_image_dsc_t image_descriptors[16];unsigned char *pending_images[16];}instance_t;
static instance_t app;
static instance_t *from_js(JSContext *context){(void)context;return &app;}
static node_t *find_node(instance_t *instance,JSValueConst id){(void)id;return &instance->nodes[0];}
static void model_lock(instance_t *instance){(void)instance;}
static void model_unlock(instance_t *instance){(void)instance;}
static void *ouse_psram_malloc(size_t size){return malloc(size);}
/* FUNCTIONS */
int main(void){
 lv_init();JSRuntime *runtime=JS_NewRuntime();JSContext *context=JS_NewContext(runtime);assert(context);app.node_count=app.image_count=1;strcpy(app.image_ids[0],"slot0");strcpy(app.nodes[0].model.image,"slot0");app.image_descriptors[0]=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w=1,.h=1,.stride=4},.data_size=4,.data=calloc(1,4)};
 unsigned char bytes[]={ 'O','I','C','1',2,0,1,0,10,20,30,40,50,60,70,80};JSValue arguments[]={JS_NewString(context,"icon0"),JS_NewArrayBufferCopy(context,bytes,sizeof(bytes))};JSValue result=ui_set_image_data(context,JS_UNDEFINED,2,arguments);assert(JS_ToBool(context,result)==1&&app.pending_images[0]);JS_FreeValue(context,result);assert(((unsigned char *)app.image_descriptors[0].data)[0]==0);pending_images_pump(&app);assert(!app.pending_images[0]&&app.nodes[0].model.image_revision==1&&((unsigned char *)app.image_descriptors[0].data)[0]==10&&((unsigned char *)app.image_descriptors[0].data)[3]==40);
 for(unsigned index=0;index<20;++index){result=ui_set_image_data(context,JS_UNDEFINED,2,arguments);assert(JS_ToBool(context,result)==1);JS_FreeValue(context,result);}pending_images_pump(&app);assert(app.nodes[0].model.image_revision==2);
 JS_FreeValue(context,arguments[1]);bytes[4]=97;arguments[1]=JS_NewArrayBufferCopy(context,bytes,sizeof(bytes));result=ui_set_image_data(context,JS_UNDEFINED,2,arguments);assert(JS_IsException(result));JS_FreeValue(context,JS_GetException(context));JS_FreeValue(context,result);app.closed=true;result=ui_set_image_data(context,JS_UNDEFINED,2,arguments);assert(JS_ToBool(context,result)==0);JS_FreeValue(context,result);
 JS_FreeValue(context,arguments[0]);JS_FreeValue(context,arguments[1]);free((void *)app.image_descriptors[0].data);JS_FreeContext(context);JS_FreeRuntime(runtime);lv_deinit();puts("PASS dynamic OIC1: bounded validation, BGRA/alpha, UI-owned swap, repeated replacement and closed-owner rejection");return 0;
}
'''.replace('/* FUNCTIONS */',functions)
(out/'check.c').write_text(fixture,encoding='utf-8')
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(root/'.tools/ouse-kernel-check/environment.cmd')],encoding='mbcs').splitlines():
 if '=' in line and not line.startswith('='):
  key,value=line.split('=',1);env[key]=value
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe';host=root/'.tools/ouse-core-check';qjs=root/'managed_components/espressif__quickjs-ng/quickjs-ng';lvgl=root/'managed_components/lvgl__lvgl'
command=[str(compiler),'/nologo','/std:c11','/O2','/MD','/utf-8','/D_CRT_SECURE_NO_WARNINGS','/DLV_KCONFIG_IGNORE','/DLV_LVGL_H_INCLUDE_SIMPLE','/DLV_CONF_PATH="'+(host/'lv_conf.h').as_posix()+'"']+['/I'+str(path) for path in [host,qjs,lvgl]]+[str(out/'check.c'),str(host/'lvgl-native.lib'),str(root/'.tools/ouse-runtime-check/quickjs-native.lib'),'/Fe'+str(out/'check.exe')]
subprocess.run(command,cwd=out,env=env,check=True);subprocess.run([str(out/'check.exe')],cwd=out,check=True,timeout=10)
