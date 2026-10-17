"""Validate delivered BPKs with the real ZIP parser, QuickJS and node schema.

No application code is executed here; the application behavior test is separate.
The schema is extracted verbatim from the production runtime, without LVGL I/O.
"""
from pathlib import Path
import os, subprocess
import bpk
import argparse

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/ouse-app-packages-check';OUT.mkdir(exist_ok=True)
QJS=ROOT/'managed_components/espressif__quickjs-ng/quickjs-ng'
LVGL=ROOT/'managed_components/lvgl__lvgl'
HOST=ROOT/'.tools/ouse-core-check'
runtime=(ROOT/'components/ouse_runtime/ouse_runtime.c').read_text(encoding='utf-8')
def section(start,end):return runtime[runtime.index(start):runtime.index(end,runtime.index(start))]
types=section('typedef enum { CONTAINER','typedef struct {\n    JSValue handler;')
helpers=section('static bool copy_string(','static node_t *find_node(')
widgets=section('static bool modal_widget(','static bool node_visible_locked(')
schema=section('static bool string_property(','static JSValue ui_set_style(')
nodes=section('static bool parse_node(','// Only run before initial readiness')
fixture=r'''
#include "quickjs.h"
#include "ouse_bpk.h"
#include "ouse_ime.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define MAX_TEXT 256
#define MAX_NODES 128
/* TYPES */
typedef struct {
 JSContext *context;bool dp_units,parsing_page,interrupted;int budget;
 unsigned node_count,image_count;node_t nodes[MAX_NODES];ouse_script_image_t images[16];
 struct {int app_width,app_height;unsigned density;} worker_ui;
 JSValue translations;
} instance_t;
static bool ouse_budget_exceeded(int *unused){(void)unused;return false;}
/* HELPERS */
/* WIDGETS */
/* SCHEMA */
/* NODES */
esp_err_t ouse_script_prepare(ouse_script_app_t *state,const ouse_script_package_t *package,ouse_app_definition_t *definition)
{assert(state&&package&&definition&&package->api==1);return ESP_OK;}
int main(int argc,char **argv){
 assert(argc>=2);
 for(int index=1;index<argc;++index){
  FILE *file=fopen(argv[index],"rb");assert(file);fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);
  unsigned char *bytes=malloc(size+1);assert(bytes&&fread(bytes,1,size,file)==(size_t)size);fclose(file);
  ouse_bpk_t *package=NULL;assert(ouse_bpk_parse(bytes,size,&package)==ESP_OK&&package);
  bytes[size]=0;ouse_bpk_t *invalid=NULL;assert(ouse_bpk_parse(bytes,size+1,&invalid)!=ESP_OK&&!invalid);free(bytes);
  if(!strcmp(package->id,"user.music")){assert(package->package.image_count==1 && package->images[0].image->header.w==128);assert(package->package.files_allowed && !package->package.sdcard_allowed);}
  if(!strcmp(package->id,"user.explorer")){assert(package->package.sdcard_allowed && !package->package.files_allowed && !package->package.network_allowed);}
  JSRuntime *rt=JS_NewRuntime();assert(rt);JS_SetMemoryLimit(rt,OUSE_RUNTIME_MEMORY_LIMIT);JSContext *ctx=JS_NewContext(rt);assert(ctx);
  JSValue compiled=JS_Eval(ctx,package->script,strlen(package->script),"app.js",JS_EVAL_TYPE_GLOBAL|JS_EVAL_FLAG_COMPILE_ONLY);assert(!JS_IsException(compiled));JS_FreeValue(ctx,compiled);
  JSValue doc=JS_ParseJSON(ctx,package->page,strlen(package->page),"page.json");assert(!JS_IsException(doc));
  JSValue pages=JS_GetPropertyStr(ctx,doc,"pages"),length=JS_GetPropertyStr(ctx,pages,"length");unsigned count=0;assert(JS_ToUint32(ctx,&count,length)==0&&count<=16&&count);JS_FreeValue(ctx,length);
  if(JS_IsUndefined(pages))count=1;
  for(unsigned page=0;page<count;++page){
   JSValue entry=count==1&&JS_IsUndefined(pages)?JS_DupValue(ctx,doc):JS_GetPropertyUint32(ctx,pages,page);
   JSValue root=JS_GetPropertyStr(ctx,entry,"root");instance_t *app=calloc(1,sizeof(*app));assert(app);
   app->context=ctx;app->dp_units=true;app->parsing_page=true;app->worker_ui.app_width=320;app->worker_ui.app_height=416;app->worker_ui.density=160;
   app->image_count=package->package.image_count;memcpy(app->images,package->package.images,app->image_count*sizeof(app->images[0]));
   app->translations=JS_UNDEFINED;
   bool valid=parse_node(app,root,-1,0);
   if(!valid)fprintf(stderr,"invalid %s page %u at node %u (%s)\n",package->id,page,app->node_count,app->node_count?app->nodes[app->node_count-1].model.id:"");
   assert(valid);free(app);JS_FreeValue(ctx,root);JS_FreeValue(ctx,entry);
  }
  JS_FreeValue(ctx,pages);JS_FreeValue(ctx,doc);JS_FreeContext(ctx);JS_FreeRuntime(rt);ouse_bpk_free(package);
 }
 puts("PASS current BPKs: native ZIP/resource limits, SD capability isolation, QuickJS compile, production node schemas and trailing-byte rejection");
}
'''
for name,value in [('TYPES',types),('HELPERS',helpers),('WIDGETS',widgets),('SCHEMA',schema),('NODES',nodes)]:fixture=fixture.replace('/* '+name+' */',value)
# The service test is a single-root document, handled before array operations.
fixture=fixture.replace('JSValue pages=JS_GetPropertyStr(ctx,doc,"pages"),length=JS_GetPropertyStr(ctx,pages,"length");unsigned count=0;assert(JS_ToUint32(ctx,&count,length)==0&&count<=16&&count);JS_FreeValue(ctx,length);',
 'JSValue pages=JS_GetPropertyStr(ctx,doc,"pages");unsigned count=1;if(!JS_IsUndefined(pages)){JSValue length=JS_GetPropertyStr(ctx,pages,"length");assert(JS_ToUint32(ctx,&count,length)==0&&count<=16&&count);JS_FreeValue(ctx,length);}')
(OUT/'packages.c').write_text(fixture,encoding='utf-8')
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(ROOT/'.tools/ouse-kernel-check/environment.cmd')],encoding='mbcs').splitlines():
 if '=' in line and not line.startswith('='):
  key,value=line.split('=',1);env[key]=value
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
includes=[HOST,LVGL,QJS,ROOT/'components/ouse_core/include',ROOT/'components/ouse_runtime/include',ROOT/'components/ouse_installer',ROOT/'components/ouse_system/include',ROOT/'components/ouse_tasks/include']
command=[str(compiler),'/nologo','/std:c11','/O2','/MD','/utf-8','/D_CRT_SECURE_NO_WARNINGS','/D_GNU_SOURCE','/DQUICKJS_NG_BUILD','/DLV_KCONFIG_IGNORE','/DLV_LVGL_H_INCLUDE_SIMPLE','/DLV_CONF_PATH="'+(HOST/'lv_conf.h').as_posix()+'"']
command+=['/I'+str(p) for p in includes]
command+=list(map(str,[OUT/'packages.c',ROOT/'components/ouse_installer/ouse_bpk.c',ROOT/'components/ouse_installer/ouse_protocol.c',ROOT/'.tools/ouse-runtime-check/quickjs-native.lib']))+['/Fe'+str(OUT/'packages.exe')]
result=subprocess.run(command,cwd=OUT,env=env,capture_output=True,text=True,errors='replace')
assert result.returncode==0,(result.stdout,result.stderr)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--apps',nargs='+',default=['music','alarm','service-test','explorer'])
names=parser.parse_args().apps
for name in names:bpk.pack(ROOT/'apps'/name,OUT/f'{name}.bpk')
subprocess.run([str(OUT/'packages.exe')]+[str(OUT/f'{name}.bpk') for name in names],cwd=OUT,check=True,timeout=15)
