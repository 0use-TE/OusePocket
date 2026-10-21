"""Actual QuickJS batch APIs and LVGL sprite clipping/animation, without a device."""
from pathlib import Path
import subprocess,os
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'.tools/platform-runtime-check';OUT.mkdir(parents=True,exist_ok=True)
source=(ROOT/'components/ouse_runtime/ouse_runtime.c').read_text(encoding='utf-8')
def part(a,b):return source[source.index(a):source.index(b,source.index(a))]
fixture=r'''
#include "quickjs.h"
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <limits.h>
#define MAX_TIMERS 8
typedef struct {JSValue handler;uint32_t id,period,due,remaining;bool interval,frame;uint32_t frame_epoch;} script_timer_t;
#include <stdbool.h>
typedef enum {NAV_IDLE,NAV_REQUESTED} navigation_stage_t;
enum {SPRITE,CONTAINER,IMAGE};
typedef struct {char id[32],image[32];int x,y,w,h,type,sprite_w,sprite_h,sprite_frame,sprite_first,sprite_count,sprite_fps;bool fill_w,fill_h,content_h,sprite_playing,sprite_loop;uint32_t style_revision,image_revision,sprite_restart;} node_model_t;
typedef struct {node_model_t model;lv_obj_t *object,*sprite_image;unsigned sprite_frame;uint32_t sprite_last,sprite_revision,sprite_restart,shown_image;} node_t;
typedef struct {bool closed,faulted,visible,built,validation_only,fullscreen_requested,game_requested,frame_waiting;uint32_t frame_epoch,display_revision;int navigation_stage,command_values[4];unsigned pending_commands,node_count,next_timer;script_timer_t timers[MAX_TIMERS];node_t nodes[2];} instance_t;
typedef struct {int app_width,app_height;uint32_t revisions[3];} ui_snapshot_t;
static uint32_t now_ms(void){return 1000;}
static instance_t app;
static lv_image_dsc_t atlas;
static instance_t *from_js(JSContext *ctx){return JS_GetContextOpaque(ctx);}
static void model_lock(instance_t *a){}static void model_unlock(instance_t *a){}
static const lv_image_dsc_t *find_image(instance_t *a,const char *id){return &atlas;}
static bool copy_string(JSContext *ctx,JSValueConst v,char *out,size_t cap){if(!JS_IsString(v))return false;size_t n;const char *s=JS_ToCStringLen(ctx,&n,v);bool ok=s&&n<cap&&!memchr(s,0,n);if(ok)memcpy(out,s,n+1);if(s)JS_FreeCString(ctx,s);return ok;}
'''
fixture+=part('static bool frame_ready(','static JSValue timer_clear(')
fixture+=part('static JSValue orientation_set(','static JSValue display_snapshot(')
fixture+=part('static JSValue fullscreen_set(','static JSValue device_snapshot(')
fixture+=part('static void publish_viewport_size(','static void ui_pump(')
fixture+='#include "'+(ROOT/'components/ouse_runtime/ouse_runtime_sprites.inc').as_posix()+'"\n'
fixture+='#include "'+(ROOT/'components/ouse_runtime/ouse_runtime_layout.inc').as_posix()+'"\n'
fixture+=part('static void image_pump(','static void build_nodes(')
fixture+=r'''
static uint16_t pixels[64*64],buffer[64*64];static uint32_t texels[64*32];
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *data){for(int y=a->y1;y<=a->y2;++y)memcpy(pixels+y*64+a->x1,data+(y-a->y1)*(a->x2-a->x1+1)*2,(a->x2-a->x1+1)*2);lv_display_flush_ready(d);}
static bool evaluate(JSContext *ctx,const char *code){JSValue v=JS_Eval(ctx,code,strlen(code),"test",JS_EVAL_TYPE_GLOBAL);if(JS_IsException(v)){const char *s=JS_ToCString(ctx,JS_GetException(ctx));fprintf(stderr,"JS failed: %s\n",s);return false;}bool result=JS_ToBool(ctx,v);JS_FreeValue(ctx,v);return result;}
int main(void){
 JSRuntime *rt=JS_NewRuntime();JSContext *ctx=JS_NewContext(rt);JS_SetContextOpaque(ctx,&app);JSValue global=JS_GetGlobalObject(ctx);
 JS_SetPropertyStr(ctx,global,"interval",JS_NewCFunctionMagic(ctx,timer_create,"interval",2,JS_CFUNC_generic_magic,1));
 JS_SetPropertyStr(ctx,global,"sprites",JS_NewCFunction(ctx,sprites_update,"sprites",1));JS_SetPropertyStr(ctx,global,"layout",JS_NewCFunction(ctx,ui_layout,"layout",1));JS_SetPropertyStr(ctx,global,"fullscreen",JS_NewCFunction(ctx,fullscreen_set,"fullscreen",1));JS_SetPropertyStr(ctx,global,"orientation",JS_NewCFunction(ctx,orientation_set,"orientation",1));JS_FreeValue(ctx,global);
 app.visible=true;app.node_count=2;strcpy(app.nodes[0].model.id,"hero");strcpy(app.nodes[1].model.id,"panel");app.nodes[0].model=(node_model_t){.type=SPRITE,.sprite_w=32,.sprite_h=32,.sprite_count=2,.sprite_fps=10,.sprite_loop=true,.image_revision=1};strcpy(app.nodes[0].model.id,"hero");app.nodes[1].model.type=CONTAINER;
 atlas=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w=64,.h=32,.stride=64*4},.data=(const uint8_t *)texels,.data_size=sizeof(texels)};
 assert(evaluate(ctx,"sprites([{id:'hero',x:12,y:20,frame:1}]); true"));assert(app.nodes[0].model.x==12&&app.nodes[0].model.sprite_frame==1);
 assert(evaluate(ctx,"(()=>{try{sprites([{id:'hero',x:99},{id:'bad',frame:0}]);return false}catch(e){return true}})()"));assert(app.nodes[0].model.x==12);
 assert(evaluate(ctx,"(()=>{try{sprites([{id:'hero',frame:2}]);return false}catch(e){return true}})()"));assert(app.nodes[0].model.sprite_frame==1);
 assert(evaluate(ctx,"(()=>{try{sprites([{id:'hero',fps:31}]);return false}catch(e){return true}})()"));
 unsigned unchanged=app.nodes[0].model.style_revision;assert(evaluate(ctx,"sprites([{id:'hero',x:12,y:20}]);true"));assert(unchanged==app.nodes[0].model.style_revision);
 assert(evaluate(ctx,"layout([{id:'panel',x:0,y:0,width:480,height:256}])"));assert(app.nodes[1].model.w==480);
 assert(evaluate(ctx,"(()=>{try{layout([{id:'panel',x:5,y:0,width:1,height:1},{id:'missing',x:0,y:0,width:1,height:1}]);return false}catch(e){return true}})()"));assert(app.nodes[1].model.x==0);
 assert(evaluate(ctx,"(()=>{try{layout([{id:'panel',x:0,y:0,width:NaN,height:1}]);return false}catch(e){return true}})()"));
 assert(evaluate(ctx,"interval(()=>{},50)>0"));assert(app.timers[0].period==50);
 JSValue global2=JS_GetGlobalObject(ctx);JS_SetPropertyStr(ctx,global2,"requestFrame",JS_NewCFunctionMagic(ctx,timer_create,"requestFrame",1,JS_CFUNC_generic_magic,2));JS_SetPropertyStr(ctx,global2,"gameMode",JS_NewCFunction(ctx,game_mode_set,"gameMode",1));JS_FreeValue(ctx,global2);
 app.frame_epoch=42;assert(evaluate(ctx,"requestFrame(()=>{})>0"));assert(app.timers[1].frame&&!app.timers[1].interval&&app.timers[1].period==0&&app.timers[1].frame_epoch==42&&app.frame_waiting);
 app.built=true;assert(!frame_ready(&app,&app.timers[1]));app.frame_epoch++;assert(frame_ready(&app,&app.timers[1]));app.visible=false;assert(!frame_ready(&app,&app.timers[1]));app.visible=true;app.built=false;assert(!frame_ready(&app,&app.timers[1]));app.built=true;
 app.visible=false;assert(evaluate(ctx,"!requestFrame(()=>{}) && !gameMode(true)"));app.visible=true;
 app.validation_only=true;assert(evaluate(ctx,"!requestFrame(()=>{}) && !gameMode(true)"));app.validation_only=false;
 assert(evaluate(ctx,"gameMode(true)"));assert(app.pending_commands&256u);assert(app.game_requested);
 assert(evaluate(ctx,"gameMode(false)"));assert(!app.game_requested);
 assert(evaluate(ctx,"(()=>{try{gameMode(1);return false}catch(e){return true}})()"));
 assert(evaluate(ctx,"(()=>{try{requestFrame(1);return false}catch(e){return true}})()"));
 app.pending_commands=0;
 ui_snapshot_t snapshot={.app_width=480,.app_height=256,.revisions={0,0,4}};app.display_revision=4;
 publish_viewport_size(&app,&snapshot,480,320);assert(snapshot.app_height==320&&snapshot.revisions[2]==5&&app.display_revision==5);
 publish_viewport_size(&app,&snapshot,480,320);assert(app.display_revision==5);
 assert(evaluate(ctx,"(()=>{try{interval(()=>{},32);return false}catch(e){return true}})()"));
 assert(evaluate(ctx,"(()=>{try{interval(()=>{},NaN);return false}catch(e){return true}})()"));
 assert(evaluate(ctx,"orientation('landscape')"));assert(app.pending_commands==64&&app.command_values[2]==1);app.visible=false;assert(evaluate(ctx,"!orientation('portrait')"));app.visible=true;
 assert(evaluate(ctx,"fullscreen(true)"));assert(app.pending_commands&128u);assert(app.fullscreen_requested);
 assert(evaluate(ctx,"fullscreen(false)"));assert(!app.fullscreen_requested);
 app.visible=false;assert(evaluate(ctx,"!fullscreen(true)"));assert(!app.fullscreen_requested);app.visible=true;
 app.validation_only=true;assert(evaluate(ctx,"!fullscreen(true)"));app.validation_only=false;
 assert(evaluate(ctx,"(()=>{try{fullscreen(1);return false}catch(e){return true}})()"));
 lv_init();lv_display_t *display=lv_display_create(64,64);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
 lv_obj_t *root=lv_screen_active();lv_obj_set_style_bg_color(root,lv_color_black(),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);lv_obj_set_style_pad_all(root,0,0);
 for(unsigned i=0;i<64*32;++i)texels[i]=(i%64)<32?0xffff0000:0xff00ff00;
 node_t *node=&app.nodes[0];node->object=lv_obj_create(root);lv_obj_remove_style_all(node->object);lv_obj_set_size(node->object,32,32);lv_obj_set_pos(node->object,8,8);lv_obj_remove_flag(node->object,LV_OBJ_FLAG_SCROLLABLE);
 node->sprite_image=lv_image_create(node->object);lv_image_set_inner_align(node->sprite_image,LV_IMAGE_ALIGN_TOP_LEFT);lv_image_set_pivot(node->sprite_image,0,0);lv_image_set_src(node->sprite_image,&atlas);lv_obj_update_layout(root);
 image_pump(&app,node,&node->model);assert(lv_obj_get_parent(node->object)==root);sprite_pump(&app,node,&node->model);lv_refr_now(display);assert(pixels[16*64+16]==0x07e0);assert(pixels[16*64+48]==0);
 assert(evaluate(ctx,"sprites([{id:'hero',frame:0,playing:true}]);true"));sprite_pump(&app,node,&node->model);lv_refr_now(display);assert(pixels[16*64+16]==0xf800);
 lv_tick_inc(100);sprite_pump(&app,node,&node->model);lv_refr_now(display);assert(pixels[16*64+16]==0x07e0);lv_tick_inc(100);sprite_pump(&app,node,&node->model);lv_refr_now(display);assert(pixels[16*64+16]==0xf800);
 for(unsigned i=0;i<MAX_TIMERS;++i)if(app.timers[i].id)JS_FreeValue(ctx,app.timers[i].handler);
 lv_tick_inc(100);sprite_pump(&app,node,&node->model);assert(node->sprite_frame==1);
 assert(evaluate(ctx,"sprites([{id:'hero',playing:false}]);true"));sprite_pump(&app,node,&node->model);unsigned frozen=node->sprite_frame;lv_tick_inc(500);sprite_pump(&app,node,&node->model);assert(node->sprite_frame==frozen);
 JS_FreeContext(ctx);JS_FreeRuntime(rt);lv_display_delete(display);puts("PASS real QuickJS + LVGL: atomic sprite/layout batches, range/unknown-node rejection, foreground orientation gate, cropped atlas pixels, native animation and loop");
}
'''
(OUT/'check.c').write_text(fixture,encoding='utf-8');env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(ROOT/'.tools/ouse-core-check/environment.cmd')],encoding='mbcs').splitlines():
 if '=' in line and not line.startswith('='):
  k,v=line.split('=',1);env[k]=v
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe';qjs=ROOT/'managed_components/espressif__quickjs-ng/quickjs-ng';host=ROOT/'.tools/ouse-core-check'
libs=list((ROOT/'.tools').glob('*/quickjs-native.lib'));assert libs,'Existing QuickJS host library required'
subprocess.run([str(compiler),'/nologo','/utf-8','/std:c11','/MD','/D_CRT_SECURE_NO_WARNINGS','/DLV_KCONFIG_IGNORE','/DLV_LVGL_H_INCLUDE_SIMPLE','/DLV_CONF_PATH="'+(host/'lv_conf.h').as_posix()+'"','/I'+str(qjs),'/I'+str(ROOT/'managed_components/lvgl__lvgl'),'/I'+str(host),str(OUT/'check.c'),str(libs[0]),str(host/'lvgl-native.lib'),'Ws2_32.lib','/Fe:'+str(OUT/'check.exe')],cwd=OUT,env=env,check=True)
subprocess.run([str(OUT/'check.exe')],cwd=OUT,check=True)
