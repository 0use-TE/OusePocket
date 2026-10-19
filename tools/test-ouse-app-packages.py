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
#define OUSE_CAMERA_IMAGES 14
static const char *const native_image_names[14]={"@camera","@photo","@thumb0","@thumb1","@thumb2","@thumb3","@thumb4","@thumb5","@thumb6","@thumb7","@thumb8","@thumb9","@thumb10","@thumb11"};
/* TYPES */
typedef struct {
 JSContext *context;bool dp_units,parsing_page,interrupted;int budget;
 unsigned node_count,image_count;node_t nodes[MAX_NODES];ouse_script_image_t images[16];
 bool camera_allowed,photos_allowed;lv_image_dsc_t native_images[14];
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
static esp_err_t test_loader(const char *id,uint32_t version,const ouse_script_package_t **out,void **handle)
{(void)id;(void)version;(void)out;(void)handle;return ESP_FAIL;}
static void test_release(void *handle){(void)handle;}
int main(int argc,char **argv){
 assert(argc>=2);
 for(int index=1;index<argc;++index){
  FILE *file=fopen(argv[index],"rb");assert(file);fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);
  unsigned char *bytes=malloc(size+1);assert(bytes&&fread(bytes,1,size,file)==(size_t)size);fclose(file);
  ouse_bpk_t *package=NULL;
  if(strstr(argv[index],".reject.bpk")){assert(ouse_bpk_parse(bytes,size,&package)!=ESP_OK&&!package);free(bytes);continue;}
  assert(ouse_bpk_parse(bytes,size,&package)==ESP_OK&&package);
  ouse_bpk_t *catalog=NULL;assert(ouse_bpk_catalog(bytes,size,&catalog)==ESP_OK&&catalog);
  assert(!catalog->page&&!catalog->script&&!catalog->package.image_count&&!catalog->package.asset_count);
  assert(!strcmp(catalog->id,package->id)&&catalog->package.source_version==package->package.source_version&&catalog->icon_image.data_size==package->icon_image.data_size);
  assert(!strcmp(catalog->description,package->description)&&!strcmp(catalog->author,package->author)&&!strcmp(catalog->released_at,package->released_at));
  if(!strcmp(package->id,"user.store")){assert(!strcmp(package->author,"OusePocket")&&!strcmp(package->released_at,"2026-10-19")&&package->description[0]);}
  ouse_bpk_free(catalog);
  bytes[size]=0;ouse_bpk_t *invalid=NULL;assert(ouse_bpk_parse(bytes,size+1,&invalid)!=ESP_OK&&!invalid);free(bytes);
  if(!strcmp(package->id,"user.music")){assert(package->package.image_count==1 && package->images[0].image->header.w==128);assert(package->package.files_allowed && !package->package.sdcard_allowed);}
  if(!strcmp(package->id,"user.explorer")){assert(package->package.sdcard_allowed && !package->package.files_allowed && !package->package.network_allowed);}
  if(!strcmp(package->id,"user.camera")){assert(package->package.camera_allowed && package->package.files_allowed && package->package.photos_allowed);}
  if(!strcmp(package->id,"user.gallery")){assert(package->package.photos_allowed && !package->package.camera_allowed && !package->package.files_allowed);}
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
   app->camera_allowed=package->package.camera_allowed;app->photos_allowed=package->package.photos_allowed;
   app->translations=JS_UNDEFINED;
   bool valid=parse_node(app,root,-1,0);
   if(!valid)fprintf(stderr,"invalid %s page %u at node %u (%s)\n",package->id,page,app->node_count,app->node_count?app->nodes[app->node_count-1].model.id:"");
   assert(valid);
   if(package->package.camera_allowed || package->package.photos_allowed){app->node_count=0;app->camera_allowed=app->photos_allowed=false;assert(!parse_node(app,root,-1,0));}
   free(app);JS_FreeValue(ctx,root);JS_FreeValue(ctx,entry);
  }
  JS_FreeValue(ctx,pages);JS_FreeValue(ctx,doc);JS_FreeContext(ctx);JS_FreeRuntime(rt);
  uint32_t version=package->package.source_version;assert(version);
  const void *icon=package->icon_image.data;
  assert(ouse_bpk_make_lazy(package,test_loader,test_release)==ESP_OK);
  assert(!package->page && !package->script && !package->asset_count && !package->package.image_count && !package->package.asset_count);
  assert(package->package.source_version==version && package->icon_image.data==icon && package->package.load==test_loader);
  for(unsigned asset=0;asset<OUSE_RUNTIME_ASSETS_MAX;++asset)assert(!package->asset_storage[asset].data);
  ouse_bpk_free(package);
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
library=ROOT/'.tools/ouse-runtime-check/quickjs-native.lib'
qjs_sources=[QJS/name for name in ['quickjs.c','dtoa.c','libregexp.c','libunicode.c']]
if not library.exists() or library.stat().st_mtime<max(path.stat().st_mtime for path in qjs_sources+list(QJS.glob('*.h'))):
 library.parent.mkdir(parents=True,exist_ok=True)
 objects=[]
 for source in qjs_sources:
  target=library.parent/(source.stem+'.obj')
  subprocess.run([str(compiler),'/nologo','/c','/O2','/MD','/std:c11','/utf-8','/D_CRT_SECURE_NO_WARNINGS','/D_GNU_SOURCE','/DQUICKJS_NG_BUILD','/D__STDC_NO_ATOMICS__=1','/I'+str(QJS),'/Fo'+str(target),str(source)],cwd=OUT,env=env,check=True,timeout=180)
  objects.append(target)
 subprocess.run([str(compiler.parent/'lib.exe'),'/nologo','/OUT:'+str(library),*map(str,objects)],cwd=OUT,env=env,check=True)
includes=[HOST,LVGL,QJS,ROOT/'components/ouse_core/include',ROOT/'components/ouse_runtime/include',ROOT/'components/ouse_installer',ROOT/'components/ouse_system/include',ROOT/'components/ouse_tasks/include']
command=[str(compiler),'/nologo','/std:c11','/O2','/MD','/utf-8','/D_CRT_SECURE_NO_WARNINGS','/D_GNU_SOURCE','/DQUICKJS_NG_BUILD','/DLV_KCONFIG_IGNORE','/DLV_LVGL_H_INCLUDE_SIMPLE','/DLV_CONF_PATH="'+(HOST/'lv_conf.h').as_posix()+'"']
command+=['/I'+str(p) for p in includes]
command+=list(map(str,[OUT/'packages.c',ROOT/'components/ouse_installer/ouse_bpk.c',ROOT/'components/ouse_installer/ouse_protocol.c',library]))+['/Fe'+str(OUT/'packages.exe')]
result=subprocess.run(command,cwd=OUT,env=env,capture_output=True,text=True,errors='replace')
assert result.returncode==0,(result.stdout,result.stderr)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--apps',nargs='+',default=['music','alarm','service-test','explorer'])
names=parser.parse_args().apps
for name in names:bpk.pack(ROOT/'apps'/name,OUT/f'{name}.bpk')
import io,json,zipfile
source=io.BytesIO()
with zipfile.ZipFile(source,'w',compression=zipfile.ZIP_STORED) as archive:
 for path in (ROOT/'apps/store').iterdir():
  if path.name in ['manifest.json','page.json','app.js','icon.bin']:archive.writestr(path.name,path.read_bytes())
rejects=[]
for ordinal,(key,value) in enumerate([('author','x'*65),('description','x'*513),('author',False),('description','bad\x00text'),('releasedAt','2026-02-29'),('releasedAt','2026-04-31'),('releasedAt','2026-13-01'),('releasedAt','0000-01-01')]):
 target=OUT/f'metadata-{ordinal}.reject.bpk'
 with zipfile.ZipFile(source) as archive,zipfile.ZipFile(target,'w',compression=zipfile.ZIP_STORED) as output:
  for entry in archive.infolist():
   data=archive.read(entry.filename)
   if entry.filename=='manifest.json':
    manifest=json.loads(data);manifest[key]=value;data=json.dumps(manifest,ensure_ascii=False).encode('utf-8')
   output.writestr(entry.filename,data)
 rejects.append(str(target))
subprocess.run([str(OUT/'packages.exe')]+[str(OUT/f'{name}.bpk') for name in names]+rejects,cwd=OUT,check=True,timeout=15)
