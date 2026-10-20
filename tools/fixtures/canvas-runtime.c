// Real production bindings + LVGL pixel tests and reproducible host benchmark.
#include <windows.h>
#include "quickjs.h"
#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_TEXT 256
#define MAX_NODES 128
typedef enum { NAV_IDLE, NAV_REQUESTED } navigation_stage_t;
#include "runtime-types.h"
typedef struct {
    node_t nodes[MAX_NODES];unsigned node_count;
    bool closed,faulted,dp_units,canvas_submitting;
    navigation_stage_t navigation_stage;
} instance_t;
static instance_t app;
static lv_image_dsc_t atlas;
static void model_lock(instance_t *a){(void)a;}
static void model_unlock(instance_t *a){(void)a;}
static instance_t *from_js(JSContext *ctx){return JS_GetContextOpaque(ctx);}
static void *ouse_psram_calloc(size_t n,size_t s){return calloc(n,s);}
static int units(instance_t *a,int v){return a->dp_units ? v*2 : v;}
static const lv_image_dsc_t *find_image(instance_t *a,const char *id){(void)a;return !strcmp(id,"actors") ? &atlas : NULL;}
static bool copy_string(JSContext *ctx,JSValueConst v,char *out,size_t cap){
    if(!JS_IsString(v))return false;size_t n;const char *s=JS_ToCStringLen(ctx,&n,v);
    bool ok=s&&n<cap&&!memchr(s,0,n);if(ok)memcpy(out,s,n+1);if(s)JS_FreeCString(ctx,s);return ok;
}
#include "ouse_runtime_canvas.inc"
#include "ouse_runtime_sprites.inc"
#include "ouse_canvas_draw.inc"
#include "sprite-pump.h"
enum { W=480,H=222,FRAMES=160 };
static uint16_t pixels[W*H],expected[W*H],buffer[W*32];
static uint32_t texels[32*32];
static unsigned long long flushed;
static lv_display_t *display;
static double us(void){LARGE_INTEGER n,f;QueryPerformanceCounter(&n);QueryPerformanceFrequency(&f);return (double)n.QuadPart*1000000.0/(double)f.QuadPart;}
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *data){
    int w=a->x2-a->x1+1;
    for(int y=a->y1;y<=a->y2;++y)memcpy(pixels+y*W+a->x1,data+(y-a->y1)*w*2,w*2);
    flushed+=(unsigned long long)w*(a->y2-a->y1+1);lv_display_flush_ready(d);
}
static JSValue eval(JSContext *ctx,const char *code){
    JSValue result=JS_Eval(ctx,code,strlen(code),"canvas-test",JS_EVAL_TYPE_GLOBAL);
    if(JS_IsException(result)){JSValue e=JS_GetException(ctx);const char *s=JS_ToCString(ctx,e);fprintf(stderr,"JS: %s\n",s);JS_FreeCString(ctx,s);JS_FreeValue(ctx,e);abort();}return result;
}
static void check(JSContext *ctx,const char *code){JSValue r=eval(ctx,code);assert(JS_ToBool(ctx,r));JS_FreeValue(ctx,r);}
static void clear_scene(void){
    lv_obj_clean(lv_screen_active());
    for(unsigned i=0;i<app.node_count;i++){free(app.nodes[i].canvas_pending);free(app.nodes[i].canvas_render);}
    memset(&app,0,sizeof(app));
}
static void setup_scene(bool canvas,unsigned count){
    clear_scene();app.node_count=canvas ? 1 : count;
    for(unsigned i=0;i<app.node_count;i++){
        node_t *n=&app.nodes[i];node_model_t *m=&n->model;
        sprintf(m->id,canvas?"world":"e%u",i);strcpy(m->image,"actors");m->type=canvas?CANVAS:SPRITE;
        m->w=canvas?W:16;m->h=canvas?H:16;m->sprite_w=m->sprite_h=16;m->sprite_count=4;m->sprite_fps=10;m->image_revision=1;
        n->object=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(n->object);
        lv_obj_remove_flag(n->object,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(n->object,m->w,m->h);lv_obj_set_pos(n->object,0,0);
        if(canvas){n->canvas_owner=&app;lv_obj_add_event_cb(n->object,canvas_draw_event,LV_EVENT_DRAW_MAIN,n);}
        else {n->sprite_image=lv_image_create(n->object);lv_obj_remove_flag(n->sprite_image,LV_OBJ_FLAG_CLICKABLE);lv_image_set_src(n->sprite_image,&atlas);lv_image_set_inner_align(n->sprite_image,LV_IMAGE_ALIGN_TOP_LEFT);lv_image_set_pivot(n->sprite_image,0,0);sprite_pump(&app,n,m);}
    }
    lv_obj_update_layout(lv_screen_active());lv_refr_now(display);
}
static void sync_scene(bool canvas){
    if(canvas)canvas_pump(&app,&app.nodes[0]);
    else for(unsigned i=0;i<app.node_count;i++){
        node_t *n=&app.nodes[i];sprite_pump(&app,n,&n->model);
        if(n->shown_style!=n->model.style_revision){lv_obj_set_pos(n->object,n->model.x,n->model.y);n->shown_style=n->model.style_revision;}
    }
}
static void qa(JSContext *ctx){
    setup_scene(true,1);
    check(ctx,"let helper=createCanvas('world');helper.beginFrame().fillRect({width:8,height:8,color:0xff0000});helper.endFrame();true");
    assert(app.nodes[0].canvas_pending->count==1);
    check(ctx,"Ouse.canvas.submit('world',[{op:'fillRect',x:0,y:0,width:20,height:20,color:0xff0000}]);true");
    canvas_frame_t *valid=app.nodes[0].canvas_pending;
    const char *bad[]={
        "[{op:'unknown'}]","[{op:'drawImage',src:'missing'}]","[{op:'drawImage',src:'@camera'}]",
        "[{op:'fillRect',x:NaN}]","[{op:'drawImage',src:'actors',sourceX:32}]",
        "[{op:'drawImage',src:'actors',sourceWidth:33}]","[{op:'drawText',text:'x'.repeat(129)}]",
        "[{op:'fillRect',width:-1}]","[{op:'fillRect',color:0x1000000}]","[null]",
        "Array(257).fill({op:'fillRect'})","[{op:'fillRect'},{op:'bad'}]",
        "[{get op(){throw Error('getter')}}]",
        "[{get op(){Ouse.canvas.submit('world',[]);return 'fillRect'}}]"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){
        char code[512];sprintf(code,"(()=>{try{Ouse.canvas.submit('world',%s);return false}catch(e){return true}})()",bad[i]);check(ctx,code);assert(app.nodes[0].canvas_pending==valid);
    }
    check(ctx,"(()=>{try{Ouse.canvas.submit('e0',[]);return false}catch(e){return true}})()");
    app.navigation_stage=NAV_REQUESTED;check(ctx,"(()=>{try{Ouse.canvas.submit('world',[]);return false}catch(e){return true}})()");app.navigation_stage=NAV_IDLE;
    app.closed=true;check(ctx,"(()=>{try{Ouse.canvas.submit('world',[]);return false}catch(e){return true}})()");app.closed=false;
    sync_scene(true);lv_refr_now(display);assert(pixels[5*W+5]==0xf800);
    check(ctx,"let commands=[{op:'fillRect',x:0,y:0,width:20,height:20,color:0x00ff00}];Ouse.canvas.submit('world',commands);commands[0].color=0xff0000;true");
    sync_scene(true);lv_refr_now(display);assert(pixels[5*W+5]==0x07e0);
    check(ctx,"Ouse.canvas.submit('world',[{op:'fillRect',x:0,y:0,width:20,height:20,color:0x00ff00}]);true");
    sync_scene(true);flushed=0;lv_refr_now(display);assert(flushed==0);
    check(ctx,"Ouse.canvas.submit('world',[]);true");sync_scene(true);lv_refr_now(display);assert(pixels[5*W+5]==0x1904);
    // All primitive paths, alpha overlap, offscreen clipping and atlas crop/scale.
    check(ctx,"Ouse.canvas.submit('world',[{op:'fillRect',x:-10,y:-10,width:30,height:30,color:0xff0000},{op:'strokeRect',x:30,y:4,width:24,height:20,radius:3,lineWidth:2,color:0xffffff},{op:'drawLine',x:60,y:4,x2:90,y2:20,lineWidth:2},{op:'drawArc',x:110,y:20,radius:14,startAngle:0,endAngle:360,lineWidth:3},{op:'fillTriangle',x:130,y:4,x2:150,y2:24,x3:130,y3:24,color:0xff0000},{op:'drawText',x:160,y:4,width:80,height:24,text:'Canvas'},{op:'drawImage',src:'actors',x:250,y:4,width:32,height:32,sourceX:16,sourceY:16,sourceWidth:16,sourceHeight:16}]);true");
    sync_scene(true);lv_refr_now(display);assert(pixels[5*W+5]==0xf800);assert(pixels[5*W+25]==0x1904);
    assert(pixels[12*W+270]!=0x1904);assert(pixels[12*W+290]==0x1904);
    // Canvas-local clip must not overwrite a sibling immediately outside it.
    lv_obj_set_size(app.nodes[0].object,10,10);lv_obj_set_pos(app.nodes[0].object,10,10);lv_refr_now(display);
    assert(pixels[15*W+15]==0xf800);assert(pixels[5*W+5]==0x1904);assert(pixels[15*W+25]==0x1904);
    setup_scene(true,1);app.dp_units=true;
    check(ctx,"Ouse.canvas.submit('world',[{op:'fillRect',x:2,y:2,width:4,height:4,color:0xff0000}]);true");sync_scene(true);lv_refr_now(display);assert(pixels[10*W+10]==0xf800);assert(pixels[12*W+12]==0x1904);app.dp_units=false;
    for(unsigned i=0;i<300;i++)check(ctx,"Ouse.canvas.submit('world',[{op:'fillRect'}]);true");
    setup_scene(true,1);lv_obj_set_size(app.nodes[0].object,100,100);lv_refr_now(display);
    check(ctx,"Ouse.canvas.submit('world',Array.from({length:40},(_,i)=>({op:'fillRect',x:i*2,y:i*2})));true");sync_scene(true);lv_refr_now(display);
    check(ctx,"Ouse.canvas.submit('world',Array.from({length:40},(_,i)=>({op:'fillRect',x:i*2+1,y:i*2+1})));true");sync_scene(true);flushed=0;lv_refr_now(display);assert(flushed==10000);
    clear_scene();puts("PASS atomic rejection, copied data, replacement/cleanup, primitives, crop/scale, local clip and dp coordinates");
}
static void invoke(JSContext *ctx,JSValue function,int argc,JSValue *args){JSValue r=JS_Call(ctx,function,JS_UNDEFINED,argc,args);if(JS_IsException(r)){JSValue e=JS_GetException(ctx);fprintf(stderr,"Call: %s\n",JS_ToCString(ctx,e));abort();}JS_FreeValue(ctx,r);}
static void qa_large_background(JSContext *ctx){
    lv_image_dsc_t previous=atlas;lv_image_cache_drop(&atlas);
    uint32_t *large=malloc(1620*385*4);assert(large);
    for(unsigned y=0;y<385;y++)for(unsigned x=0;x<1620;x++)large[y*1620+x]=0xff000000|(y>=200?0x00ff00:x<800?0xff0000:0x0000ff);
    atlas=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w=1620,.h=385,.stride=1620*4},.data_size=1620*385*4,.data=(uint8_t *)large};
    setup_scene(true,1);
    check(ctx,"Ouse.canvas.submit('world',[{op:'drawImage',src:'actors',x:-600,y:-100,width:1620,height:385,sourceWidth:1620,sourceHeight:385}]);true");
    sync_scene(true);lv_refr_now(display);
    assert(pixels[10*W+10]==0xf800);assert(pixels[10*W+210]==0x001f);assert(pixels[110*W+10]==0x07e0);
    clear_scene();lv_image_cache_drop(&atlas);atlas=previous;free(large);
    puts("PASS 1620x385 native-size background with camera offsets and clipped RGB565 output");
}
static void save_ppm(const char *path){FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",W,H);for(unsigned i=0;i<W*H;i++){unsigned c=pixels[i];unsigned char rgb[]={(c>>11)*255/31,((c>>5)&63)*255/63,(c&31)*255/31};fwrite(rgb,1,3,f);}fclose(f);}
static unsigned long long pixel_hash(void){unsigned long long h=14695981039346656037ULL;for(unsigned i=0;i<W*H;i++){h^=pixels[i];h*=1099511628211ULL;}return h;}
int main(int argc,char **argv){
    assert(argc==4);lv_init();display=lv_display_create(W,H);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    lv_obj_t *root=lv_screen_active();lv_obj_remove_style_all(root);lv_obj_set_style_bg_color(root,lv_color_hex(0x182020),0);lv_obj_set_style_bg_opa(root,255,0);lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    unsigned colors[]={0xf0643c,0x50d28c,0x508cf0,0xe6be46};
    for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++){unsigned a=x%16==0||x%16==15||y%16==0||y%16==15?0:x%16<4?128:255;texels[y*32+x]=(a<<24)|colors[y/16*2+x/16];}
    atlas=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w=32,.h=32,.stride=32*4},.data_size=sizeof(texels),.data=(uint8_t *)texels};
    JSRuntime *rt=JS_NewRuntime();JSContext *ctx=JS_NewContext(rt);JS_SetContextOpaque(ctx,&app);
    JSValue global=JS_GetGlobalObject(ctx),api=JS_NewObject(ctx),canvas=JS_NewObject(ctx),sprites=JS_NewObject(ctx);
    JS_SetPropertyStr(ctx,canvas,"submit",JS_NewCFunction(ctx,canvas_submit,"submit",2));JS_SetPropertyStr(ctx,sprites,"update",JS_NewCFunction(ctx,sprites_update,"update",1));JS_SetPropertyStr(ctx,api,"canvas",canvas);JS_SetPropertyStr(ctx,api,"sprites",sprites);JS_SetPropertyStr(ctx,global,"Ouse",api);
    FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);char *code=calloc(1,size+1);assert(fread(code,1,size,f)==(size_t)size);fclose(f);JS_FreeValue(ctx,eval(ctx,code));free(code);
    qa(ctx);
    qa_large_background(ctx);
    // Pixel parity for every deterministic frame, including overlapping alpha sprites.
    for(unsigned count=8;count<=96;count=count==8?32:count+32){
        for(unsigned frame=0;frame<24;frame++){
            char js[256];setup_scene(false,count);sprintf(js,"benchmarkSubmit('json',benchmarkScene(%u,%u,%u,%u))",count,frame,W,H);JS_FreeValue(ctx,eval(ctx,js));sync_scene(false);lv_refr_now(display);memcpy(expected,pixels,sizeof(pixels));
            setup_scene(true,count);sprintf(js,"benchmarkSubmit('canvas',benchmarkScene(%u,%u,%u,%u))",count,frame,W,H);JS_FreeValue(ctx,eval(ctx,js));sync_scene(true);lv_refr_now(display);
            if(memcmp(expected,pixels,sizeof(pixels))){unsigned bad=0;while(expected[bad]==pixels[bad])bad++;fprintf(stderr,"Pixel mismatch n=%u f=%u xy=%u,%u json=%x canvas=%x\n",count,frame,bad%W,bad/W,expected[bad],pixels[bad]);abort();}
        }
    }
    save_ppm(argv[3]);puts("PASS every-pixel JSON/Canvas parity: 8/32/64/96 entities, 24 deterministic frames each");
    check(ctx,"let actors=[];function prepare(n,f){actors=benchmarkScene(n,f,480,222)};function submit(mode){benchmarkSubmit(mode,actors)};true");
    JSValue prepare=JS_GetPropertyStr(ctx,global,"prepare"),submit=JS_GetPropertyStr(ctx,global,"submit");
    f=fopen(argv[2],"w");assert(f);fputs("invalidation,entities,mode,trial,frame,prepare_us,submit_us,sync_us,render_us,total_us,flush_pixels\n",f);
    unsigned long long hashes[FRAMES+40];
    for(unsigned policy=0;policy<2;policy++)for(unsigned count=8;count<=96;count=count==8?32:count+32)for(unsigned trial=0;trial<3;trial++)for(unsigned order=0;order<2;order++){
        bool is_canvas=((order+trial)%2)==0;const char *mode=is_canvas?"canvas":"json";setup_scene(is_canvas,count);JS_RunGC(rt);
        JSValue mode_value=JS_NewString(ctx,mode);
        for(unsigned frame=0;frame<FRAMES+40;frame++){
            JSValue args[]={JS_NewInt32(ctx,count),JS_NewInt32(ctx,frame)};
            double t0=us();invoke(ctx,prepare,2,args);double t1=us();invoke(ctx,submit,1,&mode_value);double t2=us();sync_scene(is_canvas);double t3=us();
            if(policy)lv_obj_invalidate(root);flushed=0;lv_refr_now(display);double t4=us();
            if(frame>=40)fprintf(f,"%s,%u,%s,%u,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%llu\n",policy?"full":"natural",count,mode,trial,frame-40,t1-t0,t2-t1,t3-t2,t4-t3,t4-t0,flushed);
            if(!order)hashes[frame]=pixel_hash();else assert(hashes[frame]==pixel_hash());
            JS_FreeValue(ctx,args[0]);JS_FreeValue(ctx,args[1]);lv_tick_inc(50);
        }
        JS_FreeValue(ctx,mode_value);
    }
    fclose(f);clear_scene();JS_FreeValue(ctx,prepare);JS_FreeValue(ctx,submit);JS_FreeValue(ctx,global);JS_FreeContext(ctx);JS_FreeRuntime(rt);lv_display_delete(display);lv_deinit();puts("PASS host benchmark completed (not device FPS)");return 0;
}
