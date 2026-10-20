"""Two actual LVGL indevs, production pointer queues/callbacks, real QuickJS delivery."""
from pathlib import Path
import subprocess,os
R=Path(__file__).resolve().parents[1];O=R/'.tools/multitouch-check';O.mkdir(exist_ok=True)
s=(R/'components/ouse_runtime/ouse_runtime.c').read_text(encoding='utf8')
def part(a,b):return s[s.index(a):s.index(b,s.index(a))]
f=r'''
#include "quickjs.h"
#include "lvgl.h"
#include "ouse_pointer.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
enum {NAV_IDLE,EVENT_POINTER_DOWN=0};
typedef struct {struct {char id[32];} model;lv_obj_t *object;unsigned pointer_mask;JSValue handlers[4];} node_t;
typedef struct {JSContext *context;node_t nodes[2];unsigned node_count,generation;bool visible,closed,faulted,dp_units;int navigation_stage;ouse_pointer_t pointer[2];lv_indev_t *pointer_device[2];int pointer_origin_x[2],pointer_origin_y[2];bool pointer_delivered[2];unsigned pointer_node[2];uint32_t pointer_generation[2];} instance_t;
static instance_t app;
static void model_lock(instance_t *a){}static void model_unlock(instance_t *a){}static void begin_call(instance_t *a){}static void wake_worker(void){}
static uint32_t now_ms(void){return lv_tick_get();}static int ouse_ui_to_dp(int v){return v;}
static int find_object_node(instance_t *a,lv_obj_t *o){for(int i=0;i<2;i++)if(a->nodes[i].object==o)return i;return -1;}
static bool node_visible_locked(instance_t *a,int i,bool x){return true;}
static JSValue call_js(instance_t *a,const char *x,const char *id,JSValue fn,JSValue self,int n,JSValue *args){return JS_Call(a->context,fn,self,n,(JSValueConst*)args);}
static bool result_ok(instance_t *a,JSValue v){assert(!JS_IsException(v));JS_FreeValue(a->context,v);return true;}
'''
f+=part('static void pointer_deliver(','static void reset_page_nodes(')
f+=part('static bool pointer_input(','static void input_event(')
f+=r'''
static lv_point_t points[2];static bool pressed[2];static lv_indev_t *devices[2];
static void read(lv_indev_t *i,lv_indev_data_t *d){int n=i==devices[1];d->point=points[n];d->state=pressed[n]?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;}
static void event(lv_event_t *e){pointer_input(&app,lv_event_get_target_obj(e),lv_event_get_code(e));}
static void drain(void){for(unsigned j=0;j<8;j++)for(unsigned i=0;i<2;i++){ouse_pointer_event_t e;if(ouse_pointer_pop(&app.pointer[i],&e)){e.pointer_id=i;pointer_deliver(&app,&e);}}}
static void input(int i,int x,int y,bool down){points[i]=(lv_point_t){x,y};pressed[i]=down;lv_tick_inc(25);lv_indev_read(devices[i]);drain();}
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){lv_display_flush_ready(d);}
static void js(const char *s){JSValue v=JS_Eval(app.context,s,strlen(s),"test",JS_EVAL_TYPE_GLOBAL);if(JS_IsException(v)){JSValue e=JS_GetException(app.context);fprintf(stderr,"%s\n",JS_ToCString(app.context,e));assert(0);}JS_FreeValue(app.context,v);}
int main(void){
 lv_init();lv_display_t *display=lv_display_create(320,240);static uint16_t pixels[320*20];lv_display_set_buffers(display,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
 JSRuntime *rt=JS_NewRuntime();app.context=JS_NewContext(rt);app.visible=true;app.node_count=2;app.generation=7;
 js("var contacts={},peak=0,log=[];function take(kind,e){if(kind===0)contacts[e.pointerId]=e.x;else if(kind>=2)delete contacts[e.pointerId];else if(!(e.pointerId in contacts))throw Error('move without down');peak=Math.max(peak,Object.keys(contacts).length);log.push([kind,e.pointerId,e.x,e.y]);}");
 lv_obj_t *root=lv_screen_active();lv_obj_set_style_pad_all(root,0,0);
 for(int i=0;i<2;i++){node_t *n=&app.nodes[i];sprintf(n->model.id,"pad%d",i);n->pointer_mask=15;n->object=lv_button_create(root);lv_obj_set_pos(n->object,10+i*160,40);lv_obj_set_size(n->object,140,140);lv_obj_add_event_cb(n->object,event,LV_EVENT_ALL,NULL);for(int k=0;k<4;k++){char code[100];sprintf(code,"(function(e){take(%d,e);})",k);n->handlers[k]=JS_Eval(app.context,code,strlen(code),"handler",JS_EVAL_TYPE_GLOBAL);}devices[i]=lv_indev_create();lv_indev_set_type(devices[i],LV_INDEV_TYPE_POINTER);lv_indev_set_display(devices[i],display);lv_indev_set_read_cb(devices[i],read);lv_indev_set_mode(devices[i],LV_INDEV_MODE_EVENT);}
 lv_obj_update_layout(root);
 input(0,40,80,true);input(1,210,80,true);js("if(peak!==2||Object.keys(contacts).length!==2)throw Error('not simultaneous');if(log[0][1]===log[1][1])throw Error('identity collision');");
 input(0,60,100,true);input(1,210,80,false);js("if(Object.keys(contacts).length!==1)throw Error('release cleared other finger');");
 input(0,310,220,true);input(0,310,220,false);js("if(Object.keys(contacts).length)throw Error('release outside stuck');");
 input(1,210,80,true);input(0,40,80,true);pointer_cancel_worker(&app);js("if(Object.keys(contacts).length)throw Error('cancel all stuck');");
 input(0,40,80,false);input(1,210,80,false);input(0,40,80,true);input(1,210,80,true);pointer_cancel_worker(&app);js("if(Object.keys(contacts).length)throw Error('reuse cancellation failed');");
 for(int i=0;i<2;i++)for(int k=0;k<4;k++)JS_FreeValue(app.context,app.nodes[i].handlers[k]);JS_FreeContext(app.context);JS_FreeRuntime(rt);puts("PASS real LVGL/QuickJS two contacts: independent IDs/origins, simultaneous hold, move, separate release, outside release, cancel-all and reuse");
}
'''
(O/'check.c').write_text(f,encoding='utf8');env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(R/'.tools/ouse-core-check/environment.cmd')],encoding='mbcs').splitlines():
 if '=' in line and not line.startswith('='):
  k,v=line.split('=',1);env[k]=v
cl=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe';q=R/'managed_components/espressif__quickjs-ng/quickjs-ng';h=R/'.tools/ouse-core-check';lib=list((R/'.tools').glob('*/quickjs-native.lib'))[0]
subprocess.run([str(cl),'/nologo','/utf-8','/std:c11','/MD','/D_CRT_SECURE_NO_WARNINGS','/DLV_KCONFIG_IGNORE','/DLV_LVGL_H_INCLUDE_SIMPLE','/DLV_CONF_PATH="'+(h/'lv_conf.h').as_posix()+'"','/I'+str(q),'/I'+str(R/'components/ouse_runtime'),'/I'+str(R/'managed_components/lvgl__lvgl'),'/I'+str(h),str(O/'check.c'),str(lib),str(h/'lvgl-native.lib'),'Ws2_32.lib','/Fe:'+str(O/'check.exe')],cwd=O,env=env,check=True)
subprocess.run([str(O/'check.exe')],cwd=O,check=True,timeout=30)
