"""Build and exercise Ouse Core with real LVGL on the host (no ESP firmware build)."""
from pathlib import Path
import json, os, re, subprocess, sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".tools/ouse-core-check"
OUT.mkdir(parents=True, exist_ok=True)
LVGL = Path(os.environ.get("OUSE_CORE_COMPONENT_CACHE", str(ROOT / "managed_components"))) / "lvgl__lvgl"
assert LVGL.is_dir(), "Set up the pinned LVGL 9.5.0 dependency first"
CORE = ROOT / "components/ouse_core"
SYSTEM_INCLUDE = ROOT / "components/ouse_system/include"
vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
vs = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires",
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
envscript = OUT / "environment.cmd"
envscript.write_text(f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n', encoding="mbcs")
env = dict(os.environ)
for line in subprocess.check_output(["cmd.exe", "/d", "/c", str(envscript)], encoding="mbcs").splitlines():
    if "=" in line and not line.startswith("="):
        k, v = line.split("=", 1)
        env[k] = v
compiler = Path(env["VCToolsInstallDir"]) / "bin/Hostx64/x64/cl.exe"
def write_if_changed(path, content):
    if not path.exists() or path.read_text(encoding="utf-8") != content:
        path.write_text(content, encoding="utf-8")
(OUT / "esp_err.h").write_text("""#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT 0x107
""")
write_if_changed(OUT / "lv_conf.h", """#pragma once
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_USE_OS LV_OS_NONE
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_DRAW_UNIT_CNT 1
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_USE_LABEL 1
#define LV_USE_BUTTON 1
#define LV_USE_BAR 1
#define LV_USE_SLIDER 1
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_THORVG_INTERNAL 0
#define LV_USE_THORVG_EXTERNAL 0
#define LV_USE_LODEPNG 0
#define LV_USE_LIBPNG 0
#define LV_USE_FREETYPE 0
#define LV_USE_SDL 0
#define LV_USE_SNAPSHOT 1
#define LV_USE_IMAGE 1
#define LV_DRAW_SW_COMPLEX 1
#define LV_USE_DEMO_WIDGETS 0
""")
(OUT / "esp_log.h").write_text("#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGE(...) ((void)0)\n")
chars = sorted({ord(c) for c in re.findall(r'[^\x00-\x7f]', "\n".join(p.read_text(encoding="utf-8") for p in [CORE/"ouse_core.c", CORE/"ouse_shell.c", ROOT/"main/counter_app.c"]))})
fixture = r'''
#include "ouse_core.h"
#include "counter_app.h"
#include "ouse_apps_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
LV_FONT_DECLARE(ouse_font_ui_16)
static uint8_t pixels[320 * 480 * 2], buffer[320 * 50 * 2];
static unsigned brightness, calls, locks, unlocks, flushes;
static bool allow_lock = true, allow_brightness = true;
static bool allow_save = true, allow_info = true;
static unsigned saved_requests, ui_calls;
static ouse_settings_state_t saved;
static void posted(void *context) { assert(context == &ui_calls); ++ui_calls; }
static esp_err_t save_setting(void *ctx, uint8_t value) {
    (void)ctx; ++saved_requests;
    if (!allow_save) return ESP_FAIL;
    saved.pending = true; saved.requested_brightness = value; saved.requested_theme=(uint8_t)ouse_theme_get(); return ESP_OK;
}
static esp_err_t save_theme(void *ctx, uint8_t value, uint8_t brightness_value) {
    (void)ctx; if (!allow_save) return ESP_FAIL;
    saved.pending=true; saved.requested_theme=value; saved.requested_brightness=brightness_value; return ESP_OK;
}
static esp_err_t settings_state(void *ctx, ouse_settings_state_t *out) { (void)ctx; *out = saved; return ESP_OK; }
static esp_err_t system_info(void *ctx, ouse_system_info_t *out) {
    (void)ctx; if (!allow_info) return ESP_FAIL;
    *out = (ouse_system_info_t){.cpu_mhz=400, .internal_free=400*1024, .internal_total=500*1024,
        .psram_free=30*1024*1024, .psram_total=32*1024*1024, .flash_bytes=16*1024*1024,
        .uptime_ms=123000, .idf_version="v6.1"}; return ESP_OK;
}
static void tick(void) { lv_tick_inc(1200); lv_timer_handler(); }
static void flush(lv_display_t *display, const lv_area_t *a, uint8_t *data) {
    int width = a->x2 - a->x1 + 1;
    for (int y = a->y1; y <= a->y2; ++y)
        memcpy(pixels + (y * 320 + a->x1) * 2, data + (y - a->y1) * width * 2, width * 2);
    ++flushes;
    lv_display_flush_ready(display);
}
static bool lock(void *ctx, uint32_t timeout) {
    (void)ctx; assert(timeout == 2000); ++locks; return allow_lock;
}
static void unlock(void *ctx) { (void)ctx; ++unlocks; }
static esp_err_t set_brightness(void *ctx, uint8_t percent) {
    (void)ctx; ++calls; if (!allow_brightness) return ESP_FAIL;
    brightness = percent; return ESP_OK;
}
static lv_point_t touch_point;
static lv_indev_state_t touch_state=LV_INDEV_STATE_RELEASED;
static void read_touch(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev; data->state=touch_state; data->point=touch_point;
}
static void touch_step(int x, int y, bool down) {
    touch_point=(lv_point_t){x,y}; touch_state=down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    lv_tick_inc(50); lv_timer_handler();
}
static void swipe(int x, int y, int stop_x, int stop_y) {
    touch_step(x,y,true);
    for (unsigned i=1;i<=6;++i) touch_step(x+(stop_x-x)*(int)i/6,y+(stop_y-y)*(int)i/6,true);
    touch_step(stop_x,stop_y,false); tick();
}
static lv_obj_t *label(lv_obj_t *root, const char *value) {
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return NULL;
    if (lv_obj_check_type(root, &lv_label_class) && strcmp(lv_label_get_text(root), value) == 0) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        lv_obj_t *found = label(lv_obj_get_child(root, i), value);
        if (found) return found;
    }
    return NULL;
}
static lv_obj_t *slider(lv_obj_t *root) {
    if (lv_obj_check_type(root, &lv_slider_class)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        lv_obj_t *found = slider(lv_obj_get_child(root, i));
        if (found) return found;
    }
    return NULL;
}
static unsigned count(lv_obj_t *root) {
    unsigned result = 1;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) result += count(lv_obj_get_child(root, i));
    return result;
}
static void click(lv_obj_t *root, const char *value) {
    lv_obj_t *target = label(root, value);
    assert(target);
    lv_obj_t *button=lv_obj_get_parent(target);
    while (button && !lv_obj_check_type(button,&lv_button_class)) button=lv_obj_get_parent(button);
    assert(button);
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
}
static void screenshot(const char *name) {
    lv_refr_now(lv_display_get_default());
    FILE *file = NULL;
    assert(fopen_s(&file, name, "wb") == 0 && file);
    fprintf(file, "P6\n320 480\n255\n");
    for (unsigned i = 0; i < 320 * 480; ++i) {
        uint16_t p = (uint16_t)(pixels[i*2] | ((uint16_t)pixels[i*2+1] << 8));
        uint8_t rgb[3] = {(uint8_t)(((p >> 11) & 31)*255/31),
                          (uint8_t)(((p >> 5) & 63)*255/63), (uint8_t)((p & 31)*255/31)};
        fwrite(rgb, 1, 3, file);
    }
    fclose(file);
    unsigned drawn=0;
    for (unsigned y=80;y<400;++y) for (unsigned x=0;x<320;++x) {
        unsigned i=(y*320+x)*2;
        uint16_t bg=ouse_theme_get()==OUSE_THEME_DARK ? 0x39c7 : 0xf79e;
        if (pixels[i]!=(uint8_t)bg || pixels[i+1]!=(uint8_t)(bg>>8)) ++drawn;
    }
    assert(drawn>1000); // catches zero-size app roots which render a blank screen
}
static unsigned paused_timers(void) {
    unsigned n=0; for (lv_timer_t *t=lv_timer_get_next(NULL);t;t=lv_timer_get_next(t)) if (lv_timer_get_paused(t)) ++n;
    return n;
}
static unsigned timers(void) {
    unsigned n=0;
    for (lv_timer_t *t=lv_timer_get_next(NULL); t; t=lv_timer_get_next(t)) ++n;
    return n;
}
static lv_timer_t *failed_timer;
static unsigned failed_cleanup;
static void noop(lv_timer_t *timer) { (void)timer; }
static esp_err_t fail_open(lv_obj_t *root, void *context) {
    (void)context; lv_label_set_text(lv_label_create(root), "partial app");
    failed_timer=lv_timer_create(noop,1000,NULL);
    assert(ouse_app_close()==ESP_ERR_INVALID_STATE);
    assert(ouse_app_open("ouse.counter")==ESP_ERR_INVALID_STATE);
    return ESP_FAIL;
}
static void fail_close(void *context) {
    (void)context; if (failed_timer) lv_timer_delete(failed_timer);
    failed_timer=NULL; ++failed_cleanup;
}
int main(int argc, char **argv) {
    (void)argv;
    lv_init();
    lv_display_t *display = lv_display_create(320, 480);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, flush);
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_indev_t *touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, read_touch);
    char test_name[]="Test";
    ouse_app_definition_t definitions[7] = {ouse_counter_app,
        {.id="ouse.fail", .name=test_name, .icon=LV_SYMBOL_WARNING, .open=fail_open, .close=fail_close}};
    const char *extra_ids[]={"ouse.extra2","ouse.extra3","ouse.extra4","ouse.extra5","ouse.extra6"};
    const char *extra_names[]={"Extra2","Extra3","Extra4","Extra5","Extra6"};
    for (unsigned i=2;i<7;++i) {
        definitions[i]=definitions[1]; definitions[i].id=extra_ids[i-2]; definitions[i].name=extra_names[i-2];
    }
    bool pages=argc>1 && !strcmp(argv[1],"--launcher-pages");
    ouse_core_config_t config = {.port = {
        .display = display, .touch = touch, .lock = lock, .unlock = unlock,
        .set_brightness = set_brightness, .save_brightness = save_setting, .save_theme = save_theme,
        .get_settings_state = settings_state, .get_system_info = system_info, .board_name = "Waveshare P4 LCD 3.5"}, .initial_brightness = 45, .apps = definitions, .app_count = (pages ? 7 : argc>1 ? 2 : 1)};
    assert(!ouse_core_is_started() && strcmp(ouse_core_version(), "0.3.0") == 0);
    assert(ouse_core_post_ui(posted, &ui_calls) == ESP_ERR_INVALID_STATE);
    assert(ouse_core_start(NULL) == ESP_ERR_INVALID_ARG);
    config.initial_brightness = 0;
    assert(ouse_core_start(&config) == ESP_ERR_INVALID_ARG);
    config.initial_brightness = 45; allow_lock = false;
    assert(ouse_core_start(&config) == ESP_ERR_TIMEOUT && !ouse_core_is_started());
    allow_lock = true; lv_display_set_resolution(display, 240, 320);
    assert(ouse_core_start(&config) == ESP_ERR_NOT_SUPPORTED);
    lv_display_set_resolution(display, 320, 480); allow_brightness = false;
    assert(ouse_core_start(&config) == ESP_FAIL && !ouse_core_is_started());
    allow_brightness = true;
    size_t requested_count=config.app_count;
    config.app_count=OUSE_APP_CAPACITY;
    assert(ouse_core_start(&config)==ESP_ERR_INVALID_ARG);
    config.app_count=2;
    definitions[1].id="ouse.counter";
    assert(ouse_core_start(&config)==ESP_ERR_INVALID_ARG);
    definitions[1].id="ouse.settings";
    assert(ouse_core_start(&config)==ESP_ERR_INVALID_ARG);
    definitions[1].id="ouse.fail";
    config.app_count=requested_count;
    assert(ouse_core_start(&config) == ESP_OK && ouse_core_is_started());
    assert(brightness == 45);
    assert(ouse_core_start(&config) == ESP_ERR_INVALID_STATE);
    lv_obj_t *root = lv_screen_active();
    assert(label(root, LV_SYMBOL_SETTINGS) && !label(root, "屏幕亮度"));
    if (pages) {
        tick();
        ouse_app_stats_t state; assert(ouse_app_get_stats(&state)==ESP_OK && state.registered==8);
        ouse_app_info_t info; assert(ouse_app_get_info(7,&info)==ESP_OK && !strcmp(info.id,"ouse.extra6"));
        swipe(220,300,60,300);
        bool moved=false;
        for (unsigned i=0;i<lv_obj_get_child_count(root);++i) {
            lv_obj_t *home=lv_obj_get_child(root,i);
            if (!lv_obj_is_visible(home)) continue;
            for (unsigned j=0;j<lv_obj_get_child_count(home);++j) {
                lv_obj_t *grid=lv_obj_get_child(home,j);
                if (lv_obj_get_scroll_dir(grid)==LV_DIR_HOR && lv_obj_get_scroll_x(grid)>0) moved=true;
            }
        }
        assert(moved); screenshot("desktop-page2.ppm");
        puts("PASS all eight registered apps, archived grid capacity and horizontal launcher paging");
        return 0;
    }
    if (argc>1) {
        tick(); unsigned baseline=count(root), baseline_timers=timers();
        test_name[0]='X';
        ouse_app_info_t entry;
        assert(ouse_app_get_info(2,&entry)==ESP_OK && !strcmp(entry.name,"Test"));
        assert(ouse_apps_register(&ouse_counter_app)==ESP_ERR_INVALID_STATE);
        assert(ouse_app_open("ouse.counter")==ESP_OK);
        assert(ouse_app_open("ouse.fail")==ESP_FAIL && failed_cleanup==1);
        assert(ouse_app_close_id("ouse.counter")==ESP_OK);
        assert(label(root,LV_SYMBOL_SETTINGS) && count(root)==baseline && timers()==baseline_timers);
        ouse_app_stats_t state; assert(ouse_app_get_stats(&state)==ESP_OK);
        assert(state.live_instances==0 && state.failed_opens==1 && state.opened==state.closed);
        for (unsigned i=0;i<100;++i) assert(ouse_app_open("ouse.fail")==ESP_FAIL);
        tick(); assert(count(root)==baseline && timers()==baseline_timers && failed_cleanup==101);
        assert(ouse_app_open("ouse.settings")==ESP_OK && label(root,"屏幕亮度"));
        assert(ouse_app_close()==ESP_OK);
        puts("PASS partial app failure cleanup, reentry rejection, copied registry, desktop recovery");
        return 0;
    }
    const uint32_t chars[] = {CHARS};
    for (unsigned i = 0; i < sizeof(chars)/sizeof(chars[0]); ++i) {
        lv_font_glyph_dsc_t glyph;
        assert(lv_font_get_glyph_dsc(&ouse_font_ui_16, &glyph, chars[i], 0));
        assert(!glyph.is_placeholder);
    }
    assert(ouse_core_post_ui(NULL, NULL) == ESP_ERR_INVALID_ARG);
    allow_lock = false;
    assert(ouse_core_post_ui(posted, &ui_calls) == ESP_ERR_TIMEOUT);
    allow_lock = true;
    for (unsigned i=0; i<8; ++i) assert(ouse_core_post_ui(posted, &ui_calls) == ESP_OK);
    assert(ui_calls == 0);
    assert(ouse_core_post_ui(posted, &ui_calls) == ESP_ERR_NO_MEM);
    tick(); assert(ui_calls == 8);
    assert(ouse_core_post_ui(posted, &ui_calls) == ESP_OK);
    tick(); assert(ui_calls == 9);
    screenshot("desktop.ppm");
    assert(pixels[(320*480-1)*2] == 0x9e && pixels[(320*480-1)*2+1] == 0xf7);
    assert(flushes > 0);
    unsigned objects = count(root), baseline_timers = timers();
    click(root, LV_SYMBOL_SETTINGS);
    assert(label(root, "屏幕亮度") && !label(root, LV_SYMBOL_SETTINGS));
    lv_obj_t *control = slider(root); assert(control);
    lv_slider_set_value(control, 75, LV_ANIM_OFF);
    lv_obj_send_event(control, LV_EVENT_VALUE_CHANGED, NULL);
    assert(brightness == 75 && label(root, "75%"));
    assert(saved_requests == 0); // dragging never writes/queues Flash
    lv_obj_send_event(control, LV_EVENT_RELEASED, NULL);
    assert(saved_requests == 1 && saved.requested_brightness == 75);
    tick(); assert(label(root, "正在保存亮度"));
    saved.pending = false; saved.last_error = ESP_FAIL;
    tick(); assert(label(root, "保存失败，请点击保存重试"));
    allow_save = false; click(root, "保存"); tick(); assert(label(root, "保存失败，请点击保存重试"));
    allow_save = true; click(root, "保存");
    saved.pending = false; saved.last_error = ESP_OK; saved.saved_brightness = 75;
    tick(); assert(label(root, "已保存，重启保留"));
    screenshot("settings.ppm");
    click(root, "系统信息");
    assert(label(root, "返回设置") && !label(root, "屏幕亮度"));
    screenshot("system.ppm");
    allow_info = false; tick(); assert(label(root, "系统信息读取失败"));
    allow_info = true; tick(); assert(!label(root, "系统信息读取失败"));
    click(root, "返回设置");
    allow_brightness = false;
    lv_slider_set_value(control, 90, LV_ANIM_OFF);
    lv_obj_send_event(control, LV_EVENT_VALUE_CHANGED, NULL);
    assert(brightness == 75 && lv_slider_get_value(control) == 75);
    assert(label(root, "亮度设置失败，请重试"));
    allow_brightness = true;
    click(root, "返回");
    assert(ouse_app_close_id("ouse.settings")==ESP_OK);
    for (unsigned i = 0; i < 200; ++i) {
        click(root, LV_SYMBOL_SETTINGS); click(root, "回到桌面");
        assert(ouse_app_close_id("ouse.settings")==ESP_OK);
    }
    assert(label(root, LV_SYMBOL_SETTINGS) && count(root) == objects);
    assert(timers()==baseline_timers);
    ouse_app_stats_t state;
    assert(ouse_app_get_stats(&state)==ESP_OK && state.registered==2 && !state.live_instances);
    assert(ouse_app_get_info(99,NULL)==ESP_ERR_INVALID_ARG);
    assert(ouse_app_open(NULL)==ESP_ERR_INVALID_ARG);
    click(root, LV_SYMBOL_PLUS);
    assert(label(root,"0"));
    assert(ouse_app_get_stats(&state)==ESP_OK && state.live_instances==1 && !strcmp(state.active_id,"ouse.counter"));
    unsigned one_instance=count(root), one_timer=timers();
    assert(ouse_app_open("ouse.counter")==ESP_OK && count(root)==one_instance && timers()==one_timer);
    assert(ouse_app_open("missing")==ESP_ERR_NOT_FOUND && label(root,"0"));
    click(root,"+1"); click(root,"+1"); assert(label(root,"2"));
    click(root,"清零"); assert(label(root,"0"));
    tick(); screenshot("counter.ppm");
    click(root,"回到桌面");
    assert(ouse_app_close_id("ouse.counter")==ESP_OK);
    assert(count(root)==objects && timers()==baseline_timers);
    for (unsigned i=0; i<200; ++i) {
        click(root,LV_SYMBOL_PLUS); assert(label(root,"0"));
        click(root,"+1"); tick(); assert(label(root,"1"));
        assert(ouse_app_open("ouse.settings")==ESP_OK && label(root,"屏幕亮度"));
        assert(ouse_app_open("ouse.counter")==ESP_OK && label(root,"1"));
        click(root,"回到桌面"); tick();
        assert(ouse_app_close_id("ouse.settings")==ESP_OK);
        assert(ouse_app_close_id("ouse.counter")==ESP_OK);
        assert(count(root)==objects && timers()==baseline_timers);
    }
    assert(ouse_app_close()==ESP_OK); // already on desktop is harmless
    assert(ouse_app_get_stats(&state)==ESP_OK && !state.live_instances && state.opened==state.closed);
    // Exercise actual LVGL pointer input, not synthetic gesture callbacks.
    click(root,LV_SYMBOL_PLUS); click(root,"+1");
    swipe(4,240,84,240);
    assert(paused_timers()>=1);
    assert(ouse_app_get_stats(&state)==ESP_OK && !state.active_id[0] && state.live_instances==1 && state.preview_bytes>0);
    click(root,LV_SYMBOL_PLUS); assert(label(root,"1"));
    swipe(316,240,236,240);
    assert(ouse_app_get_stats(&state)==ESP_OK && !state.active_id[0]);
    swipe(160,14,160,110); assert(label(root,"快捷设置"));
    lv_obj_t *quick=lv_obj_get_parent(label(root,"快捷设置"));
    lv_obj_t *quick_slider=slider(quick);
    lv_slider_set_value(quick_slider,55,LV_ANIM_OFF);
    lv_obj_send_event(quick_slider,LV_EVENT_VALUE_CHANGED,NULL);
    lv_obj_send_event(quick_slider,LV_EVENT_RELEASED,NULL);
    assert(brightness==55 && saved.requested_brightness==55);
    saved.pending=false; saved.saved_brightness=55; saved.last_error=ESP_OK;
    tick(); screenshot("quick.ppm");
    swipe(160,320,160,220); assert(!label(root,"快捷设置"));
    swipe(160,474,160,370); assert(label(root,"最近应用"));
    screenshot("recents.ppm");
    touch_step(160,240,true); touch_step(160,240,false); tick();
    assert(!label(root,"最近应用") && label(root,"1"));
    click(root,"+1"); assert(label(root,"2"));
    swipe(160,474,160,370); assert(label(root,"最近应用"));
    swipe(160,280,160,170);
    assert(ouse_app_get_stats(&state)==ESP_OK && state.live_instances==0 && state.preview_bytes==0);
    assert(label(root,"暂无最近应用"));
    swipe(4,240,84,240); assert(!label(root,"最近应用"));
    click(root,LV_SYMBOL_SETTINGS); click(root,"系统信息");
    swipe(4,240,84,240); assert(label(root,"屏幕亮度"));
    swipe(316,240,236,240);
    click(root,LV_SYMBOL_PLUS); assert(label(root,"0"));
    swipe(4,240,24,240); assert(label(root,"0")); // below threshold must not navigate
    swipe(160,474,160,370);
    lv_obj_t *recent=lv_obj_get_parent(label(root,"最近应用")), *strip=NULL;
    for (unsigned i=0;i<lv_obj_get_child_count(recent);++i) {
        lv_obj_t *candidate=lv_obj_get_child(recent,i);
        if (lv_obj_has_flag(candidate,LV_OBJ_FLAG_SCROLLABLE) && lv_obj_get_scroll_dir(candidate)==LV_DIR_HOR) strip=candidate;
    }
    assert(strip);
    swipe(220,240,65,240);
    assert(lv_obj_get_scroll_x(strip)>0);
    click(root,"桌面");
    assert(ouse_app_close_id("ouse.settings")==ESP_OK && ouse_app_close_id("ouse.counter")==ESP_OK);
    tick();
    assert(count(root)==objects && timers()==baseline_timers);
    assert(ouse_app_get_stats(&state)==ESP_OK && !state.live_instances && !state.preview_bytes && state.opened==state.closed);
    assert(!label(root,"OusePocket") && !label(root,"桌面已就绪") && !label(root,"0.3.0"));
    screenshot("desktop-light.ppm");
    click(root,LV_SYMBOL_PLUS); click(root,"+1"); click(root,"回到桌面");
    click(root,LV_SYMBOL_SETTINGS); click(root,"外观：浅色");
    assert(ouse_theme_get()==OUSE_THEME_DARK && saved.requested_theme==1);
    tick(); screenshot("settings-dark.ppm");
    const lv_image_dsc_t *preview=ouse_app_get_preview("ouse.counter");
    assert(preview && preview->data[0]==0xc7 && preview->data[1]==0x39);
    allow_save=false; click(root,"外观：深色"); tick();
    assert(label(root,"主题保存失败，请重试"));
    allow_save=true; click(root,"保存");
    assert(saved.requested_theme==0 && saved.requested_brightness==brightness);
    saved.pending=false; saved.saved_theme=0; saved.saved_brightness=brightness; tick();
    click(root,"外观：浅色"); tick();
    click(root,"回到桌面"); screenshot("desktop-dark.ppm");
    click(root,LV_SYMBOL_PLUS); assert(label(root,"1"));
    screenshot("counter-dark.ppm");
    click(root,"回到桌面");
    swipe(160,14,160,110); tick(); click(root,"浅色模式");
    assert(ouse_theme_get()==OUSE_THEME_LIGHT && saved.requested_theme==0);
    swipe(160,320,160,220);
    assert(ouse_app_close_id("ouse.settings")==ESP_OK && ouse_app_close_id("ouse.counter")==ESP_OK);
    tick(); assert(count(root)==objects && timers()==baseline_timers);
    puts("PASS standard icon-only desktop, shared light/dark theme, theme save request, cached app state retained");
    puts("PASS pointer gestures: both side backs, pull-down brightness, bottom recents, swipe cards, resume state, swipe-close and cleanup");
    puts("PASS app registry, single instance, counter/reset/resume, background and switching, 200 lifecycle rounds; objects/timers restored");
    assert(locks == unlocks + 2); // only the timed-out lock wasn't acquired.
    puts("PASS real LVGL 9.5: desktop/settings/navigation, brightness success+failure, save pending/failure/retry, system refresh, bounded UI dispatch, opaque background, CJK coverage, 200 round trips");
    return 0;
}
'''.replace("CHARS", ",".join(str(c) for c in chars))
(OUT / "check.c").write_text(fixture, encoding="utf-8")
project = r'''
cmake_minimum_required(VERSION 3.24)
project(ouse_core_host C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_PATH "OUT/lv_conf.h" CACHE PATH "" FORCE)
add_subdirectory("LVGL" lvgl)
target_compile_options(lvgl PRIVATE /utf-8 "$<$<COMPILE_LANGUAGE:C>:/std:c11>")
target_compile_definitions(lvgl PRIVATE _CRT_SECURE_NO_WARNINGS)
add_executable(check check.c "CORE/ouse_core.c" "CORE/ouse_font_ui_16.c" "CORE/ouse_apps.c" "CORE/ouse_shell.c" "CORE/ouse_theme.c" "CORE/ouse_navigation.cpp" "MAIN/counter_app.c")
target_include_directories(check PRIVATE "CORE/include" "CORE" "MAIN" "SYSTEM_INCLUDE" "OUT")
target_compile_options(check PRIVATE /utf-8 /UNDEBUG)
target_link_libraries(check PRIVATE lvgl)
'''.replace("OUT", OUT.as_posix()).replace("LVGL", LVGL.as_posix()).replace("CORE", CORE.as_posix()).replace("SYSTEM_INCLUDE", SYSTEM_INCLUDE.as_posix()).replace("MAIN", (ROOT/"main").as_posix())
(OUT / "CMakeLists.txt").write_text(project, encoding="utf-8")
cmd = ["D:/Espressif/tools/cmake/4.0.3/bin/cmake.exe", "-S", str(OUT), "-B", str(OUT / "build"),
       "-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=D:/Espressif/tools/ninja/1.12.1/ninja.exe",
       "-DCMAKE_C_COMPILER="+str(compiler), "-DCMAKE_CXX_COMPILER="+str(compiler),
       "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_TRY_COMPILE_CONFIGURATION=Release",
       "-DCMAKE_C_COMPILER_WORKS=TRUE", "-DCMAKE_CXX_COMPILER_WORKS=TRUE",
       "-DCMAKE_C_ABI_COMPILED=TRUE", "-DCMAKE_CXX_ABI_COMPILED=TRUE"]
# Compiler self-checks are skipped to avoid Windows PDB/try-compile hangs;
# the complete real LVGL library and test executable are still compiled below.
p = subprocess.run(cmd, env=env, capture_output=True, text=True, errors="replace", timeout=90)
if p.returncode: print(p.stdout, p.stderr)
assert p.returncode == 0
# Invoke the compiler directly: Ninja's Windows /FS/PDB transport can hang in
# constrained environments. CMake still determines the genuine LVGL source set.
from concurrent.futures import ThreadPoolExecutor
commands = json.loads((OUT / "build/compile_commands.json").read_text(encoding="utf-8"))
objects = OUT / "objects"
objects.mkdir(exist_ok=True)
def compile_file(item):
    index, entry = item
    target = objects / f"{index}.obj"
    is_app = index < 8
    args = [str(compiler), "/nologo", "/c", "/O2", "/MD", "/utf-8",
            "/D_CRT_SECURE_NO_WARNINGS", "/DLV_KCONFIG_IGNORE", "/DLV_LVGL_H_INCLUDE_SIMPLE",
            '/DLV_CONF_PATH="'+(OUT/"lv_conf.h").as_posix()+'"',
            "/I"+str(OUT), "/I"+str(LVGL), "/I"+str(CORE/"include"), "/I"+str(SYSTEM_INCLUDE), "/I"+str(CORE), "/I"+str(ROOT/"main"),
            "/Fo"+str(target), entry["file"]]
    if not is_app: args.append("/DNDEBUG")
    args += ["/std:c++17", "/EHsc"] if entry["file"].endswith(".cpp") else ["/std:c11"]
    result = subprocess.run(args, cwd=OUT, env=env, capture_output=True, text=True, errors="replace", timeout=60)
    assert result.returncode == 0, (entry["file"], result.stdout, result.stderr)
    return target
lib = OUT / "lvgl-native.lib"
library_inputs = [Path(entry["file"]) for entry in commands[8:]] + list((LVGL/"src").rglob("*.h"))
library_inputs += [OUT/"lv_conf.h", LVGL/"lvgl.h"]
reuse_library = lib.exists() and lib.stat().st_mtime >= max(p.stat().st_mtime for p in library_inputs)
selected = list(enumerate(commands[:8] if reuse_library else commands))
print(f"Compiling {len(selected)} native files; reuse verified LVGL library={reuse_library}", flush=True)
with ThreadPoolExecutor(max_workers=6) as workers:
    compiled = list(workers.map(compile_file, selected))
if not reuse_library:
    (OUT/"library.rsp").write_text('\n'.join('"'+str(p)+'"' for p in compiled[8:]), encoding="utf-8")
    result = subprocess.run([str(compiler.parent/"lib.exe"), "/nologo", "/OUT:"+str(lib), "@"+str(OUT/"library.rsp")],
                            cwd=OUT, env=env, capture_output=True, text=True, errors="replace")
    assert result.returncode == 0, (result.stdout, result.stderr)
result = subprocess.run([str(compiler), "/nologo", *[str(p) for p in compiled[:8]], str(lib),
                        "/Fe"+str(OUT/"check.exe")], cwd=OUT, env=env, capture_output=True, text=True, errors="replace")
assert result.returncode == 0, (result.stdout, result.stderr)
subprocess.run([str(OUT / "check.exe")], check=True, cwd=OUT)
subprocess.run([str(OUT / "check.exe"), "--lifecycle-errors"], check=True, cwd=OUT)
subprocess.run([str(OUT / "check.exe"), "--launcher-pages"], check=True, cwd=OUT)
from PIL import Image
for name in ("desktop", "settings", "system", "counter", "quick", "recents", "desktop-light", "desktop-dark", "settings-dark", "counter-dark", "desktop-page2"):
    with Image.open(OUT / (name+".ppm")) as image:
        image.save(OUT / (name+".png"))
print("PASS native core compiled against real LVGL; firmware/device not built or accessed")
print(f"Preview: {OUT / 'desktop.png'}")
