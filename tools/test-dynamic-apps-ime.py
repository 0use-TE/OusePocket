"""Production registry and IME against real host LVGL, with no device access."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.tools/dynamic-apps-ime-check'
OUT.mkdir(parents=True, exist_ok=True)
CACHE = ROOT / '.tools/ouse-core-check'
LVGL = ROOT / 'managed_components/lvgl__lvgl'
source = r'''
#include "ouse_apps_internal.h"
#include "ouse_ime.h"
#include "ouse_theme.h"
#include "ouse_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned opened,closed;
bool ouse_tasks_quiescing(void){return false;}
void ouse_theme_apply(lv_obj_t *object,ouse_style_role_t role,lv_style_selector_t selector){(void)object;(void)role;(void)selector;}
uint32_t ouse_ui_generation(void){return 1;}
unsigned ouse_ui_locale(void){return 0;}
const char *ouse_ui_tr(const char *value){return value;}
int ouse_ui_dp(int value){return value;}
void ouse_ui_use_font(lv_obj_t *object,unsigned size){(void)size;lv_obj_set_style_text_font(object,&lv_font_montserrat_16,0);}
static esp_err_t open_app(lv_obj_t *root,void *context){assert(root&&context);++opened;return ESP_OK;}
static void close_app(void *context){assert(context);++closed;}
static void changed(bool active){(void)active;}
int main(void){
 lv_init();lv_display_t *display=lv_display_create(320,480);assert(display);
 lv_obj_t *host=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(host);lv_obj_set_size(host,320,416);
 assert(ouse_apps_init(host,changed)==ESP_OK);ouse_apps_seal();
 for(unsigned index=0;index<100;++index){char id[48],name[48];snprintf(id,sizeof(id),"user.test%u",index);snprintf(name,sizeof(name),"Test %u",index);
  ouse_app_definition_t definition={.id=id,.name=name,.icon=LV_SYMBOL_OK,.open=open_app,.close=close_app,.context=&opened};
  assert(ouse_apps_user_apply(&definition)==ESP_OK);if(index==1)assert(ouse_app_open(id)==ESP_OK);
 }
 ouse_app_stats_t stats;assert(ouse_app_get_stats(&stats)==ESP_OK&&stats.registered==100&&!strcmp(stats.active_id,"user.test1"));
 ouse_app_info_t info;assert(ouse_app_get_info(99,&info)==ESP_OK&&!strcmp(info.id,"user.test99"));
 assert(ouse_apps_user_remove("user.test0")==ESP_OK);assert(ouse_app_get_stats(&stats)==ESP_OK&&stats.registered==99&&!strcmp(stats.active_id,"user.test1"));
 assert(ouse_app_close()==ESP_OK&&opened==1&&closed==1);
 lv_obj_t *viewport=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(viewport);lv_obj_set_size(viewport,320,440);
 lv_obj_t *field=lv_textarea_create(viewport);lv_obj_set_size(field,260,40);lv_obj_set_pos(field,10,350);lv_textarea_set_one_line(field,true);lv_textarea_set_text(field,"private-value");
 assert(ouse_ime_attach(field,viewport,OUSE_IME_RESIZE,OUSE_IME_PASSWORD)==ESP_OK);
 ouse_ime_set_bottom_inset(40);ouse_ime_focus(field);lv_obj_update_layout(lv_screen_active());
 ouse_ime_state_t ime;ouse_ime_get_state(&ime);assert(ime.visible&&ime.keyboard_bounds.y2==439&&lv_textarea_get_password_mode(field));
 lv_obj_t *reveal=NULL;for(unsigned index=0;index<lv_obj_get_child_count(field);++index){lv_obj_t *child=lv_obj_get_child(field,index);if(lv_obj_check_type(child,&lv_button_class))reveal=child;}
 assert(reveal);lv_obj_send_event(reveal,LV_EVENT_CLICKED,NULL);assert(!lv_textarea_get_password_mode(field)&&ouse_ime_focused(field));
 assert(!strcmp(lv_textarea_get_text(field),"private-value"));
 ouse_ime_set_bottom_inset(24);ouse_ime_get_state(&ime);assert(ime.keyboard_bounds.y2==455);
 ouse_ime_hide();assert(lv_textarea_get_password_mode(field)&&!ouse_ime_active());
 ouse_ime_focus(field);lv_obj_delete(viewport);assert(!ouse_ime_active());
 puts("PASS real LVGL: 100-app growth, live-entry relocation/removal, password reveal/remask, three-key/gesture IME insets and focus-delete cleanup");
}
'''
(OUT / 'check.c').write_text(source)
env = dict(os.environ)
for line in subprocess.check_output(['cmd.exe', '/d', '/c', str(CACHE/'environment.cmd')], encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key, value = line.split('=', 1)
        env[key] = value
compiler = Path(env['VCToolsInstallDir']) / 'bin/Hostx64/x64/cl.exe'
command = [str(compiler), '/nologo', '/std:c11', '/O2', '/MD', '/utf-8', '/DLV_KCONFIG_IGNORE', '/DLV_LVGL_H_INCLUDE_SIMPLE',
    '/DLV_CONF_PATH="'+(CACHE/'lv_conf.h').as_posix()+'"']
command += ['/I'+str(path) for path in [CACHE, LVGL, ROOT/'components/ouse_core/include', ROOT/'components/ouse_core', ROOT/'components/ouse_tasks/include']]
command += [str(OUT/'check.c'), str(ROOT/'components/ouse_core/ouse_apps.c'), str(ROOT/'components/ouse_core/ouse_ime.c'), str(CACHE/'lvgl-native.lib'), '/Fe'+str(OUT/'check.exe')]
result = subprocess.run(command, cwd=OUT, env=env, capture_output=True, text=True, encoding='utf-8', errors='replace')
assert result.returncode == 0, (result.stdout, result.stderr)
subprocess.run([str(OUT/'check.exe')], cwd=OUT, check=True, timeout=20)
