"""Run the actual QuickJS+LVGL backend on Windows; only hardware stats are fake."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import os
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".tools/ouse-runtime-check"
HOST = ROOT / ".tools/ouse-core-check"
OUT.mkdir(parents=True, exist_ok=True)
assert (HOST/"lvgl-native.lib").exists(), "Run test-ouse-core.py first"
LVGL = ROOT / "managed_components/lvgl__lvgl"
QJS = ROOT / "managed_components/espressif__quickjs-ng/quickjs-ng"
CORE = ROOT / "components/ouse_core"
RUNTIME = ROOT / "components/ouse_runtime"
subprocess.run([sys.executable, str(ROOT/"tools/embed-script-fixtures.py"), "--output", str(OUT)], check=True)
vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
vs = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires",
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
script = OUT / "environment.cmd"
script.write_text(f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n', encoding="mbcs")
env = dict(os.environ)
for line in subprocess.check_output(["cmd.exe", "/d", "/c", str(script)], encoding="mbcs").splitlines():
    if "=" in line and not line.startswith("="):
        key, value = line.split("=", 1)
        env[key] = value
compiler = Path(env["VCToolsInstallDir"]) / "bin/Hostx64/x64/cl.exe"
(OUT/"esp_timer.h").write_text('#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
(OUT/"esp_log.h").write_text('''#include <stdio.h>
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(tag, ...) do { fprintf(stderr,"%s: ",tag); fprintf(stderr,__VA_ARGS__); fputc('\n',stderr); } while(0)
'''.replace("fputc('\n'", "fputc('\\n'"))
exec((ROOT/"tools/fixtures/usb_host_setup.py").read_text(encoding="utf-8"))
fixture = r'''
#include <windows.h>
#include "ouse_core.h"
#include "ouse_device_driver.h"
#include "ouse_runtime.h"
#include "script_packages.h"
#include "usb-host.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t pixels[640*960*2], buffer[640*50*2];
static unsigned screen_width=320,screen_height=480;
static unsigned info_calls, brightness;
const char *esp_err_to_name(esp_err_t code) { switch(code){case ESP_ERR_NO_MEM:return "ESP_ERR_NO_MEM";case ESP_ERR_INVALID_ARG:return "ESP_ERR_INVALID_ARG";case ESP_ERR_INVALID_STATE:return "ESP_ERR_INVALID_STATE";case ESP_FAIL:return "ESP_FAIL";default:return "ESP_TEST_ERROR";} }
int64_t esp_timer_get_time(void) {
    LARGE_INTEGER count,frequency; QueryPerformanceCounter(&count); QueryPerformanceFrequency(&frequency);
    return (count.QuadPart/frequency.QuadPart)*1000000 + count.QuadPart%frequency.QuadPart*1000000/frequency.QuadPart;
}
esp_err_t ouse_system_get_info(ouse_system_info_t *info) {
    ++info_calls;
    *info=(ouse_system_info_t){.internal_free=400*1024,.internal_total=500*1024,
        .psram_free=30*1024*1024,.psram_total=32*1024*1024,.flash_bytes=16*1024*1024,
        .cpu_mhz=400,.uptime_ms=123000,.idf_version="v6.1"}; return ESP_OK;
}
static void flush(lv_display_t *display,const lv_area_t *a,uint8_t *data) {
    int width=a->x2-a->x1+1;
    for (int y=a->y1;y<=a->y2;++y) memcpy(pixels+(y*screen_width+a->x1)*2,data+(y-a->y1)*width*2,width*2);
    lv_display_flush_ready(display);
}
static bool lock(void *ctx,uint32_t timeout) { (void)ctx;(void)timeout;if(fail_ui_lock){fail_ui_lock=0;return false;}return true; }
static void unlock(void *ctx) { (void)ctx; }
static esp_err_t light(void *ctx,uint8_t value) { (void)ctx; brightness=value;return ESP_OK; }
static void touch(lv_indev_t *indev,lv_indev_data_t *data) { (void)indev; data->state=LV_INDEV_STATE_RELEASED; }
static void tick(unsigned ms) { lv_tick_inc(ms); lv_timer_handler(); }
static lv_obj_t *label(lv_obj_t *root,const char *value) {
    if (lv_obj_has_flag(root,LV_OBJ_FLAG_HIDDEN)) return NULL;
    if (lv_obj_check_type(root,&lv_label_class) && !strcmp(lv_label_get_text(root),value)) return root;
    for (unsigned i=0;i<lv_obj_get_child_count(root);++i) {
        lv_obj_t *found=label(lv_obj_get_child(root,i),value); if (found) return found;
    } return NULL;
}
static lv_obj_t *slider(lv_obj_t *root) {
    if (lv_obj_check_type(root,&lv_slider_class)) return root;
    for (unsigned i=0;i<lv_obj_get_child_count(root);++i) { lv_obj_t *result=slider(lv_obj_get_child(root,i));if(result)return result; }
    return NULL;
}
static void click(lv_obj_t *root,const char *value) {
    lv_obj_t *target=label(root,value); assert(target);
    while (target && !lv_obj_check_type(target,&lv_button_class)) target=lv_obj_get_parent(target);
    assert(target); lv_obj_send_event(target,LV_EVENT_CLICKED,NULL);
}
static unsigned objects(lv_obj_t *root) {
    unsigned n=1; for (unsigned i=0;i<lv_obj_get_child_count(root);++i)n+=objects(lv_obj_get_child(root,i));return n;
}
static unsigned timers(void) { unsigned n=0; for(lv_timer_t *t=lv_timer_get_next(NULL);t;t=lv_timer_get_next(t))++n;return n; }
static void picture(const char *name) {
    lv_refr_now(lv_display_get_default()); FILE *f=NULL; assert(!fopen_s(&f,name,"wb") && f);
    fprintf(f,"P6\n%u %u\n255\n",screen_width,screen_height);
    for(unsigned i=0;i<screen_width*screen_height;++i) {
        unsigned p=pixels[i*2]|((unsigned)pixels[i*2+1]<<8);
        uint8_t rgb[3]={(uint8_t)(((p>>11)&31)*255/31),(uint8_t)(((p>>5)&63)*255/63),(uint8_t)((p&31)*255/31)};
        fwrite(rgb,1,3,f);
    } fclose(f);
}
int main(int argc,char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);_set_error_mode(_OUT_TO_STDERR);setvbuf(stdout,NULL,_IONBF,0);
    assert(argc==2);
    const char *mode=argv[1];
    if(!strcmp(mode,"hdpi") || !strcmp(mode,"legacy")){screen_width=640;screen_height=960;}
    lv_init();
    ouse_device_port_t device_port={0};ouse_device_config_t device_config={.volume=40};
    assert(ouse_devices_init(&device_port,&device_config)==ESP_OK);
    ouse_devices_set_available(1,true,ESP_OK);ouse_devices_set_available(2,true,ESP_OK); lv_display_t *display=lv_display_create(screen_width,screen_height);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display,flush); lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_indev_t *input=lv_indev_create(); lv_indev_set_type(input,LV_INDEV_TYPE_POINTER); lv_indev_set_read_cb(input,touch);
    ouse_script_app_t states[4]={0}; ouse_app_definition_t definitions[4];
    ouse_script_package_t bad=ouse_package_counter;
    bad.id="ouse.fixture"; bad.name="Fixture";
    if (!strcmp(mode,"throw")) bad.script="throw new Error('startup failure');";
    else if (!strcmp(mode,"syntax")) bad.script="const broken = ;";
    else if (!strcmp(mode,"loop")) bad.script="while(true){}";
    else if (!strcmp(mode,"timer-loop")) bad.script="Ouse.timers.setInterval(()=>{while(true){}},100);";
    else if (!strcmp(mode,"event-loop")) bad.script="Ouse.ui.on('increment','click',()=>{while(true){}});";
    else if (!strcmp(mode,"pause-loop")) bad.script="globalThis.app={onPause(){while(true){}}};";
    else if (!strcmp(mode,"resume-loop")) bad.script="globalThis.app={onResume(){while(true){}}};";
    else if (!strcmp(mode,"memory")) bad.script="let data=[];for(;;)data.push(new Uint8Array(8192));";
    else if (!strcmp(mode,"timer-limit")) bad.script="for(let i=0;i<9;i++)Ouse.timers.setInterval(()=>{},1000);";
    else if (!strcmp(mode,"binding")) bad.script="Ouse.ui.setText('missing','test');";
    else if (!strcmp(mode,"mixed-rejections")) bad.script="Promise.reject(new Error('unhandled A'));const b=Promise.reject('handled B');b.catch(()=>{});";
    else if (!strcmp(mode,"rejection")) bad.script="Promise.reject(new Error('rejected'));";
    else if (!strcmp(mode,"jobs")) bad.script="function again(){Promise.resolve().then(again)}again();";
    else if (!strcmp(mode,"native-api")) bad.script="std.loadFile('anything');";
    else if (!strcmp(mode,"bad-json")) bad.page="{";
    else if (!strcmp(mode,"bad-type")) bad.page="{\"api\":1,\"root\":{\"type\":\"unknown\",\"id\":\"x\"}}";
    else if (!strcmp(mode,"bad-bounds")) bad.page="{\"api\":1,\"root\":{\"type\":\"container\",\"id\":\"x\",\"x\":0,\"y\":0,\"width\":321,\"height\":416}}";
    else if (!strcmp(mode,"duplicate-id")) bad.page="{\"api\":1,\"root\":{\"type\":\"container\",\"id\":\"x\",\"x\":0,\"y\":0,\"width\":320,\"height\":416,\"children\":[{\"type\":\"label\",\"id\":\"x\",\"x\":0,\"y\":0,\"width\":100,\"height\":20}]}}";
    else if(!strcmp(mode,"theme-loop"))bad.script="Ouse.theme.onChange(()=>{while(true){}});Ouse.theme.set('dark');";
    else if(!strcmp(mode,"locale-loop"))bad.script="Ouse.locale.onChange(()=>{while(true){}});Ouse.locale.set('en');";
    else if(!strcmp(mode,"display-loop"))bad.script="Ouse.display.onChange(()=>{while(true){}});Ouse.display.setFontPercent(130);";
    else if(!strcmp(mode,"power-loop"))bad.script="Ouse.power.onChange(()=>{while(true){}});Ouse.power.setTimeout(30);";
    else if(!strcmp(mode,"audio-loop"))bad.script="Ouse.audio.onChange(()=>{while(true){}});Ouse.audio.setVolume(41);";
    else if(!strcmp(mode,"wifi-loop"))bad.script="Ouse.wifi.onChange(()=>{while(true){}});Ouse.wifi.setEnabled(true);";
    else if(!strcmp(mode,"devices"))bad.script=
        "function check(ok){if(!ok)throw new Error('device API assertion');}"
        "let p=0,a=0,w=0;function view(){Ouse.ui.setText('value',p+'/'+a+'/'+w);}"
        "Ouse.power.onChange(()=>{++p;view();});Ouse.audio.onChange(()=>{++a;view();});Ouse.wifi.onChange(()=>{++w;view();});"
        "check(Ouse.power.setTimeout(30));check(!Ouse.power.setTimeout(14));check(Ouse.power.sleep());check(Ouse.power.wake());"
        "check(Ouse.audio.setVolume(65));check(Ouse.audio.setMuted(true));check(!Ouse.audio.record(4));"
        "check(Ouse.audio.testTone());check(Ouse.audio.stop());check(!Ouse.audio.playRecording());"
        "check(Ouse.wifi.setEnabled(true));check(!Ouse.wifi.connect('Office','short'));check(Ouse.wifi.connect('Office','abcdefgh'));"
        "check(!Ouse.wifi.scan());check(!('password' in Ouse.wifi.get()));"
        "check(Ouse.audio.get().volume===65 && Ouse.power.get().sleepSeconds===30);"
        "let threw=false;try{Ouse.audio.setVolume('bad')}catch(e){threw=true}check(threw);view();";
    else if(!strcmp(mode,"bad-layout"))bad.page="{\"api\":1,\"units\":\"dp\",\"root\":{\"type\":\"container\",\"id\":\"x\",\"width\":\"fill\",\"height\":\"fill\",\"layout\":\"diagonal\"}}";
    else if(!strcmp(mode,"bad-translations"))bad.page="{\"api\":1,\"units\":\"dp\",\"translations\":{\"en\":{\"title\":123}},\"root\":{\"type\":\"label\",\"id\":\"x\",\"width\":\"fill\",\"height\":\"content\"}}";
    else if(!strcmp(mode,"legacy")) {
        bad.page="{\"api\":1,\"root\":{\"type\":\"container\",\"id\":\"page\",\"x\":0,\"y\":0,\"width\":320,\"height\":416,\"children\":[{\"type\":\"label\",\"id\":\"value\",\"x\":24,\"y\":40,\"width\":200,\"height\":48},{\"type\":\"button\",\"id\":\"increment\",\"text\":\"+1\",\"x\":24,\"y\":120,\"width\":200,\"height\":48}]}}";
        bad.script="Ouse.ui.on('increment','click',()=>Ouse.ui.setText('value','legacy px'));";
    }
    else if (!strcmp(mode,"good-callbacks")) bad.script=
        "let t=Ouse.timers.setInterval(()=>{Ouse.timers.clear(t);Ouse.ui.setText('value','once');},100);"
        "Ouse.ui.on('increment','click',()=>{Ouse.ui.on('increment','click',()=>Ouse.ui.setText('value','replaced'));});"
        "Promise.resolve().then(()=>Ouse.ui.setText('elapsed','job ok'));"
        "Promise.reject('handled').catch(()=>{});";
    assert(ouse_script_prepare(NULL,&bad,&definitions[2])==ESP_ERR_INVALID_ARG);
    bad.api=999;assert(ouse_script_prepare(&states[2],&bad,&definitions[2])==ESP_ERR_INVALID_ARG);bad.api=1;
    assert(ouse_script_prepare(&states[0],&ouse_package_counter,&definitions[0])==ESP_OK);
    assert(ouse_script_prepare(&states[1],&ouse_package_resource_monitor,&definitions[1])==ESP_OK);
    assert(ouse_script_prepare(&states[2],&bad,&definitions[2])==ESP_OK);
    assert(ouse_script_prepare(&states[3],&ouse_package_api_test,&definitions[3])==ESP_OK);
    ouse_core_config_t config={.initial_brightness=45,.initial_theme=OUSE_THEME_LIGHT,.apps=definitions,.app_count=4,.density_dpi=screen_width==640 ? 320 : 160,
        .port={.display=display,.touch=input,.lock=lock,.unlock=unlock,.set_brightness=light,.board_name="Host P4"}};
    assert(ouse_core_start(&config)==ESP_OK); tick(30);
    lv_obj_t *root=lv_screen_active(); unsigned baseline_objects=objects(root),baseline_timers=timers();
    unsigned baseline_bindings=ouse_ui_binding_count();
    ouse_runtime_stats_t stats;
    if(!strcmp(mode,"usb-server")) { usb_server();return 0; }
    assert(ouse_script_validate(&ouse_package_counter)==ESP_OK);
    if(!strcmp(mode,"syntax") || !strncmp(mode,"bad-",4) || !strcmp(mode,"duplicate-id"))assert(ouse_script_validate(&bad)!=ESP_OK);
    if(!strcmp(mode,"close-lifecycle")) {
        const char *scripts[]={
            "globalThis.app={onPause(){Ouse.audio.setVolume(Ouse.audio.get().volume+1);}};",
            "globalThis.app={onPause(){Ouse.audio.setVolume(Ouse.audio.get().volume+1);throw new Error('pause failure');}};",
            "globalThis.app={onPause(){Ouse.audio.setVolume(Ouse.audio.get().volume+1);while(true){}}};",
            "globalThis.app={onPause(){Ouse.audio.setVolume(Ouse.audio.get().volume+1);}};throw new Error('startup failure');"
        };
        ouse_script_package_t first=ouse_package_counter,second=ouse_package_counter;
        first.id=second.id="user.lifecycle";first.name=second.name="Lifecycle";
        first.script=second.script=scripts[0];
        ouse_script_app_t a={0},b={0};ouse_app_definition_t da,db;
        assert(ouse_script_prepare(&a,&first,&da)==ESP_OK && ouse_script_prepare(&b,&second,&db)==ESP_OK);
        assert(ouse_core_user_apply(&da)==ESP_OK && ouse_app_open(first.id)==ESP_OK);
        assert(ouse_core_user_apply(&db)==ESP_OK && !a.instance); // foreground BPK upgrade
        ouse_audio_state_t audio;assert(ouse_audio_get(&audio)==ESP_OK && audio.volume==41);
        assert(ouse_app_open(second.id)==ESP_OK && ouse_core_user_remove(second.id)==ESP_OK && !b.instance);
        assert(ouse_audio_get(&audio)==ESP_OK && audio.volume==42); // foreground uninstall
        assert(ouse_core_user_apply(&da)==ESP_OK && ouse_app_open(first.id)==ESP_OK);
        assert(ouse_app_background()==ESP_OK);
        assert(ouse_audio_get(&audio)==ESP_OK && audio.volume==43);
        assert(ouse_core_user_remove(first.id)==ESP_OK && !a.instance);
        assert(ouse_audio_get(&audio)==ESP_OK && audio.volume==43); // already paused: no duplicate
        for(unsigned i=1;i<4;++i) {
            first.script=scripts[i];
            assert(ouse_core_user_apply(&da)==ESP_OK && ouse_app_open(first.id)==ESP_OK);
            int64_t started=esp_timer_get_time();
            assert(ouse_core_user_remove(first.id)==ESP_OK && !a.instance);
            assert(esp_timer_get_time()-started<2000000); // throwing/timed-out pause never blocks deletion
            assert(ouse_audio_get(&audio)==ESP_OK && audio.volume==43+(i<3 ? i : 2));
            tick(30);ouse_runtime_get_stats(&stats);
            assert(!stats.instances && !stats.contexts && !stats.widgets && !stats.timers && !stats.js_heap_bytes);
            assert(objects(root)==baseline_objects && timers()==baseline_timers && ouse_ui_binding_count()==baseline_bindings);
        }
        assert(ouse_app_open("ouse.counter")==ESP_OK && label(root,"0") && ouse_app_close()==ESP_OK);
        puts("PASS closing foreground BPKs pauses once before upgrade/uninstall; background close does not repeat; throwing/timed-out hooks and failed startup cleanly release resources");
        return 0;
    }
    if(!strcmp(mode,"devices")) {
        assert(ouse_app_open("ouse.fixture")==ESP_OK);tick(30);lv_obj_t *value=label(root,"1/1/1");assert(value);
        assert(ouse_app_background()==ESP_OK);
        assert(ouse_audio_set_volume(66)==ESP_OK && ouse_audio_set_volume(67)==ESP_OK);
        assert(ouse_power_set_timeout(60)==ESP_OK && ouse_power_set_timeout(0)==ESP_OK);
        assert(ouse_wifi_set_enabled(false)==ESP_OK);tick(200);
        assert(!strcmp(lv_label_get_text(value),"1/1/1"));
        assert(ouse_app_open("ouse.fixture")==ESP_OK);tick(30);assert(label(root,"2/2/2"));
        assert(ouse_app_close()==ESP_OK);tick(30);
        ouse_runtime_get_stats(&stats);assert(!stats.instances && !stats.contexts && !stats.timers);
        assert(objects(root)==baseline_objects && timers()==baseline_timers && ouse_ui_binding_count()==baseline_bindings);
        puts("PASS real JS device APIs: validation, queued commands, private credentials, coalesced foreground events, resume and cleanup");return 0;
    }
    if(!strcmp(mode,"legacy")) {
        assert(ouse_app_open("ouse.fixture")==ESP_OK);click(root,"+1");tick(30);
        lv_obj_t *value=label(root,"legacy px");assert(value);
        assert(lv_obj_get_x(value)==24 && lv_obj_get_width(value)==200);
        assert(ouse_ui_dp(24)==48);assert(ouse_app_close()==ESP_OK);
        assert(ouse_ui_binding_count()==baseline_bindings);
        puts("PASS legacy px page retains its pixel coordinates at density 320");return 0;
    }

    if (!strcmp(mode,"preferences") || !strcmp(mode,"hdpi")) {
        unsigned density=!strcmp(mode,"hdpi") ? 320 : 160;
        assert(ouse_ui_density()==density && ouse_ui_dp(24)==(int)(24*density/160));
        assert(ouse_ui_to_dp(screen_width)==320);
        assert(ouse_core_set_ui(3,100)==ESP_ERR_INVALID_ARG);
        assert(ouse_core_set_ui(0,101)==ESP_ERR_INVALID_ARG);
        assert(ouse_app_open("ouse.counter")==ESP_OK);click(root,"+1");click(root,"+1");tick(30);
        lv_obj_t *value=label(root,"2");assert(value);
        const lv_font_t *old_font=lv_obj_get_style_text_font(value,0);
        assert(ouse_core_set_ui(1,130)==ESP_OK);tick(30);
        assert(value==label(root,"2") && label(root,"Reset") && label(root,"Active time: 0 s"));
        assert(lv_obj_get_style_text_font(value,0)==ouse_ui_font(32));
        assert(lv_obj_get_style_text_font(value,0)!=old_font);
        assert(ouse_app_open("ouse.api-test")==ESP_OK);tick(30);
        click(root,"Toggle theme");tick(30);
        assert(ouse_theme_get()==OUSE_THEME_DARK && label(root,"Change events: 1 / 0 / 0"));
        click(root,"Switch language");tick(30);
        assert(label(root,"通知次数: 1 / 1 / 0") && label(root,"切换明暗"));
        click(root,"切换字号");tick(30);
        assert(ouse_ui_font_percent()==100 && label(root,"通知次数: 1 / 1 / 1"));
        lv_obj_t *brightness_slider=slider(root);assert(brightness_slider);
        lv_slider_set_value(brightness_slider,70,LV_ANIM_OFF);lv_obj_send_event(brightness_slider,LV_EVENT_VALUE_CHANGED,NULL);
        assert(brightness==70);
        tick(30);assert(label(root,"通知次数: 1 / 1 / 2"));
        assert(ouse_app_background()==ESP_OK);
        assert(ouse_core_set_ui(1,115)==ESP_OK); assert(ouse_core_set_ui(0,130)==ESP_OK);
        assert(ouse_core_set_theme(OUSE_THEME_LIGHT)==ESP_OK);tick(2000);
        assert(ouse_app_open("ouse.api-test")==ESP_OK);tick(30);
        assert(label(root,"通知次数: 2 / 2 / 3")); // coalesced while paused, not executed in background
        assert(ouse_app_open("ouse.counter")==ESP_OK && label(root,"2") && label(root,"清零"));
        assert(ouse_app_close()==ESP_OK && ouse_app_close_id("ouse.api-test")==ESP_OK);
        for(unsigned i=0;i<50;++i) {
            assert(ouse_app_open("ouse.api-test")==ESP_OK);
            assert(ouse_core_set_ui(i%2,i%3==0 ? 100 : i%3==1 ? 115 : 130)==ESP_OK);tick(30);
            assert(ouse_app_close()==ESP_OK);
        }
        assert(ouse_core_set_ui(0,100)==ESP_OK && ouse_core_set_theme(OUSE_THEME_LIGHT)==ESP_OK);tick(30);
        ouse_runtime_get_stats(&stats);assert(!stats.instances && !stats.contexts && !stats.timers);
        assert(objects(root)==baseline_objects && timers()==baseline_timers);
        assert(ouse_ui_binding_count()==baseline_bindings);
        assert(ouse_app_open("ouse.api-test")==ESP_OK);tick(30);
        if(density==160)picture("script-api-test.ppm");
        assert(ouse_core_set_ui(1,130)==ESP_OK);tick(30);
        if(density==160)picture("script-api-test-en-large.ppm");
        else picture("script-api-test-hdpi.ppm");
        assert(ouse_app_close()==ESP_OK);
        assert(ouse_app_open("ouse.resources")==ESP_OK);tick(30);
        if(density==160)picture("script-resources-en-large.ppm");
        assert(ouse_app_close()==ESP_OK);
        assert(ouse_app_open("ouse.settings")==ESP_OK);tick(30);
        if(density==160)picture("script-settings-en-large.ppm");
        assert(ouse_app_close()==ESP_OK);
        if(density==160)picture("script-desktop-en-large.ppm");
        printf("PASS preferences: density=%u; live localization/fonts; coalesced events; retained JS state; cleanup\n",density);
        return 0;
    }
    if (!strcmp(mode,"normal")) {
        assert(ouse_app_open("ouse.counter")==ESP_OK && label(root,"0"));
        click(root,"+1");click(root,"+1");assert(label(root,"2"));tick(1000);assert(label(root,"本次运行: 1 s"));
        picture("script-counter.ppm");
        assert(ouse_app_background()==ESP_OK);tick(10000);
        assert(ouse_app_open("ouse.counter")==ESP_OK && label(root,"2") && label(root,"本次运行: 1 s"));
        tick(1000);assert(label(root,"本次运行: 2 s"));
        click(root,"清零");assert(label(root,"0"));
        assert(ouse_app_open("ouse.resources")==ESP_OK);
        assert(!label(root,"正在读取") && label(root,"刷新间隔: 1000 ms") && info_calls==1);
        ouse_runtime_get_stats(&stats);assert(stats.contexts==2 && stats.timers==2 && stats.js_heap_bytes<=2*OUSE_RUNTIME_MEMORY_LIMIT);
        picture("script-resources.ppm");
        lv_obj_t *control=slider(root); assert(control);
        lv_slider_set_value(control,3,LV_ANIM_OFF);lv_obj_send_event(control,LV_EVENT_VALUE_CHANGED,NULL);
        assert(label(root,"刷新间隔: 3000 ms"));
        unsigned reads=info_calls;tick(1000);assert(info_calls==reads);tick(2000);assert(info_calls==reads+1);
        assert(ouse_core_set_theme(OUSE_THEME_DARK)==ESP_OK); picture("script-resources-dark.ppm");
        click(root,"回到桌面");reads=info_calls;tick(20000);assert(info_calls==reads);
        assert(ouse_app_open("ouse.resources")==ESP_OK && info_calls==reads+1);
        assert(ouse_app_close()==ESP_OK);
        assert(ouse_app_close_id("ouse.counter")==ESP_OK); tick(30);
        ouse_runtime_get_stats(&stats);assert(!stats.instances && !stats.contexts && !stats.timers && !stats.js_heap_bytes);
        for(unsigned i=0;i<100;++i) {
            assert(ouse_app_open("ouse.counter")==ESP_OK);click(root,"+1");tick(1000);
            assert(ouse_app_open("ouse.resources")==ESP_OK);tick(1000);
            assert(ouse_app_open("ouse.counter")==ESP_OK && label(root,"1"));
            assert(ouse_app_background()==ESP_OK);
            assert(ouse_app_close_id("ouse.counter")==ESP_OK && ouse_app_close_id("ouse.resources")==ESP_OK);
            tick(30);ouse_runtime_get_stats(&stats);
            assert(!stats.instances && !stats.contexts && !stats.widgets && !stats.timers && !stats.js_heap_bytes);
            assert(objects(root)==baseline_objects && timers()==baseline_timers);
        }
        puts("PASS real QuickJS+LVGL: JSON widgets/image, JS bindings/timers, immediate system data, pause/resume, themes, 100 cleanup rounds");
    } else {
        int64_t started=esp_timer_get_time();
        assert(ouse_app_open("ouse.fixture")==ESP_OK);
        if (!strcmp(mode,"event-loop")) click(root,"+1");
        if (!strcmp(mode,"pause-loop") || !strcmp(mode,"resume-loop")) {
            assert(ouse_app_background()==ESP_OK);
            assert(ouse_app_open("ouse.fixture")==ESP_OK);
        }
        tick(200);
        if (!strcmp(mode,"good-callbacks")) {
            assert(label(root,"once") && label(root,"job ok"));
            click(root,"+1");click(root,"+1");assert(label(root,"replaced"));
        } else {
            assert(label(root,ouse_ui_tr("应用运行出错")));
            ouse_runtime_get_stats(&stats);assert(stats.errors==1 && !stats.timers);
            assert(esp_timer_get_time()-started<2000000);
            picture("script-error.ppm");
        }
        click(root,ouse_ui_tr("回到桌面"));
        assert(ouse_app_close_id("ouse.fixture")==ESP_OK);tick(30);
        ouse_runtime_get_stats(&stats);assert(!stats.instances && !stats.contexts && !stats.widgets && !stats.timers && !stats.js_heap_bytes);
        assert(objects(root)==baseline_objects && timers()==baseline_timers);
        assert(ouse_app_open("ouse.counter")==ESP_OK && label(root,"0")); // another app survives
        assert(ouse_app_close()==ESP_OK);
        printf("PASS runtime case: %s; resources reclaimed and another app works\n",mode);
    }
    return 0;
}
'''
(OUT/"check.c").write_text(fixture, encoding="utf-8")
includes = [OUT, HOST, LVGL, CORE, CORE/"include", ROOT/"components/ouse_devices/include", RUNTIME/"include", ROOT/"components/ouse_system/include", ROOT/"components/ouse_installer", ROOT/"components/ouse_installer/include", QJS]
common = [str(compiler), "/nologo", "/c", "/O2", "/MD", "/utf-8", "/D_CRT_SECURE_NO_WARNINGS",
          "/D_GNU_SOURCE", "/DQUICKJS_NG_BUILD", "/D__STDC_NO_ATOMICS__=1",
          "/DLV_KCONFIG_IGNORE", "/DLV_LVGL_H_INCLUDE_SIMPLE", '/DLV_CONF_PATH="'+(HOST/"lv_conf.h").as_posix()+'"']
common += ["/I"+str(p) for p in includes]
qjs_library=OUT/"quickjs-native.lib"
qjs_files=[QJS/name for name in ["quickjs.c", "dtoa.c", "libregexp.c", "libunicode.c"]]
def compile_one(source, target):
    command=common+["/std:c++17", "/EHsc"] if source.suffix==".cpp" else common+["/std:c11"]
    result=subprocess.run(command+["/Fo"+str(target), str(source)], cwd=OUT, env=env,
                          capture_output=True, text=True, errors="replace", timeout=180)
    assert result.returncode==0, (source, result.stdout, result.stderr)
    return target
qjs_inputs=qjs_files+list(QJS.glob("*.h"))
if not qjs_library.exists() or qjs_library.stat().st_mtime < max(p.stat().st_mtime for p in qjs_inputs):
    print("Compiling real QuickJS-NG 0.14.0 for host",flush=True)
    with ThreadPoolExecutor(max_workers=4) as workers:
        qjs_objects=list(workers.map(lambda p: compile_one(p, OUT/(p.stem+".obj")), qjs_files))
    subprocess.run([str(compiler.parent/"lib.exe"), "/nologo", "/OUT:"+str(qjs_library), *map(str,qjs_objects)], cwd=OUT, env=env, check=True)
sources = [OUT/"check.c", OUT/"script_packages.c", RUNTIME/"ouse_runtime.c", ROOT/"components/ouse_installer/ouse_bpk.c", ROOT/"components/ouse_installer/ouse_protocol.c"]
sources += [CORE/name for name in ["ouse_setup.c", "ouse_core.c", "ouse_apps.c", "ouse_shell.c", "ouse_theme.c", "ouse_navigation.cpp", "ouse_ui.c", "ouse_widgets.c", "ouse_dialog.c", "ouse_screen.c", "ouse_device_ui.c"]]
sources += [ROOT/"components/ouse_devices/ouse_devices.c"]
sources += sorted(CORE.glob("ouse_font_ui_*.c"))
with ThreadPoolExecutor(max_workers=6) as workers:
    objects=list(workers.map(lambda pair: compile_one(pair[1], OUT/f"app-{pair[0]}.obj"), enumerate(sources)))
result=subprocess.run([str(compiler), "/nologo", *map(str,objects), str(HOST/"lvgl-native.lib"), str(qjs_library),
    "Ws2_32.lib", "/Fe"+str(OUT/"check.exe")], cwd=OUT, env=env, capture_output=True, text=True, errors="replace")
assert result.returncode==0, (result.stdout,result.stderr)
for case in ["preferences", "hdpi", "legacy", "normal", "devices", "close-lifecycle", "good-callbacks", "theme-loop", "locale-loop", "display-loop", "power-loop", "audio-loop", "wifi-loop", "bad-layout", "bad-translations", "throw", "syntax", "loop", "timer-loop", "event-loop", "pause-loop", "resume-loop",
             "memory", "timer-limit", "binding", "rejection", "mixed-rejections", "jobs", "native-api", "bad-json", "bad-type", "bad-bounds", "duplicate-id"]:
    if "--usb-only" not in sys.argv:
        subprocess.run([str(OUT/"check.exe"),case],cwd=OUT,check=True,timeout=30)
from PIL import Image
for path in OUT.glob("*.ppm"):
    with Image.open(path) as preview: preview.save(path.with_suffix(".png"))
print("PASS native runtime verified against genuine QuickJS-NG 0.14.0 and LVGL 9.5.0; no device accessed")

subprocess.run([sys.executable,str(ROOT/"tools/test-ouse-installer.py")],check=True)
