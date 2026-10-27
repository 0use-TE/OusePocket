"""Render production camera image setup with real LVGL: fill, fit and clipping."""
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.tools/camera-preview-check'
HOST = ROOT / '.tools/ouse-core-check'
OUT.mkdir(parents=True, exist_ok=True)
env = dict(os.environ)
for line in subprocess.check_output(['cmd.exe', '/d', '/c', str(HOST/'environment.cmd')], encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key, value = line.split('=', 1)
        env[key] = value
runtime = (ROOT/'components/ouse_runtime/ouse_runtime.c').read_text(encoding='utf-8')
start = runtime.index('else if(model.type==IMAGE){') + len('else if(model.type==IMAGE){')
body = runtime[start:runtime.index('else if(model.type==TEXT_INPUT)', start)].rstrip()
assert body.endswith('}')
body = body[:-1]
fixture = r'''
#include "lvgl.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint16_t pixels[320*240],screen[500*500],buffer[500*20];
static lv_image_dsc_t source;
static const void *find_image(void *app,const char *name){(void)app;(void)name;return &source;}
static void configure(lv_obj_t *object,const char *name){void *app=NULL;struct{const char *image;}model={name};
__PRODUCTION__
}
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *map){
 for(int y=area->y1;y<=area->y2;y++){
  memcpy(screen+y*500+area->x1,map,(area->x2-area->x1+1)*2);map+=(area->x2-area->x1+1)*2;
 }lv_display_flush_ready(display);
}
static void render(void){lv_obj_update_layout(lv_screen_active());lv_obj_invalidate(lv_screen_active());lv_refr_now(NULL);}
static void check(int w,int h,int fill){
 lv_obj_t *root=lv_screen_active();lv_obj_clean(root);lv_obj_set_style_bg_color(root,lv_color_hex(0x700070),0);
 lv_obj_t *view=lv_obj_create(root);lv_obj_remove_style_all(view);lv_obj_remove_flag(view,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_set_pos(view,10,10);lv_obj_set_size(view,w,h);lv_obj_set_style_bg_color(view,lv_color_black(),0);lv_obj_set_style_bg_opa(view,255,0);
 lv_obj_t *image=lv_image_create(view);configure(image,"@camera");
 assert(!lv_image_get_antialias(image));assert(lv_image_get_inner_align(image)==LV_IMAGE_ALIGN_CONTAIN);
 int unit=fill?((w+3)/4>(h+2)/3?(w+3)/4:(h+2)/3)+1:(w/4<h/3?w/4:h/3);
 int iw=unit*4,ih=unit*3;lv_obj_set_size(image,iw,ih);
 lv_obj_set_pos(image,LV_FLOOR((w-iw)/2.0),LV_FLOOR((h-ih)/2.0));render();
 assert(screen[(10+h/2)*500+10+w/2]!=0);
 if(fill){assert(screen[10*500+10]!=0);assert(screen[(9+h)*500+9+w]!=0);}
 else {assert(screen[10*500+10]==0);}
 // Enlarged image must not leak through the parent into adjacent UI.
 assert(screen[10*500+9]==screen[0]);assert(screen[9*500+10]==screen[0]);
 printf("PASS real LVGL %dx%d %s: centered 4:3, pixels, clipping\n",w,h,fill?"fill":"whole");
}
int main(void){
 lv_init();for(unsigned i=0;i<320*240;i++)pixels[i]=(i%320<160)?0x07e0:0xf800;
 source=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=320,.h=240,.stride=640},.data=(uint8_t *)pixels,.data_size=sizeof(pixels)};
 lv_display_t *display=lv_display_create(500,500);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
 lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
 for(int fill=0;fill<2;fill++){check(320,440,fill);check(480,280,fill);check(320,480,fill);check(480,320,fill);}
 lv_obj_t *photo=lv_image_create(lv_screen_active());configure(photo,"@photo");assert(lv_image_get_antialias(photo));
 puts("PASS photo filtering retained; live camera uses nearest sampling");lv_deinit();return 0;
}
'''.replace('__PRODUCTION__', body).replace('LV_FLOOR((w-iw)/2.0)', '(w-iw>=0?(w-iw)/2:(w-iw-1)/2)').replace('LV_FLOOR((h-ih)/2.0)', '(h-ih>=0?(h-ih)/2:(h-ih-1)/2)')
(OUT/'check.c').write_text(fixture, encoding='utf-8')
compiler = Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
command = [str(compiler), '/nologo', '/O2', '/MD', '/utf-8', '/std:c11', '/DLV_KCONFIG_IGNORE',
           '/DLV_CONF_PATH="'+(HOST/'lv_conf.h').as_posix()+'"', '/I'+str(ROOT/'managed_components/lvgl__lvgl'),
           str(OUT/'check.c'), str(HOST/'lvgl-native.lib'), '/Fe'+str(OUT/'check.exe')]
subprocess.run(command, cwd=OUT, env=env, check=True)
subprocess.run([str(OUT/'check.exe')], cwd=OUT, check=True)
