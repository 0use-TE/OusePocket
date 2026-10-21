"""Actual UI text policy; added game glyphs retain shipped pixels and metrics."""
from pathlib import Path
import os,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/default-fonts';OUT.mkdir(exist_ok=True)
CORE=ROOT/'components/ouse_core'
pool=(CORE/'ui_font_pool.h').read_text(encoding='utf-8')
sizes=list(map(int,re.search(r'sizes\[\]=\{([^}]+)',pool)[1].split(',')))
assert sizes==[8,10,12,14,16,18,20,22,24,28,32,40]
def glyph_data(text):
    bitmap=bytes(int(v,16) for v in re.findall(r'0x([0-9a-fA-F]{2})',text.split('static const uint8_t bitmap[] = {',1)[1].split('};',1)[0]))
    result={}
    for match in re.finditer(r'\{\.bitmap_index=(\d+), \.adv_w=(\d+), \.box_w=(\d+), \.box_h=(\d+), \.ofs_x=(-?\d+), \.ofs_y=(-?\d+)\}, // U\+([0-9A-F]+)',text):
        offset,advance,width,height,x,y,cp=match.groups();offset=int(offset);count=(int(width)*int(height)+1)//2
        result[int(cp,16)]=((advance,width,height,x,y),bitmap[offset:offset+count])
    return result
for n in sizes+['cjk_14','cjk_16','cjk_20']:
    path=f'components/ouse_core/ouse_font_ui_{n}.c'
    current=(ROOT/path).read_text(encoding='utf-8');previous=subprocess.check_output(['git','show','HEAD:'+path]).decode('utf-8')
    if isinstance(n,str):assert current.replace('\r\n','\n')==previous.replace('\r\n','\n'),path
    else:
        old,new=glyph_data(previous),glyph_data(current)
        assert old and all(cp in new and new[cp]==value for cp,value in old.items()),path
        assert re.search(r'\.line_height=\d+, \.base_line=\d+',current)[0]==re.search(r'\.line_height=\d+, \.base_line=\d+',previous)[0],path
        assert all(ord(c) in new for c in '荒野生存跳跳庄园散弹狙击榴电弧枪伙伴营地'),path
assert 'setFontPercent' not in (ROOT/'tools/vscode-ouse-bpk/media/preview.js').read_text(encoding='utf-8')
runtime=(ROOT/'components/ouse_runtime/ouse_runtime.c').read_text(encoding='utf-8')
assert '"setFontPercent"' not in runtime and '"fontSize",&font,default_font,8,84)' in runtime
assert 'font_marks' not in (CORE/'ouse_core.c').read_text(encoding='utf-8')
for n in [4,6,21,48,56,64,72,84]:assert not (CORE/f'ouse_font_ui_{n}.c').exists()
(OUT/'lvgl.h').write_text('''#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct {unsigned size;} lv_font_t;
typedef struct {const lv_font_t *font;char text[256];void *user;} lv_obj_t;
typedef struct {void *user;} lv_event_t;
#define LV_EVENT_DELETE 1
#define LV_FONT_DECLARE(name) extern const lv_font_t name;
static inline void *lv_event_get_user_data(lv_event_t *e){return e->user;}
void lv_obj_add_event_cb(lv_obj_t*,void (*)(lv_event_t*),int,void*);
void lv_label_set_text(lv_obj_t*,const char*);
void lv_obj_set_style_text_font(lv_obj_t*,const lv_font_t*,int);
''',encoding='utf-8')
(OUT/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 0x102\n',encoding='utf-8')
fixture='''#include "ouse_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
'''+''.join(f'const lv_font_t ouse_font_ui_{n}={{ {n} }};\n' for n in sizes)+'''
void lv_obj_add_event_cb(lv_obj_t *o,void (*cb)(lv_event_t*),int code,void *user){(void)cb;(void)code;o->user=user;}
void lv_label_set_text(lv_obj_t *o,const char *t){assert(strlen(t)<sizeof(o->text));strcpy(o->text,t);}
void lv_obj_set_style_text_font(lv_obj_t *o,const lv_font_t *f,int flags){(void)flags;o->font=f;}
int main(void){
 assert(ouse_ui_init(160,0,0)==ESP_OK);assert(ouse_ui_font_percent()==100);
 assert(ouse_ui_font(8)->size==8&&ouse_ui_font(13)->size==12&&ouse_ui_font(16)->size==16);
 assert(ouse_ui_font(72)->size==40&&ouse_ui_font(84)->size==40);
 assert(ouse_ui_font(28)->size==28&&ouse_ui_font(32)->size==32&&ouse_ui_font(40)->size==40);
 assert(ouse_ui_init(160,0,115)==ESP_ERR_INVALID_ARG);assert(ouse_ui_init(160,0,130)==ESP_ERR_INVALID_ARG);
 lv_obj_t label={0};ouse_ui_text_pair(&label,"中文","English");ouse_ui_use_font(&label,16);
 const lv_font_t *font=label.font;assert(ouse_ui_set_preferences(1,100)==ESP_OK);assert(!strcmp(label.text,"English")&&label.font==font);
 assert(ouse_ui_set_preferences(0,130)==ESP_ERR_INVALID_ARG);assert(ouse_ui_locale()==1&&ouse_ui_font_percent()==100);
 assert(ouse_ui_dp(10)==10&&ouse_ui_to_dp(10)==10);
 puts("PASS default fonts: fixed 100% policy, retained glyph pixels/metrics and CJK fallbacks; new game copy covered");
}
'''
(OUT/'font-check.c').write_text(fixture,encoding='utf-8')
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(ROOT/'.tools/ouse-kernel-check/environment.cmd')],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        k,v=line.split('=',1);env[k]=v
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
subprocess.run([str(compiler),'/nologo','/std:c11','/utf-8','/D_CRT_SECURE_NO_WARNINGS','/I'+str(OUT),'/I'+str(CORE/'include'),str(CORE/'ouse_ui.c'),str(OUT/'font-check.c'),'/Fe'+str(OUT/'font-check.exe')],cwd=OUT,env=env,check=True)
subprocess.run([str(OUT/'font-check.exe')],check=True)
