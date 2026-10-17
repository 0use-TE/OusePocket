"""Build and exercise Ouse Core with real LVGL on the host (no ESP firmware build).

--focused-native-ui runs only the modal/card preference checks, without the
existing lifecycle stress scenarios.
"""
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
#define LV_FONT_SOURCE_HAN_SANS_SC_14_CJK 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0
#define LV_OBJ_STYLE_CACHE 0
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_DRAW_UNIT_CNT 1
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_USE_LABEL 1
#define LV_USE_BUTTON 1
#define LV_USE_BAR 1
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1
#define LV_USE_TEXTAREA 1
#define LV_USE_KEYBOARD 1
#define LV_USE_BUTTONMATRIX 1
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
(OUT / "freertos").mkdir(exist_ok=True)
write_if_changed(OUT / "freertos/FreeRTOS.h", """#pragma once
typedef unsigned TickType_t;
""")
write_if_changed(OUT / "freertos/task.h", """#pragma once
#include "FreeRTOS.h"
void vTaskDelay(TickType_t ticks);
""")
chars = sorted({ord(c) for c in re.findall(r'[^\x00-\x7f]', "\n".join(p.read_text(encoding="utf-8") for p in [CORE/"ouse_core.c", CORE/"ouse_shell.c", ROOT/"tools/fixtures/counter_app.c"]))})
fixture = r'''
#include "ouse_core.h"
#include "ouse_debug.h"
#include "counter_app.h"
#include "ouse_apps_internal.h"
#include "ouse_shell.h"
#include "ouse_device_driver.h"
#include "ouse_dialog.h"
#include "ouse_device_ui.h"
#include "ouse_widgets.h"
#include "display_flush_wait.h"
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
static ouse_settings_state_t saved={.saved_font_percent=100,.requested_font_percent=100};
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
static void tick(void) {
    for(unsigned frame=0;frame<60;++frame) { lv_tick_inc(20); lv_timer_handler(); }
}
static void flush(lv_display_t *display, const lv_area_t *a, uint8_t *data) {
    int width = a->x2 - a->x1 + 1;
    for (int y = a->y1; y <= a->y2; ++y)
        memcpy(pixels + (y * 320 + a->x1) * 2, data + (y - a->y1) * width * 2, width * 2);
    ++flushes;
    lv_display_flush_ready(display);
}
static struct {
    lv_display_t *display;
    const uint8_t *data;
    size_t size;
    uint32_t checksum;
    unsigned submitted,completed,yields,current_yields;
    bool pending,complete_inline;
} dma;
static uint32_t dma_checksum(const uint8_t *data,size_t size) {
    uint32_t hash=2166136261u;
    for(size_t i=0;i<size;++i)hash=(hash^data[i])*16777619u;
    return hash;
}
void vTaskDelay(TickType_t ticks) {
    assert(ticks==1 && dma.pending && dma.display->flushing);
    // Read the original buffer, not a copy. Rendering into it before actual
    // completion must fail even if the next flush submission has not happened.
    assert(dma_checksum(dma.data,dma.size)==dma.checksum);
    ++dma.yields;
    if(++dma.current_yields==3) {
        dma.pending=false;++dma.completed;
        lv_display_flush_ready(dma.display);
    }
}
static void async_flush(lv_display_t *display,const lv_area_t *area,uint8_t *data) {
    assert(display==dma.display && display->flushing);
    assert(!dma.pending && dma.submitted==dma.completed);
    dma.data=data;
    dma.size=(size_t)(area->x2-area->x1+1)*(size_t)(area->y2-area->y1+1)*2;
    assert(dma.size && dma.size<=64*8*2);
    dma.checksum=dma_checksum(data,dma.size);
    dma.current_yields=0;++dma.submitted;
    if(dma.complete_inline) {
        ++dma.completed;lv_display_flush_ready(display);
    } else dma.pending=true;
}
static void test_async_flush_wait(void) {
    uint8_t drawing[64*8*2];
    lv_display_t *display=lv_display_create(64,48);
    dma.display=display;
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,drawing,NULL,sizeof(drawing),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,async_flush);
    lv_display_set_flush_wait_cb(display,ouse_display_flush_wait);
    lv_obj_t *root=lv_display_get_screen_active(display);
    lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
    for(unsigned frame=0;frame<2;++frame) {
        unsigned submitted=dma.submitted;
        lv_obj_set_style_bg_color(root,lv_color_hex(frame ? 0x1234ab : 0xef4512),0);
        lv_obj_invalidate(root);
        // Real LVGL must wait between each of the six partial-buffer chunks.
        lv_refr_now(display);
        assert(dma.submitted>=submitted+6 && dma.yields);
        // A last asynchronous transfer can remain after the refresh returns.
        ouse_display_flush_wait(display);
        assert(!dma.pending && !display->flushing && dma.completed==dma.submitted);
        assert(dma.yields==dma.completed*3);
    }
    unsigned yields=dma.yields;
    ouse_display_flush_wait(display); // Already complete: do not delay again.
    assert(dma.yields==yields);
    dma.complete_inline=true;
    unsigned submitted=dma.submitted;
    lv_obj_set_style_bg_color(root,lv_color_hex(0x65abcd),0);
    lv_obj_invalidate(root);lv_refr_now(display);
    assert(dma.submitted>=submitted+6 && dma.completed==dma.submitted);
    assert(!dma.pending && !display->flushing && dma.yields==yields);
    lv_display_delete(display);
    memset(&dma,0,sizeof(dma));
    puts("PASS real LVGL asynchronous flush: DMA buffer unchanged until three yields complete; inline completion does not wait");
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
static lv_indev_t *touch_device;
static lv_indev_state_t touch_state=LV_INDEV_STATE_RELEASED;
static void read_touch(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev; data->state=touch_state; data->point=touch_point;
}
static void touch_step(int x, int y, bool down) {
    touch_point=(lv_point_t){x,y}; touch_state=down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    // Feed the pointer before the UI timers, as an input-driven adapter does.
    // Otherwise timer list order adds a frame of stale coordinates.
    lv_tick_inc(50); lv_indev_read(touch_device); lv_timer_handler();
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
    if(lv_obj_has_flag(root,LV_OBJ_FLAG_HIDDEN))return NULL;
    if (lv_obj_check_type(root, &lv_slider_class)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        lv_obj_t *found = slider(lv_obj_get_child(root, i));
        if (found) return found;
    }
    return NULL;
}
static lv_obj_t *find_type(lv_obj_t *root,const lv_obj_class_t *type) {
    if(!lv_obj_is_visible(root))return NULL;
    if(lv_obj_check_type(root,type))return root;
    for(unsigned i=0;i<lv_obj_get_child_count(root);++i){lv_obj_t *obj=find_type(lv_obj_get_child(root,i),type);if(obj)return obj;}
    return NULL;
}
static void gap_pressed(lv_event_t *event) {
    if(lv_event_get_target_obj(event)==lv_event_get_current_target_obj(event))
        *(bool *)lv_event_get_user_data(event)=true;
}
static void scroll_from_child_gap(lv_obj_t *page,int min_y,int end_y) {
    lv_obj_update_layout(page);
    lv_area_t outer;lv_obj_get_coords(page,&outer);
    lv_point_t point={(outer.x1+outer.x2)/2,-1};
    lv_obj_t *previous=NULL;
    for(unsigned i=0;i<lv_obj_get_child_count(page);++i) {
        lv_obj_t *child=lv_obj_get_child(page,i);
        if(!lv_obj_is_visible(child))continue;
        if(previous) {
            lv_area_t a,b;lv_obj_get_coords(previous,&a);lv_obj_get_coords(child,&b);
            int y=(a.y2+b.y1)/2;
            if(b.y1-a.y2>=5 && y>=min_y && y>outer.y1 && y<outer.y2) {point.y=y;break;}
        }
        previous=child;
    }
    assert(point.y>=0 && point.y>end_y+20);
    for(unsigned i=0;i<lv_obj_get_child_count(page);++i) {
        lv_obj_t *child=lv_obj_get_child(page,i);if(!lv_obj_is_visible(child))continue;
        lv_area_t area;lv_obj_get_coords(child,&area);
        assert(!lv_area_is_point_on(&area,&point,0));
    }
    int before=lv_obj_get_scroll_y(page);
    bool pressed=false;lv_obj_add_event_cb(page,gap_pressed,LV_EVENT_PRESSED,&pressed);
    touch_step(point.x,point.y,true);
    // Verify the empty gap really hit this scrolling surface, not a button.
    assert(pressed);lv_obj_remove_event_cb(page,gap_pressed);
    for(unsigned i=1;i<=6;++i)touch_step(point.x,point.y+(end_y-point.y)*(int)i/6,true);
    touch_step(point.x,end_y,false);tick();
    int after=lv_obj_get_scroll_y(page);
    printf("Native blank-gap pointer scroll: origin=(%d,%d) before=%d after=%d\n",point.x,point.y,before,after);fflush(stdout);
    assert(after>before+10);
}
static unsigned count(lv_obj_t *root) {
    unsigned result = 1;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) result += count(lv_obj_get_child(root, i));
    return result;
}
static void click(lv_obj_t *root, const char *value) {
    lv_obj_t *target = label(root, value);
    if(!target && !strcmp(value,"返回设置"))target=label(root,LV_SYMBOL_LEFT);
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
    assert(drawn>300); // compact launcher glyphs must still visibly render
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
static lv_obj_t *recent_title(lv_obj_t *obj,const char *text) {
    if(!lv_obj_is_visible(obj))return NULL;
    if(lv_obj_check_type(obj,&lv_label_class) && !strcmp(lv_label_get_text(obj),text)) {
        lv_obj_t *parent=lv_obj_get_parent(obj);
        if(parent && lv_obj_get_width(parent)==224 && lv_obj_get_height(parent)==310)return obj;
    }
    for(unsigned i=0;i<lv_obj_get_child_count(obj);++i) {
        lv_obj_t *found=recent_title(lv_obj_get_child(obj,i),text);if(found)return found;
    }
    return NULL;
}
static bool allow_ui_save=true;
static unsigned ui_save_calls;
static esp_err_t audit_save_ui(void *context,uint8_t locale,uint8_t percent) {
    (void)context;++ui_save_calls;
    if(!allow_ui_save)return ESP_FAIL;
    saved.pending=true;saved.requested_locale=locale;saved.requested_font_percent=percent;
    return ESP_OK;
}
static void focused_native_ui(lv_obj_t *root) {
    tick();unsigned baseline=count(root),baseline_timers=timers(),baseline_bindings=ouse_ui_binding_count();
    allow_ui_save=false;
    assert(ouse_core_set_ui(1,130)==ESP_FAIL && ouse_ui_locale()==1 && ouse_ui_font_percent()==130);
    assert(ouse_dialog_active() && label(root,"Retry"));
    click(root,"Retry");assert(ouse_dialog_active() && ui_save_calls==2);
    allow_ui_save=true;click(root,"Retry");assert(!ouse_dialog_active() && ui_save_calls==3);
    assert(saved.requested_locale==1 && saved.requested_font_percent==130);
    assert(ouse_core_set_ui(0,100)==ESP_OK);saved.pending=false;saved.last_error=ESP_OK;tick();
    puts("PASS focused UI preferences: live locale/font survives save failure, failed Retry remains available, successful Retry submits both and dismisses");
    assert(ouse_app_open("ouse.settings")==ESP_OK);click(root,"电源");
    ouse_power_state_t power={.available=true,.valid=true,.present=true,.external=true,.percent=73,.millivolts=4030};
    ouse_devices_publish_power(&power);tick();
    assert(label(root,"自动熄屏") && label(root,"从不") && !label(root,"15 秒"));
    screenshot("native-power-compact.ppm");
    lv_obj_t *sleep_caption=label(root,"立即熄屏");assert(sleep_caption && lv_obj_get_width(sleep_caption)>=64);
    click(root,"自动熄屏");tick();assert(ouse_dialog_choice_active());
    assert(label(root,"从不") && label(root,"15 秒") && label(root,"30 秒") && label(root,"1 分钟") && label(root,"2 分钟") && label(root,"5 分钟"));
    lv_obj_t *cancel_caption=label(root,"取消");assert(cancel_caption && lv_obj_get_width(cancel_caption)>=32);
    screenshot("native-timeout-choices.ppm");
    click(root,"取消");assert(!ouse_dialog_active());ouse_power_get(&power);assert(power.sleep_seconds==0);
    click(root,"自动熄屏");click(root,"15 秒");assert(!ouse_dialog_active());
    ouse_power_get(&power);assert(power.sleep_seconds==15);assert(ouse_power_set_timeout(0)==ESP_OK);
    click(root,"自动熄屏");assert(ouse_app_background()==ESP_OK && !ouse_dialog_active());
    assert(ouse_app_open("ouse.settings")==ESP_OK);click(root,"返回设置");click(root,"声音");tick();
    assert(!label(root,"播放测试音") && !label(root,"录音 3 秒") && !label(root,"回放录音") && !label(root,"停止"));
    lv_obj_t *mute=find_type(root,&lv_switch_class);assert(mute);
    lv_area_t mute_area;lv_obj_get_coords(mute,&mute_area);int mx=(mute_area.x1+mute_area.x2)/2,my=(mute_area.y1+mute_area.y2)/2;
    ouse_audio_state_t audio;ouse_audio_get(&audio);bool muted_before=audio.muted;
    touch_step(mx,my,true);touch_step(mx,my,false);tick();ouse_audio_get(&audio);assert(audio.muted!=muted_before);
    touch_step(mx,my,true);touch_step(mx,my,false);tick();ouse_audio_get(&audio);assert(audio.muted==muted_before);
    screenshot("native-sound-compact.ppm");
    click(root,"返回设置");click(root,"外观");tick();screenshot("native-appearance-light.ppm");
    lv_obj_t *theme_switch=find_type(root,&lv_switch_class);assert(theme_switch);
    lv_area_t theme_area;lv_obj_get_coords(theme_switch,&theme_area);int tx=(theme_area.x1+theme_area.x2)/2,ty=(theme_area.y1+theme_area.y2)/2;
    touch_step(tx,ty,true);touch_step(tx,ty,false);tick();assert(ouse_theme_get()==OUSE_THEME_DARK && lv_obj_has_state(theme_switch,LV_STATE_CHECKED));
    screenshot("native-appearance-dark.ppm");
    touch_step(tx,ty,true);touch_step(tx,ty,false);tick();assert(ouse_theme_get()==OUSE_THEME_LIGHT && !lv_obj_has_state(theme_switch,LV_STATE_CHECKED));
    click(root,"返回设置");click(root,"Wi-Fi");
    assert(ouse_wifi_set_enabled(true)==ESP_OK);ouse_wifi_request_t request;assert(ouse_devices_take_wifi(&request));
    ouse_wifi_state_t wifi={.available=true,.enabled=true,.ready=true,.count=1,.completed_id=request.id};
    strcpy(wifi.aps[0].ssid,"Office");wifi.aps[0].rssi=-42;wifi.aps[0].secured=true;
    ouse_devices_publish_wifi(&wifi,request.id);tick();click(root,"Office");tick();
    lv_obj_t *password=find_type(root,&lv_textarea_class);assert(password);
    lv_obj_t *keyboard=find_type(root,&lv_keyboard_class);assert(keyboard);
    lv_textarea_set_text(password,"bad");lv_obj_send_event(keyboard,LV_EVENT_READY,NULL);tick();
    assert(ouse_dialog_active() && ouse_device_ui_modal() && !ouse_devices_take_wifi(&request));
    swipe(4,240,84,240);assert(!ouse_dialog_active() && ouse_device_ui_modal());
    swipe(316,240,236,240);assert(!ouse_device_ui_modal() && !*lv_textarea_get_text(password));
    click(root,"Office");lv_textarea_set_text(password,"12345678");
    assert(ouse_core_set_ui(1,130)==ESP_OK);tick();
    assert(ouse_device_ui_modal() && !strcmp(lv_textarea_get_text(password),"12345678"));
    assert(label(root,"Connect") && label(root,"Cancel"));
    lv_obj_send_event(keyboard,LV_EVENT_CANCEL,NULL);tick();
    assert(!ouse_device_ui_modal() && !*lv_textarea_get_text(password));
    click(root,"Other network");tick();assert(ouse_device_ui_modal());
    lv_obj_t *ssid=find_type(root,&lv_textarea_class);assert(ssid && ssid!=password);
    assert(lv_keyboard_get_textarea(keyboard)==ssid);
    lv_textarea_set_text(ssid,"HiddenNetwork");lv_textarea_set_text(password,"abcdefgh");
    screenshot("wifi-modal-en-large.ppm");
    lv_obj_send_event(keyboard,LV_EVENT_READY,NULL);tick();
    assert(!ouse_device_ui_modal() && ouse_devices_take_wifi(&request));
    assert(!strcmp(request.ssid,"HiddenNetwork") && !strcmp(request.password,"abcdefgh"));
    assert(ouse_core_set_ui(0,100)==ESP_OK);assert(ouse_app_close()==ESP_OK);tick();
    assert(count(root)==baseline && timers()==baseline_timers && ouse_ui_binding_count()==baseline_bindings);
    puts("PASS focused native settings: compact power/sound/appearance, timeout single-entry/select/cancel/pause cleanup, one switch toggle per touch; Wi-Fi keyboard ready/cancel/password/modal back/language/font/manual SSID and complete cleanup");

    assert(ouse_app_open("ouse.counter")==ESP_OK);tick();
    touch_step(160,474,true);touch_step(160,370,true);
    for(unsigned i=0;i<8;++i)touch_step(160,370,true);
    touch_step(160,370,false);tick();
    ouse_shell_state_t shell;ouse_shell_get_state(&shell);assert(shell.recents && shell.snapshot_bytes);
    lv_obj_t *title=recent_title(root,"计数器");assert(title);
    int before=lv_obj_get_height(title);
    swipe(160,14,160,200);ouse_shell_get_state(&shell);assert(shell.quick && shell.recents);
    click(root,"字号：标准");tick();click(root,"字号：较大");tick();
    assert(ouse_ui_font_percent()==130);
    swipe(160,462,160,350);ouse_shell_get_state(&shell);assert(!shell.quick && shell.recents && shell.snapshot_bytes);
    title=recent_title(root,"计数器");assert(title);
    int after=lv_obj_get_height(title),line=lv_obj_get_style_text_font(title,0)->line_height;
    screenshot("recents-font130.ppm");
    printf("Recent title after quick-font130: before=%d after=%d required_line_height=%d\n",before,after,line);fflush(stdout);
    assert(after>=line);
    swipe(160,14,160,200);click(root,"简体中文");tick();
    swipe(160,462,160,350);assert(ouse_ui_locale()==1);
    title=recent_title(root,"Counter");assert(title && lv_obj_get_height(title)>=lv_obj_get_style_text_font(title,0)->line_height);
    assert(ouse_core_set_ui(0,100)==ESP_OK);tick();
    title=recent_title(root,"计数器");assert(title && lv_obj_get_height(title)==ouse_ui_font(14)->line_height);
    click(root,"全部清除");tick();
    ouse_app_stats_t state;assert(ouse_app_get_stats(&state)==ESP_OK && !state.live_instances && !state.preview_bytes);
    ouse_shell_get_state(&shell);assert(!shell.recents && !shell.snapshot_bytes);
    assert(count(root)==baseline && timers()==baseline_timers && ouse_ui_binding_count()==baseline_bindings);
    puts("PASS focused recent cards: title height follows quick font/locale changes, historical preview retained, Clear all releases images/instances/bindings/timers");
}
static uint16_t rgb565(uint32_t rgb) {return (uint16_t)(((rgb>>19)&31)<<11 | ((rgb>>10)&63)<<5 | ((rgb>>3)&31));}
static uint16_t pixel(int x,int y) {assert(x>=0 && x<320 && y>=0 && y<480);return pixels[(y*320+x)*2] | (uint16_t)pixels[(y*320+x)*2+1]<<8;}
static unsigned switch_events;
static void switch_changed(lv_event_t *event) {(void)event;++switch_events;}
static void focused_widgets(lv_obj_t *root) {
    const unsigned densities[]={160,240,320},fonts[]={100,130};
    for(unsigned density=0;density<3;++density)for(unsigned font=0;font<2;++font)for(unsigned dark=0;dark<2;++dark) {
        assert(ouse_ui_init(densities[density],0,fonts[font])==ESP_OK);
        ouse_theme_set(dark ? OUSE_THEME_DARK : OUSE_THEME_LIGHT);
        lv_obj_t *panel=ouse_widget_box(root,OUSE_STYLE_APP,0);lv_obj_set_size(panel,320,480);
        lv_obj_t *sw=ouse_widget_switch(panel);lv_obj_set_pos(sw,ouse_ui_dp(24),ouse_ui_dp(64));
        lv_obj_add_event_cb(sw,switch_changed,LV_EVENT_VALUE_CHANGED,NULL);tick();
        lv_area_t area;lv_obj_get_coords(sw,&area);
        int y=(area.y1+area.y2)/2,left=area.x1+ouse_ui_dp(14),right=area.x1+ouse_ui_dp(34);
        uint16_t background=rgb565(ouse_theme_colors().background),track=rgb565(dark ? 0x364354 : 0xdddddd);
        if(!density && !font && !dark)screenshot("widgets-switch-current.ppm");
        printf("Switch dpi=%u font=%u dark=%u off knob=%04x corner=%04x required=ffff/%04x\n",densities[density],fonts[font],dark,pixel(left,y),pixel(area.x1+1,area.y1+1),background);fflush(stdout);
        assert(pixel(left,y)==0xffff && pixel(right,y)==track);
        assert(pixel(area.x1+1,area.y1+1)==background);
        unsigned before=switch_events;
        // The visible track is 28dp, but a 6dp extension makes the contact
        // height 40dp. A real press in that extension must toggle exactly once.
        touch_step((area.x1+area.x2)/2,area.y1-ouse_ui_dp(4),true);
        touch_step((area.x1+area.x2)/2,area.y1-ouse_ui_dp(4),false);tick();
        assert(switch_events==before+1 && lv_obj_has_state(sw,LV_STATE_CHECKED));
        assert(pixel(right,y)==0xffff && pixel(left,y)==rgb565(ouse_theme_colors().accent));
        assert(pixel(area.x1+1,area.y1+1)==background);
        if(density==0 && font==0)screenshot(dark ? "widgets-switch-dark.ppm" : "widgets-switch-light.ppm");
        if(density==2 && font==1)screenshot(dark ? "widgets-switch-dark-hdpi.ppm" : "widgets-switch-light-hdpi.ppm");
        touch_step((area.x1+area.x2)/2,y,true);touch_step((area.x1+area.x2)/2,y,false);tick();
        assert(switch_events==before+2 && !lv_obj_has_state(sw,LV_STATE_CHECKED));
        lv_obj_t *button=ouse_widget_button(panel,"确定",NULL,NULL);lv_obj_set_pos(button,8,8);
        lv_obj_t *single=ouse_widget_row(panel,NULL,"静音",NULL,NULL,NULL,NULL);lv_obj_set_y(single,ouse_ui_dp(110));
        lv_obj_t *dual=ouse_widget_row(panel,NULL,"音量","test",NULL,NULL,NULL);lv_obj_set_y(dual,ouse_ui_dp(170));tick();
        assert(lv_obj_get_width(button)<lv_obj_get_width(panel) && lv_obj_get_height(button)>=ouse_ui_dp(40));
        lv_obj_t *caption=lv_obj_get_child(button,0);lv_point_t text_size;
        lv_text_get_size(&text_size,lv_label_get_text(caption),lv_obj_get_style_text_font(caption,0),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
        assert(lv_obj_get_width(caption)>=text_size.x && lv_obj_get_width(button)>=lv_obj_get_width(caption)+ouse_ui_dp(28));
        assert(lv_obj_get_style_radius(button,0)==ouse_ui_dp(12));
        assert(lv_obj_get_style_min_height(single,0)==ouse_ui_dp(52) && lv_obj_get_style_min_height(dual,0)==ouse_ui_dp(64));
        assert(lv_obj_get_style_radius(single,0)==ouse_ui_dp(12));
        lv_obj_delete(panel);tick();
    }
    puts("PASS widget pixels: light/dark, checked/off, 160/240/320dpi, 100/130% text; opaque round knob/track, 40dp contact, one toggle, content buttons and compact rows");
}
int main(int argc, char **argv) {
    (void)argv;
    lv_init();
    test_async_flush_wait();
    ouse_device_port_t device_port={0};ouse_device_config_t device_config={.volume=40};
    assert(ouse_devices_init(&device_port,&device_config)==ESP_OK);
    ouse_devices_set_available(1,true,ESP_OK);ouse_devices_set_available(2,true,ESP_OK);
    lv_display_t *display = lv_display_create(320, 480);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, flush);
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_indev_t *touch = lv_indev_create();
    touch_device=touch;
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, read_touch);
    if(argc>1 && !strcmp(argv[1],"--focused-widgets")) {focused_widgets(lv_screen_active());return 0;}
    char test_name[]="Test";
    ouse_app_definition_t definitions[OUSE_APP_CAPACITY-1] = {ouse_counter_app,
        {.id="ouse.fail", .name=test_name, .icon=LV_SYMBOL_WARNING, .open=fail_open, .close=fail_close}};
    char extra_ids[OUSE_APP_CAPACITY-3][48],extra_names[OUSE_APP_CAPACITY-3][48];
    for (unsigned i=2;i<OUSE_APP_CAPACITY-1;++i) {
        snprintf(extra_ids[i-2],48,"ouse.extra%u",i);snprintf(extra_names[i-2],48,"Extra%u",i);
        definitions[i]=definitions[1]; definitions[i].id=extra_ids[i-2]; definitions[i].name=extra_names[i-2];
    }
    bool pages=argc>1 && !strcmp(argv[1],"--launcher-pages");
    ouse_core_config_t config = {.port = {
        .display = display, .touch = touch, .lock = lock, .unlock = unlock,
        .set_brightness = set_brightness, .save_brightness = save_setting, .save_theme = save_theme,
        .get_settings_state = settings_state, .get_system_info = system_info, .board_name = "Waveshare P4 LCD 3.5"}, .initial_brightness = 45, .initial_font_percent=pages ? 130 : 100, .apps = definitions, .app_count = (pages ? OUSE_APP_CAPACITY-1 : argc>1 ? 2 : 1)};
    if(argc>1 && !strcmp(argv[1],"--focused-native-ui"))config.port.save_ui=audit_save_ui;
    assert(!ouse_core_is_started() && strcmp(ouse_core_version(), "CORE_VERSION_EXPECTED") == 0);
    assert(ouse_core_sleep_display()==ESP_ERR_INVALID_STATE && ouse_core_wake_display()==ESP_ERR_INVALID_STATE);
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
    if(argc>1 && !strcmp(argv[1],"--focused-native-ui")) {focused_native_ui(root);return 0;}
    assert(label(root, LV_SYMBOL_SETTINGS) && !label(root, "屏幕亮度"));
    if (pages) {
        tick();
        ouse_app_stats_t state; assert(ouse_app_get_stats(&state)==ESP_OK && state.registered==OUSE_APP_CAPACITY);
        ouse_app_info_t last;assert(ouse_app_get_info(OUSE_APP_CAPACITY-1,&last)==ESP_OK && !strcmp(last.id,extra_ids[OUSE_APP_CAPACITY-4]));
        ouse_app_definition_t overflow=definitions[1];overflow.id="user.overflow";
        assert(ouse_apps_user_apply(&overflow)==ESP_ERR_NO_MEM);
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
        puts("PASS full registry, overflow rejection and horizontal launcher paging");
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
        assert(ouse_app_open("ouse.settings")==ESP_OK && label(root,"显示与个性化"));
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
    unsigned objects=count(root),baseline_timers=timers(),baseline_bindings=ouse_ui_binding_count();
    ouse_shell_state_t shell;ouse_app_stats_t state;
    click(root,LV_SYMBOL_SETTINGS);tick();
    assert(label(root,"显示与个性化") && label(root,"Waveshare P4 LCD 3.5"));screenshot("settings.ppm");
    // A compact page still owns drags starting between its child rows.
    lv_obj_t *settings_page=lv_obj_get_parent(label(root,"显示与个性化"));
    scroll_from_child_gap(settings_page,140,55);
    screenshot("settings-gap-scroll.ppm");
    // Reset only after verifying real indev scrolling, for the independent
    // settings/control scenarios below.
    lv_obj_scroll_to_y(settings_page,0,LV_ANIM_OFF);tick();
    puts("PASS native settings scroll from blank gap using real pointer input");
    click(root,"显示");tick();assert(label(root,"屏幕亮度"));
    lv_obj_t *control=slider(root);assert(control);
    lv_slider_set_value(control,75,LV_ANIM_OFF);lv_obj_send_event(control,LV_EVENT_VALUE_CHANGED,NULL);
    assert(brightness==75 && label(root,"75%") && !saved_requests);
    lv_obj_send_event(control,LV_EVENT_RELEASED,NULL);tick();assert(saved_requests==1 && !label(root,"保存") && !label(root,"正在保存设置"));
    saved.pending=false;saved.last_error=ESP_FAIL;tick();assert(label(root,"设置未能保存，请重试"));screenshot("save-error.ppm");
    allow_save=false;click(root,"重试");assert(label(root,"设置未能保存，请重试"));
    allow_save=true;click(root,"重试");saved.pending=false;saved.last_error=ESP_OK;saved.saved_brightness=75;
    tick();assert(!ouse_dialog_active() && !label(root,"已保存，重启保留"));screenshot("display.ppm");
    allow_brightness=false;lv_slider_set_value(control,90,LV_ANIM_OFF);lv_obj_send_event(control,LV_EVENT_VALUE_CHANGED,NULL);
    assert(brightness==75 && lv_slider_get_value(control)==75 && label(root,"亮度设置失败，请重试"));allow_brightness=true;click(root,"确定");
    click(root,"返回设置");click(root,"系统信息");tick();screenshot("system.ppm");
    allow_info=false;tick();assert(label(root,"系统信息读取失败"));allow_info=true;tick();
    click(root,"返回设置");click(root,"外观");click(root,"深色模式");tick();
    assert(ouse_theme_get()==OUSE_THEME_DARK && !label(root,"已开启"));screenshot("settings-dark.ppm");
    lv_obj_t *switch_row=lv_obj_get_parent(lv_obj_get_parent(label(root,"深色模式")));
    lv_obj_t *theme_switch=lv_obj_get_child(switch_row,-1);
    assert(lv_obj_check_type(theme_switch,&lv_switch_class));
    lv_area_t switch_area;lv_obj_get_coords(theme_switch,&switch_area);
    int switch_x=(switch_area.x1+switch_area.x2)/2,switch_y=(switch_area.y1+switch_area.y2)/2;
    touch_step(switch_x,switch_y,true);touch_step(switch_x,switch_y,false);tick();
    assert(ouse_theme_get()==OUSE_THEME_LIGHT && !lv_obj_has_state(theme_switch,LV_STATE_CHECKED));
    touch_step(switch_x,switch_y,true);touch_step(switch_x,switch_y,false);tick();
    assert(ouse_theme_get()==OUSE_THEME_DARK && lv_obj_has_state(theme_switch,LV_STATE_CHECKED));
    allow_save=false;click(root,"深色模式");assert(label(root,"设置未能保存，请重试"));
    allow_save=true;click(root,"重试");saved.pending=false;saved.last_error=ESP_OK;saved.saved_theme=0;tick();
    click(root,"返回设置");click(root,"语言与文字");click(root,"English");tick();
    assert(label(root,"Language & text") && !label(root,"语言与文字"));click(root,"Largest");tick();
    assert(ouse_ui_locale()==1 && ouse_ui_font_percent()==130);screenshot("settings-text-large.ppm");
    assert(ouse_core_set_ui(0,100)==ESP_OK);(void)ouse_app_background();assert(ouse_app_close_id("ouse.settings")==ESP_OK);tick();
    assert(count(root)==objects && timers()==baseline_timers && ouse_ui_binding_count()==baseline_bindings);
    // Real native device pages. Hardware completions are injected at the worker boundary.
    assert(ouse_app_open("ouse.settings")==ESP_OK);click(root,"电源");
    ouse_power_state_t power={.available=true,.valid=true,.present=true,.external=true,.charging=true,.percent=73,.millivolts=4030};
    ouse_devices_publish_power(&power);tick();screenshot("power.ppm");
    assert(!label(root,"保存") && label(root,"自动熄屏"));
    click(root,"返回设置");click(root,"声音");tick();screenshot("audio.ppm");
    lv_obj_t *audio_slider=slider(root);assert(audio_slider);
    lv_slider_set_value(audio_slider,65,LV_ANIM_OFF);lv_obj_send_event(audio_slider,LV_EVENT_VALUE_CHANGED,NULL);
    ouse_audio_state_t audio;assert(ouse_audio_get(&audio)==ESP_OK && audio.volume==65);
    assert(!label(root,"播放测试音") && !label(root,"录音 3 秒") && !label(root,"回放录音") && !label(root,"停止"));
    click(root,"返回设置");click(root,"Wi-Fi");assert(ouse_wifi_set_enabled(true)==ESP_OK);
    ouse_wifi_request_t wifi_request;assert(ouse_devices_take_wifi(&wifi_request));
    ouse_wifi_state_t wifi={.available=true,.enabled=true,.ready=true,.count=2,.completed_id=wifi_request.id};
    strcpy(wifi.aps[0].ssid,"Office");wifi.aps[0].rssi=-42;wifi.aps[0].secured=true;
    strcpy(wifi.aps[1].ssid,"Guest");wifi.aps[1].rssi=-65;
    ouse_devices_publish_wifi(&wifi,wifi_request.id);tick();screenshot("wifi.ppm");
    click(root,"Office");tick();assert(ouse_device_ui_modal());screenshot("wifi-password.ppm");
    lv_obj_t *password=find_type(root,&lv_textarea_class);assert(password);
    // Force a smaller form viewport, as when content grows above a keyboard.
    // Its real padding/gap must still accept pointer scrolling independently
    // of the text fields; only test geometry changes, never interaction flags.
    lv_obj_t *wifi_form=lv_obj_get_parent(password);
    int form_max=lv_obj_get_style_max_height(wifi_form,LV_PART_MAIN);
    lv_obj_set_style_max_height(wifi_form,120,0);tick();
    scroll_from_child_gap(wifi_form,100,60);screenshot("wifi-form-gap-scroll.ppm");
    lv_obj_set_style_max_height(wifi_form,form_max,0);lv_obj_scroll_to_y(wifi_form,0,LV_ANIM_OFF);tick();
    lv_textarea_set_text(password,"bad");click(root,"连接");assert(ouse_dialog_active() && ouse_device_ui_modal());
    click(root,"确定");lv_textarea_set_text(password,"abcdefgh");click(root,"连接");
    assert(!ouse_device_ui_modal() && ouse_devices_take_wifi(&wifi_request) && !strcmp(wifi_request.ssid,"Office") && !strcmp(wifi_request.password,"abcdefgh"));
    // Closing the app while an input popup exists frees the keyboard and bindings.
    click(root,"Office");assert(ouse_device_ui_modal());assert(ouse_app_close()==ESP_OK);tick();assert(!ouse_device_ui_modal());
    assert(ouse_wifi_set_enabled(false)==ESP_OK);assert(ouse_devices_take_wifi(&wifi_request));
    assert(ouse_app_open("ouse.counter")==ESP_OK);click(root,"+1");tick();
    assert(ouse_power_set_timeout(15)==ESP_OK);
    for(unsigned i=0;i<13;++i)tick();ouse_power_get(&power);assert(power.asleep && !brightness);
    lv_area_t plus;lv_obj_get_coords(label(root,"+1"),&plus);int px=(plus.x1+plus.x2)/2,py=(plus.y1+plus.y2)/2;
    touch_step(px,py,true);touch_step(px,py,false);tick();ouse_power_get(&power);
    assert(!power.asleep && brightness==75 && label(root,"1")); // waking gesture consumed
    touch_step(px,py,true);touch_step(px,py,false);tick();assert(label(root,"2"));
    assert(ouse_power_set_timeout(0)==ESP_OK && ouse_app_close()==ESP_OK);tick();
    assert(count(root)==objects && timers()==baseline_timers && ouse_ui_binding_count()==baseline_bindings);
    puts("PASS device pages, password form/validation/cleanup, auto sleep, brightness restore, complete waking contact consumed");
    for(unsigned i=0;i<200;++i) {
        assert(ouse_app_open("ouse.counter")==ESP_OK);click(root,"+1");
        assert(ouse_app_open("ouse.settings")==ESP_OK);click(root,"显示");
        assert(ouse_app_open("ouse.counter")==ESP_OK && label(root,"1"));
        (void)ouse_app_background();assert(ouse_app_close_id("ouse.counter")==ESP_OK && ouse_app_close_id("ouse.settings")==ESP_OK);
        tick();assert(count(root)==objects && timers()==baseline_timers && ouse_ui_binding_count()==baseline_bindings);
    }
    click(root,LV_SYMBOL_PLUS);click(root,"+1");
    // Back feedback follows the pointer, and a short drag cancels.
    touch_step(4,240,true);touch_step(24,240,true);ouse_shell_get_state(&shell);
    assert(shell.tracking && shell.back_progress>0);screenshot("back-follow.ppm");
    touch_step(24,240,false);tick();assert(label(root,"1"));
    swipe(4,240,84,240);assert(ouse_app_get_stats(&state)==ESP_OK && !state.active_id[0]);
    click(root,LV_SYMBOL_PLUS);swipe(316,240,236,240);assert(ouse_app_get_stats(&state)==ESP_OK && !state.active_id[0]);
    // Pull down exposes an intermediate sheet position before release.
    touch_step(160,14,true);touch_step(160,180,true);ouse_shell_get_state(&shell);
    assert(shell.quick && shell.tracking && shell.quick_y<0 && shell.quick_y>-480);screenshot("quick-follow.ppm");
    touch_step(160,180,false);tick();ouse_shell_get_state(&shell);assert(shell.quick && shell.quick_y==0);
    lv_obj_t *quick_body=lv_obj_get_parent(lv_obj_get_parent(label(root,"屏幕亮度")));
    scroll_from_child_gap(quick_body,100,90);screenshot("quick-gap-scroll.ppm");
    lv_obj_scroll_to_y(quick_body,0,LV_ANIM_OFF);tick();
    lv_obj_t *quick_slider=slider(root);assert(quick_slider);
    lv_slider_set_value(quick_slider,55,LV_ANIM_OFF);lv_obj_send_event(quick_slider,LV_EVENT_VALUE_CHANGED,NULL);
    lv_obj_send_event(quick_slider,LV_EVENT_RELEASED,NULL);assert(brightness==55 && saved.requested_brightness==55);
    saved.pending=false;saved.saved_brightness=55;saved.last_error=ESP_OK;
    tick();screenshot("quick.ppm");
    unsigned writes=saved_requests;
    touch_step(130,258,true);touch_step(230,258,true);ouse_shell_get_state(&shell);
    assert(shell.quick_y==0 && !shell.tracking && brightness!=55 && saved_requests==writes);
    touch_step(230,258,false);tick();assert(saved_requests==writes+1);
    swipe(160,462,160,350);ouse_shell_get_state(&shell);assert(!shell.quick);
    // A short pull cancels; it must not leave an overlay that eats app input.
    swipe(160,14,160,42);ouse_shell_get_state(&shell);assert(!shell.quick);
    click(root,LV_SYMBOL_PLUS);assert(label(root,"1"));
    touch_step(160,474,true);touch_step(160,400,true);ouse_shell_get_state(&shell);
    assert(shell.tracking && shell.snapshot_bytes>0);screenshot("home-follow.ppm");
    touch_step(160,400,false);tick();ouse_shell_get_state(&shell);
    assert(!shell.recents && !shell.tracking && !shell.snapshot_bytes);
    assert(ouse_app_get_stats(&state)==ESP_OK && !state.active_id[0] && state.live_instances==1);
    click(root,LV_SYMBOL_PLUS);assert(label(root,"1"));swipe(160,474,160,450);assert(label(root,"1"));
    // Hold after the upward motion, then release without clicking a card.
    touch_step(160,474,true);touch_step(160,370,true);
    for(unsigned i=0;i<8;++i)touch_step(160,370,true);
    ouse_shell_get_state(&shell);assert(shell.recents && shell.cards==1);screenshot("recents.ppm");
    touch_step(160,370,false);tick();ouse_shell_get_state(&shell);assert(shell.recents);
    assert(!label(root,"最近应用") && label(root,"全部清除"));
    touch_step(160,200,true);touch_step(160,200,false);tick();assert(label(root,"1"));
    touch_step(160,474,true);touch_step(160,370,true);for(unsigned i=0;i<8;++i)touch_step(160,370,true);
    touch_step(160,370,false);tick();swipe(160,240,160,214);
    ouse_shell_get_state(&shell);assert(shell.recents && shell.cards==1); // short close rebounds
    swipe(160,240,160,120);
    ouse_shell_get_state(&shell);assert(shell.recents && !shell.cards && !shell.snapshot_bytes && label(root,"暂无最近应用"));
    touch_step(160,400,true);touch_step(160,400,false);tick();ouse_shell_get_state(&shell);assert(!shell.recents);
    assert(ouse_app_get_stats(&state)==ESP_OK && !state.live_instances && !state.preview_bytes);
    // Two cards must scroll horizontally; Clear all closes instances, not apps.
    assert(ouse_app_open("ouse.settings")==ESP_OK);assert(ouse_app_open("ouse.counter")==ESP_OK);
    touch_step(160,474,true);touch_step(160,370,true);for(unsigned i=0;i<8;++i)touch_step(160,370,true);
    touch_step(160,370,false);tick();swipe(220,200,70,200);
    ouse_shell_get_state(&shell);assert(shell.recents && shell.cards==2 && shell.recent_scroll_x==244);
    screenshot("recents-second.ppm");
    click(root,"全部清除");tick();ouse_shell_get_state(&shell);
    assert(!shell.recents && !shell.cards && !shell.snapshot_bytes);
    assert(ouse_app_get_stats(&state)==ESP_OK && !state.live_instances && state.registered==2 && !state.preview_bytes);
    assert(ouse_app_open("ouse.settings")==ESP_OK);click(root,"显示");
    swipe(4,240,84,240);assert(label(root,"显示与个性化"));swipe(316,240,236,240);
    assert(ouse_app_close_id("ouse.settings")==ESP_OK);tick();
    assert(count(root)==objects && timers()==baseline_timers && ouse_ui_binding_count()==baseline_bindings);
    // Complete animations and snapshots repeatedly, including interrupted pulls.
    for(unsigned i=0;i<30;++i) {
        swipe(160,14,160,160);swipe(160,462,160,350);
        assert(ouse_app_open("ouse.counter")==ESP_OK);swipe(160,474,160,370);
        assert(ouse_app_close_id("ouse.counter")==ESP_OK);tick();
        ouse_shell_get_state(&shell);assert(!shell.quick && !shell.recents && !shell.snapshot_bytes);
        assert(count(root)==objects && timers()==baseline_timers && ouse_ui_binding_count()==baseline_bindings);
    }
    assert(ouse_core_set_theme(OUSE_THEME_DARK)==ESP_OK);tick();screenshot("desktop-dark.ppm");
    swipe(160,14,160,160);screenshot("quick-dark.ppm");
    assert(ouse_core_set_ui(1,130)==ESP_OK);tick();screenshot("quick-en-large.ppm");
    swipe(160,462,160,350);assert(ouse_core_set_ui(0,100)==ESP_OK && ouse_core_set_theme(OUSE_THEME_LIGHT)==ESP_OK);
    tick();screenshot("desktop-light.ppm");
    assert(ouse_app_get_stats(&state)==ESP_OK && !state.live_instances && state.opened==state.closed);
    // The exported device input must traverse the same LVGL device and shell,
    // even with no fresh hardware reads while holding a stationary finger.
    assert(ouse_debug_touch(-1,240,true)==ESP_ERR_INVALID_ARG);
    assert(ouse_debug_touch(320,240,true)==ESP_ERR_INVALID_ARG);
    assert(ouse_debug_open("ouse.counter")==ESP_OK);
    assert(ouse_debug_touch(160,474,true)==ESP_OK);lv_tick_inc(40);lv_timer_handler();
    assert(ouse_debug_touch(160,430,true)==ESP_OK);lv_tick_inc(40);lv_timer_handler();
    assert(ouse_debug_touch(160,430,false)==ESP_OK);tick();ouse_shell_get_state(&shell);
    assert(!shell.recents && ouse_app_get_stats(&state)==ESP_OK && !state.active_id[0]);
    assert(ouse_debug_open("ouse.counter")==ESP_OK);
    assert(ouse_debug_touch(160,474,true)==ESP_OK);lv_tick_inc(35);lv_timer_handler();
    assert(ouse_debug_touch(160,430,true)==ESP_OK);lv_tick_inc(35);lv_timer_handler();
    for(unsigned i=0;i<6;++i){
        assert(ouse_debug_touch(160+(int)(i%3),430-(int)(i%2),true)==ESP_OK);
        lv_tick_inc(35);lv_timer_handler();
    }
    ouse_shell_get_state(&shell);assert(shell.recents && shell.tracking);
    assert(ouse_debug_touch(160,430,false)==ESP_OK);tick();ouse_shell_get_state(&shell);assert(shell.recents);
    char debug_state[400],debug_tree[32768];
    assert(ouse_debug_state(debug_state,sizeof(debug_state))==ESP_OK && strstr(debug_state,"\"recents\":true"));
    assert(ouse_debug_tree(debug_tree,sizeof(debug_tree))==ESP_OK && strstr(debug_tree,"全部清除"));
    ouse_debug_image_t image;
    assert(ouse_debug_snapshot(&image)==ESP_OK && image.width==320 && image.height==480 && image.size==320*480*3);
    ouse_debug_snapshot_free(&image);assert(!image.pixels);
    assert(ouse_debug_home()==ESP_OK && ouse_app_close_id("ouse.counter")==ESP_OK);tick();
    assert(ouse_app_get_stats(&state)==ESP_OK && !state.live_instances && state.opened==state.closed);
    puts("PASS device debug: actual input injection, short home swipe, jitter-tolerant held overview, UI state/tree/RGB snapshot");
    puts("PASS native settings pages, switch, live language/font, save failure/retry, 200 lifecycle rounds");
    puts("PASS real pointer: follow/cancel quick sheet, slider isolation, back feedback, swipe home, hold recents, restore/swipe close/blank/Clear all");
    puts("PASS repeated animation/preview cleanup, shared themes, large text and bounded UI dispatch");
    assert(locks == unlocks + 2); // only the timed-out lock wasn't acquired.
    puts("PASS real LVGL 9.5: desktop/settings/navigation, brightness success+failure, save pending/failure/retry, system refresh, bounded UI dispatch, opaque background, CJK coverage, 200 round trips");
    return 0;
}
'''.replace("CHARS", ",".join(str(c) for c in chars)).replace("CORE_VERSION_EXPECTED",(ROOT/"version.txt").read_text().strip())
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
file(GLOB UI_FONTS "CORE/ouse_font_ui_*.c")
add_executable(check check.c ${UI_FONTS} "CORE/ouse_ui.c" "CORE/ouse_widgets.c" "CORE/ouse_dialog.c" "CORE/ouse_screen.c" "CORE/ouse_debug.c" "CORE/ouse_device_ui.c" "DEVICES/ouse_devices.c" "CORE/ouse_core.c" "CORE/ouse_apps.c" "CORE/ouse_shell.c" "CORE/ouse_theme.c" "CORE/ouse_navigation.cpp" "MAIN/counter_app.c")
target_include_directories(check PRIVATE "DEVICES/include" "CORE/include" "CORE" "MAIN" "SYSTEM_INCLUDE" "OUT" "BOARD")
target_compile_options(check PRIVATE /utf-8 /UNDEBUG)
target_link_libraries(check PRIVATE lvgl)
'''.replace("OUT", OUT.as_posix()).replace("LVGL", LVGL.as_posix()).replace("DEVICES", (ROOT/"components/ouse_devices").as_posix()).replace("CORE", CORE.as_posix()).replace("SYSTEM_INCLUDE", SYSTEM_INCLUDE.as_posix()).replace("MAIN", (ROOT/"tools/fixtures").as_posix()).replace("BOARD", (ROOT/"boards/waveshare_lcd35").as_posix())
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
app_commands=[(i,c) for i,c in enumerate(commands) if not Path(c["file"]).resolve().is_relative_to(LVGL.resolve())]
library_commands=[(i,c) for i,c in enumerate(commands) if Path(c["file"]).resolve().is_relative_to(LVGL.resolve())]
objects = OUT / "objects"
objects.mkdir(exist_ok=True)
def compile_file(item):
    index, entry = item
    target = objects / f"{index}.obj"
    is_app = not Path(entry["file"]).resolve().is_relative_to(LVGL.resolve())
    args = [str(compiler), "/nologo", "/c", "/O2", "/MD", "/utf-8",
            "/D_CRT_SECURE_NO_WARNINGS", "/DLV_KCONFIG_IGNORE", "/DLV_LVGL_H_INCLUDE_SIMPLE",
            '/DLV_CONF_PATH="'+(OUT/"lv_conf.h").as_posix()+'"',
            "/I"+str(OUT), "/I"+str(LVGL), "/I"+str(ROOT/"components/ouse_devices/include"), "/I"+str(CORE/"include"), "/I"+str(SYSTEM_INCLUDE), "/I"+str(CORE), "/I"+str(ROOT/"tools/fixtures"), "/I"+str(ROOT/"boards/waveshare_lcd35"),
            "/Fo"+str(target), entry["file"]]
    if not is_app: args.append("/DNDEBUG")
    args += ["/std:c++17", "/EHsc"] if entry["file"].endswith(".cpp") else ["/std:c11"]
    result = subprocess.run(args, cwd=OUT, env=env, capture_output=True, text=True, errors="replace", timeout=60)
    assert result.returncode == 0, (entry["file"], result.stdout, result.stderr)
    return target
lib = OUT / "lvgl-native.lib"
library_inputs = [Path(entry["file"]) for _,entry in library_commands] + list((LVGL/"src").rglob("*.h"))
library_inputs += [OUT/"lv_conf.h", LVGL/"lvgl.h"]
reuse_library = lib.exists() and lib.stat().st_mtime >= max(p.stat().st_mtime for p in library_inputs)
selected = app_commands if reuse_library else app_commands+library_commands
print(f"Compiling {len(selected)} native files; reuse verified LVGL library={reuse_library}", flush=True)
with ThreadPoolExecutor(max_workers=6) as workers:
    compiled = list(workers.map(compile_file, selected))
if not reuse_library:
    (OUT/"library.rsp").write_text('\n'.join('"'+str(p)+'"' for p in compiled[len(app_commands):]), encoding="utf-8")
    result = subprocess.run([str(compiler.parent/"lib.exe"), "/nologo", "/OUT:"+str(lib), "@"+str(OUT/"library.rsp")],
                            cwd=OUT, env=env, capture_output=True, text=True, errors="replace")
    assert result.returncode == 0, (result.stdout, result.stderr)
result = subprocess.run([str(compiler), "/nologo", *[str(p) for p in compiled[:len(app_commands)]], str(lib),
                        "/Fe"+str(OUT/"check.exe")], cwd=OUT, env=env, capture_output=True, text=True, errors="replace")
assert result.returncode == 0, (result.stdout, result.stderr)
widget_only="--focused-widgets" in sys.argv[1:]
current_ui="--focused-current-ui" in sys.argv[1:]
focused=widget_only or current_ui or "--focused-native-ui" in sys.argv[1:]
if not focused:
    subprocess.run([str(OUT / "check.exe")], check=True, cwd=OUT, timeout=60)
    subprocess.run([str(OUT / "check.exe"), "--lifecycle-errors"], check=True, cwd=OUT, timeout=60)
    subprocess.run([str(OUT / "check.exe"), "--launcher-pages"], check=True, cwd=OUT, timeout=60)
if not widget_only:subprocess.run([str(OUT / "check.exe"), "--focused-native-ui"], check=True, cwd=OUT, timeout=60)
if widget_only or current_ui or not focused:subprocess.run([str(OUT / "check.exe"), "--focused-widgets"], check=True, cwd=OUT, timeout=60)
from PIL import Image
previews=() if widget_only else ("wifi-modal-en-large", "recents-font130", "native-power-compact", "native-timeout-choices", "native-sound-compact", "native-appearance-light", "native-appearance-dark")
if widget_only or current_ui or not focused:previews+=("widgets-switch-current","widgets-switch-dark","widgets-switch-light","widgets-switch-dark-hdpi","widgets-switch-light-hdpi")
if not focused:
    previews+=("desktop", "settings", "settings-gap-scroll", "display", "system", "settings-dark", "settings-text-large", "back-follow", "quick-follow", "quick-gap-scroll", "home-follow", "quick", "recents", "recents-second", "desktop-dark", "quick-dark", "quick-en-large", "desktop-light", "desktop-page2", "save-error", "power", "audio", "wifi", "wifi-password", "wifi-form-gap-scroll")
for name in previews:
    with Image.open(OUT / (name+".ppm")) as image:
        image.save(OUT / (name+".png"))

print("PASS native core compiled against real LVGL; firmware/device not built or accessed")
print(f"Preview: {OUT / ('widgets-switch-light.png' if widget_only else 'recents-font130.png' if focused else 'desktop.png')}")
