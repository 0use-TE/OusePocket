"""Reproduce the ASCII password bug and verify real LVGL UTF-8 input behavior."""
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'examples/brookesia-minimal/build/host-input'
OUT.mkdir(parents=True, exist_ok=True)
LV = ROOT / 'examples/brookesia-minimal/managed_components/lvgl__lvgl'
vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
vs = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
script = OUT / 'environment.cmd'
script.write_text(f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n', encoding='mbcs')
env = dict(os.environ)
for line in subprocess.check_output(['cmd','/d','/c',str(script)],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1); env[key]=value
source=OUT/'input.cpp'
source.write_text(r'''
#include "lvgl.h"
#include "src/misc/lv_text_private.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
void require(bool b,const char *s) {if(!b) throw std::runtime_error(s);}
int main(int argc,char**) {
 lv_init(); auto *display=lv_display_create(320,480);
 auto *input=lv_textarea_create(lv_screen_active());
 lv_textarea_set_one_line(input,true); lv_textarea_set_text(input,"");
 lv_textarea_set_max_length(input,64); lv_textarea_set_password_mode(input,true);
 lv_textarea_set_password_show_time(input,0);
 lv_textarea_add_text(input,"8"); lv_textarea_add_text(input,"8");
 auto *label=lv_textarea_get_label(input);
 const bool correct=lv_text_get_encoded_length(lv_label_get_text(label))==2;
 if(argc>1) {require(!correct,"ASCII bug did not reproduce"); std::cout<<"PASS: ASCII mode reproduces malformed masking after second 8\n"; return 0;}
 require(correct,"UTF-8 mask length mismatch after second 8");
 for(int i=0;i<62;++i) lv_textarea_add_text(input,"8");
 require(std::strlen(lv_textarea_get_text(input))==64,"Password length changed");
 require(lv_text_get_encoded_length(lv_label_get_text(label))==64,"Mask grew beyond password length");
 lv_textarea_add_text(input,"8"); require(std::strlen(lv_textarea_get_text(input))==64,"Max length exceeded");
 lv_textarea_delete_char(input); require(std::strlen(lv_textarea_get_text(input))==63,"Backspace failed");
 lv_textarea_set_password_mode(input,false); require(std::strcmp(lv_label_get_text(label),lv_textarea_get_text(input))==0,"Reveal corrupted password");
 lv_textarea_set_text(input,u8"测试 Wi-Fi");
 require(lv_text_get_encoded_length(lv_textarea_get_text(input))==8,"Chinese SSID decoded as bytes");
 lv_textarea_set_password_mode(input,true); lv_textarea_add_text(input,"8");
 require(lv_text_get_encoded_length(lv_textarea_get_text(input))==9,"Mixed input corrupted");
 lv_textarea_cursor_left(input); lv_textarea_add_text(input,"2"); lv_textarea_delete_char(input);
 require(lv_text_get_encoded_length(lv_textarea_get_text(input))==9,"Cursor edit corrupted text");
 lv_textarea_set_text(input,""); require(std::strlen(lv_textarea_get_text(input))==0,"Reopen didn't clear input");
 lv_obj_delete(input); lv_display_delete(display);
 std::cout<<"PASS: UTF-8 repeated 8, 64-character limit, mask, reveal, Chinese, cursor editing and clear\n";
}
'''.replace('u8"测试 Wi-Fi"','"测试 Wi-Fi"'),encoding='utf-8')
lib=ROOT/'examples/brookesia-minimal/build/host-shell-controls/compiled/lvgl.lib'
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
common=['/nologo','/utf-8','/MDd','/DLV_CONF_SKIP=1','/DLV_USE_STDLIB_MALLOC=1',f'/I{LV}']
def run(args):
    r=subprocess.run(args,env=env,cwd=OUT,capture_output=True,text=True,errors='replace')
    if r.returncode: print(r.stdout);print(r.stderr); raise SystemExit(r.returncode)
    return r.stdout
# Only lv_text.c selects the encoding function pointers. The remaining widgets
# are identical in both runs, using the existing real LVGL renderer library.
for mode in ['ASCII','UTF8']:
    obj=OUT/f'text-{mode}.obj'; exe=OUT/f'input-{mode}.exe'
    run([str(compiler),*common,f'/DLV_TXT_ENC=LV_TXT_ENC_{mode}','/c',str(LV/'src/misc/lv_text.c'),f'/Fo{obj}'])
    run([str(compiler),*common,'/EHsc',str(source),str(obj),str(lib),f'/Fe{exe}'])
    print(run([str(exe),*(['reproduce'] if mode=='ASCII' else [])]),end='')
