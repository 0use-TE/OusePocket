"""Build and exercise Ouse Core with real LVGL on the host (no ESP firmware build).

--focused-native-ui runs only the modal/card preference checks, without the
existing lifecycle stress scenarios.
Default lifecycle regression uses 24 rounds; --stress retains the 200-round run.
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
const char *esp_err_to_name(esp_err_t error);
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_RESPONSE 0x108
""")
write_if_changed(OUT / "lv_conf.h", """#pragma once
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_DEF_REFR_PERIOD 16
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
write_if_changed(OUT / "sdmmc_cmd.h", "#pragma once\ntypedef struct {int unused;} sdmmc_card_t;\n")
write_if_changed(OUT / "esp_timer.h", "#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n")
(OUT / "freertos").mkdir(exist_ok=True)
write_if_changed(OUT / "freertos/FreeRTOS.h", """#pragma once
typedef unsigned TickType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(value) ((void)(value))
#define portEXIT_CRITICAL(value) ((void)(value))
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
#define OUSE_APP_CAPACITY 80
#include "ouse_shell.h"
#include "ouse_device_driver.h"
#include "ouse_dialog.h"
#include "ouse_device_ui.h"
#include "ouse_widgets.h"
#include "ouse_apps_internal.h"
#include "display_flush_wait.h"
#include "core-services.h"
#include "ouse_memory_pressure.h"
ouse_memory_pressure_t ouse_test_memory_pressure=OUSE_MEMORY_NORMAL;
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
LV_FONT_DECLARE(ouse_font_ui_16)
static uint8_t pixels[320 * 480 * 2], buffer[320 * 50 * 2];
static unsigned brightness, calls, locks, unlocks, flushes;
int64_t esp_timer_get_time(void){return (int64_t)lv_tick_get()*1000;}
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
void ouse_display_wait_event(lv_display_t *display) {
    assert(display==dma.display);
    vTaskDelay(1); // Simulate completion delivery, retaining source-buffer checks.
}
static void async_flush(lv_display_t *display,const lv_area_t *area,uint8_t *data) {
    assert(display==dma.display && display->flushing);
    assert(!dma.pending && dma.submitted==dma.completed);
    if(dma.submitted && lv_display_is_double_buffered(display))assert(data!=dma.data);
    dma.data=data;
    dma.size=(size_t)(area->x2-area->x1+1)*(size_t)(area->y2-area->y1+1)*2;
    assert(dma.size && dma.size<=64*8*2);
    dma.checksum=dma_checksum(data,dma.size);
    dma.current_yields=0;++dma.submitted;
    if(dma.complete_inline) {
        ++dma.completed;lv_display_flush_ready(display);
    } else dma.pending=true;
}
static void test_async_flush_wait_mode(bool double_buffered) {
    uint8_t drawing[64*8*2],second[64*8*2];
    lv_display_t *display=lv_display_create(64,48);
    dma.display=display;
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,drawing,double_buffered ? second : NULL,sizeof(drawing),LV_DISPLAY_RENDER_MODE_PARTIAL);
    assert(lv_display_is_double_buffered(display)==double_buffered);
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
}
static void test_async_flush_wait(void) {
    test_async_flush_wait_mode(false);
    test_async_flush_wait_mode(true);
    puts("PASS real LVGL single/double-buffer asynchronous flush: DMA buffer unchanged until three yields complete; inline completion does not wait");
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
    lv_tick_inc(50);lv_indev_read(touch_device);lv_timer_handler();
}
static void swipe(int x, int y, int stop_x, int stop_y) {
    touch_step(x,y,true);
    for (unsigned i=1;i<=6;++i) touch_step(x+(stop_x-x)*(int)i/6,y+(stop_y-y)*(int)i/6,true);
    touch_step(stop_x,stop_y,false); tick();
}
static unsigned caret_pixels(lv_obj_t *input){
    lv_area_t bounds;lv_obj_get_coords(input,&bounds);unsigned count=0;
    uint16_t accent=lv_color_to_u16(lv_color_hex(ouse_theme_colors().accent));
    for(int vertical=bounds.y1;vertical<=bounds.y2 && vertical<480;++vertical)
        for(int horizontal=bounds.x1;horizontal<=bounds.x2 && horizontal<320;++horizontal)
            if(vertical>=0 && horizontal>=0){unsigned offset=(unsigned)(vertical*320+horizontal)*2;
                if((pixels[offset]|((uint16_t)pixels[offset+1]<<8))==accent)++count;}
    return count;
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
static lv_obj_t *brightness_slider(lv_obj_t *root) {
    if(!lv_obj_is_visible(root))return NULL;
    if(lv_obj_check_type(root,&lv_slider_class) && lv_slider_get_min_value(root)==10)return root;
    for(unsigned index=0;index<lv_obj_get_child_count(root);++index){lv_obj_t *found=brightness_slider(lv_obj_get_child(root,index));if(found)return found;}
    return NULL;
}
static lv_obj_t *find_type(lv_obj_t *root,const lv_obj_class_t *type) {
    if(!lv_obj_is_visible(root))return NULL;
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
    int available=lv_obj_get_scroll_bottom(page);assert(available>0);
    bool pressed=false;lv_obj_add_event_cb(page,gap_pressed,LV_EVENT_PRESSED,&pressed);
    lv_indev_reset(touch_device,NULL);touch_step(point.x,point.y,false);
    touch_step(point.x,point.y,true);
    // Verify the empty gap really hit this scrolling surface, not a button.
    if(!pressed){char tree[30000];ouse_debug_tree(tree,sizeof(tree));printf("Gap did not target form: x=%d y=%d outer=%d,%d,%d,%d\n%s\n",point.x,point.y,outer.x1,outer.y1,outer.x2,outer.y2,tree);fflush(stdout);}
    assert(pressed);lv_obj_remove_event_cb(page,gap_pressed);
    for(unsigned i=1;i<=6;++i)touch_step(point.x,point.y+(end_y-point.y)*(int)i/6,true);
    touch_step(point.x,end_y,false);tick();
    int after=lv_obj_get_scroll_y(page);
    printf("Native blank-gap pointer scroll: origin=(%d,%d) before=%d after=%d\n",point.x,point.y,before,after);fflush(stdout);
    assert(after>=before+(available<11 ? available : 11));
}
static unsigned count(lv_obj_t *root) {
    unsigned result = 1;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) result += count(lv_obj_get_child(root, i));
    return result;
}
static void click(lv_obj_t *root, const char *value) {
    ouse_wifi_request_t completed;while(host_take_wifi(&completed)){}
    lv_obj_t *target = label(root, value);
    if(!target && !strcmp(value,"返回设置"))target=label(root,LV_SYMBOL_LEFT);
    if(!target)fprintf(stderr,"Missing click label: %s; launching=%d\n",value,ouse_core_launch_active());
    assert(target);
    lv_obj_t *button=lv_obj_get_parent(target);
    while (button && !lv_obj_check_type(button,&lv_button_class)) button=lv_obj_get_parent(button);
    assert(button);
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
}
static void press_keyboard(lv_obj_t *keyboard,const char *key) {
    for(unsigned index=0;index<200;++index){
        const char *text=lv_buttonmatrix_get_button_text(keyboard,index);
        if(text && (!strcmp(text,key) || (!strcmp(key,LV_SYMBOL_CLOSE) && !strcmp(text,LV_SYMBOL_KEYBOARD)))){lv_buttonmatrix_set_selected_button(keyboard,index);lv_obj_send_event(keyboard,LV_EVENT_VALUE_CHANGED,NULL);return;}
    }
    assert(false);
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
    // Repeated state polling must not dirty an idle desktop. Exclude a real
    // wall-clock minute change, which legitimately updates the clock label.
    extern void ouse_navigation_clock(char *);
    char clock_before[8],clock_after[8];
    tick();tick();ouse_navigation_clock(clock_before);
    unsigned idle_flushes=flushes;tick();ouse_navigation_clock(clock_after);
    if(!strcmp(clock_before,clock_after))assert(flushes==idle_flushes);
    puts("PASS idle desktop: unchanged status polls submit no LCD flushes");
    tick();unsigned baseline=count(root),baseline_timers=timers(),baseline_bindings=ouse_ui_binding_count();
    allow_ui_save=false;
    assert(ouse_core_set_ui(1,100)==ESP_FAIL && ouse_ui_locale()==1 && ouse_ui_font_percent()==100);
    assert(ouse_dialog_active() && label(root,"Retry"));
    click(root,"Retry");assert(ouse_dialog_active() && ui_save_calls==2);
    allow_ui_save=true;click(root,"Retry");assert(!ouse_dialog_active() && ui_save_calls==3);
    assert(saved.requested_locale==1 && saved.requested_font_percent==100);
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
    lv_textarea_set_text(password,"bad");press_keyboard(keyboard,LV_SYMBOL_OK);tick();
    assert(ouse_dialog_active() && ouse_device_ui_modal() && !ouse_devices_take_wifi(&request));
    swipe(4,240,84,240);assert(!ouse_dialog_active() && ouse_device_ui_modal());
    swipe(316,240,236,240);assert(!ouse_device_ui_modal() && !*lv_textarea_get_text(password));
    click(root,"Office");lv_textarea_set_text(password,"12345678");
    assert(ouse_core_set_ui(1,100)==ESP_OK);tick();
    assert(ouse_device_ui_modal() && !strcmp(lv_textarea_get_text(password),"12345678"));
    assert(label(root,"Connect") && label(root,"Cancel"));
    press_keyboard(keyboard,LV_SYMBOL_CLOSE);tick();
    assert(!ouse_device_ui_modal() && !*lv_textarea_get_text(password));
    click(root,"Add network");tick();assert(ouse_device_ui_modal());
    lv_obj_t *ssid=find_type(root,&lv_textarea_class);assert(ssid && ssid!=password);
    assert(lv_keyboard_get_textarea(keyboard)==ssid);
    lv_textarea_set_text(ssid,"HiddenNetwork");lv_textarea_set_text(password,"abcdefgh");
    screenshot("wifi-modal-en-large.ppm");
    press_keyboard(keyboard,LV_SYMBOL_OK);tick();
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
    assert(ouse_core_set_ui(0,130)==ESP_ERR_INVALID_ARG);
    assert(ouse_ui_font_percent()==100);
    title=recent_title(root,"计数器");assert(title);
    int after=lv_obj_get_height(title),line=lv_obj_get_style_text_font(title,0)->line_height;
    screenshot("recents-font130.ppm");
    printf("Recent title after quick-font130: before=%d after=%d required_line_height=%d\n",before,after,line);fflush(stdout);
    assert(after>=line);
    assert(ouse_core_set_ui(1,100)==ESP_OK);tick();assert(ouse_ui_locale()==1);
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
    const unsigned densities[]={160,240,320},fonts[]={100};
    assert(ouse_ui_init(160,0,130)==ESP_ERR_INVALID_ARG);
    for(unsigned density=0;density<3;++density)for(unsigned font=0;font<1;++font)for(unsigned dark=0;dark<2;++dark) {
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
        if(density==2 && font==0)screenshot(dark ? "widgets-switch-dark-hdpi.ppm" : "widgets-switch-light-hdpi.ppm");
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
    puts("PASS widget pixels: light/dark, checked/off, 160/240/320dpi, fixed 100% text and unsupported-size rejection; opaque round knob/track, 40dp contact, one toggle, content buttons and compact rows");
}
static unsigned file_open_target,file_open_calls;
static bool host_side_keys[2];
static unsigned host_side_clicks;
static bool host_read_key(void *context,unsigned key){(void)context;return key<2 && host_side_keys[key];}
static bool host_app_key(void *context,unsigned key){(void)context;if(key!=1)return false;++host_side_clicks;return true;}
static char file_open_path[128];
static esp_err_t file_test_open(lv_obj_t *root,void *context){(void)root;(void)context;return ESP_OK;}
static void file_test_close(void *context){(void)context;}
static esp_err_t launch_slow_open(lv_obj_t *root,void *context){
    (void)context;lv_obj_add_flag(root,OUSE_APP_LOADING_FLAG);
    lv_label_set_text(lv_label_create(root),"Ready content");return ESP_OK;
}
static esp_err_t launch_blocking_open(lv_obj_t *root,void *context){
    (void)context;lv_tick_inc(480);lv_label_set_text(lv_label_create(root),"Native ready");return ESP_OK;
}
static void advance_launch(unsigned ms){
    for(unsigned elapsed=0;elapsed<ms;elapsed+=16){lv_tick_inc(16);lv_timer_handler();}
}
static esp_err_t file_test_receive(void *context,const char *path){file_open_target=(unsigned)(uintptr_t)context;++file_open_calls;snprintf(file_open_path,sizeof(file_open_path),"%s",path);return ESP_OK;}
int main(int argc, char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
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
    bool order=argc>1 && !strcmp(argv[1],"--launcher-order");
    bool reload=argc>1 && !strcmp(argv[1],"--launcher-order-reload");
    bool corrupt=argc>1 && !strcmp(argv[1],"--launcher-order-corrupt");
    bool pages=argc>1 && (!strcmp(argv[1],"--launcher-pages") || order || reload);
    if(reload)host_launcher_order=_strdup("OUSE1\nouse.counter\nouse.settings\n");
    if(corrupt)host_launcher_order=_strdup("OUSE1\nouse.settings\nouse.settings\n");
    ouse_core_config_t config = {.port = {
        .display = display, .touch = touch, .lock = lock, .unlock = unlock,
        .set_brightness = set_brightness, .save_brightness = save_setting, .save_theme = save_theme,
        .get_settings_state = settings_state, .get_system_info = system_info, .board_name = "Waveshare P4 LCD 3.5"}, .initial_brightness = 45, .initial_font_percent=100, .apps = definitions, .app_count = (pages ? OUSE_APP_CAPACITY-1 : argc>1 ? 2 : 1)};
    if(argc>1 && !strcmp(argv[1],"--focused-native-ui"))config.port.save_ui=audit_save_ui;
    if(argc>1 && !strcmp(argv[1],"--hardware-keys")){config.port.read_key=host_read_key;host_side_keys[0]=true;}
    if(argc>1 && !strncmp(argv[1],"--boot-animation",16))config.boot_animation=true;
    if(argc>1 && !strcmp(argv[1],"--boot-animation-low-memory"))ouse_test_memory_pressure=OUSE_MEMORY_WARNING;
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
    config.apps=NULL;
    assert(ouse_core_start(&config)==ESP_ERR_INVALID_ARG);
    config.apps=definitions;
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
    if(argc>1 && !strcmp(argv[1],"--launch-animation")){
        uint32_t *icon_pixels=calloc(8*8,sizeof(uint32_t));assert(icon_pixels);
        for(unsigned i=0;i<64;++i)icon_pixels[i]=0xff367bef;
        lv_image_dsc_t icon={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w=8,.h=8,.stride=32},.data_size=256,.data=(uint8_t *)icon_pixels};
        ouse_app_definition_t slow={.id="user.launch-slow",.name="Slow Launch",.icon=LV_SYMBOL_FILE,.icon_image=&icon,.open=launch_slow_open,.close=file_test_close};
        assert(ouse_core_user_apply(&slow)==ESP_OK);tick();
        unsigned objects=count(root),clock_count=timers(),bindings=ouse_ui_binding_count();
        ouse_app_definition_t blocking={.id="user.launch-blocking",.name="Native Launch",.icon=LV_SYMBOL_FILE,.open=launch_blocking_open,.close=file_test_close};
        assert(ouse_core_user_apply(&blocking)==ESP_OK);tick();click(root,"Native Launch");
        advance_launch(100);assert(ouse_core_launch_active() && !label(root,"Native ready"));
        advance_launch(200);assert(!ouse_core_launch_active() && label(root,"Native ready"));
        assert(ouse_core_user_remove(blocking.id)==ESP_OK);tick();
        assert(count(root)==objects && timers()==clock_count && ouse_ui_binding_count()==bindings);
        lv_obj_t *tile=lv_obj_get_parent(lv_obj_get_parent(label(root,LV_SYMBOL_PLUS)));
        click(root,LV_SYMBOL_PLUS);assert(ouse_core_launch_active() && !label(root,"+1"));
        ouse_app_stats_t stats;ouse_app_get_stats(&stats);unsigned opened=stats.opened;
        lv_obj_send_event(tile,LV_EVENT_CLICKED,NULL);ouse_app_get_stats(&stats);assert(stats.opened==opened);
        advance_launch(64);assert(ouse_core_launch_active());screenshot("launch-expand.ppm");
        advance_launch(240);assert(!ouse_core_launch_active() && label(root,"+1"));
        assert(ouse_app_background()==ESP_OK);tick();click(root,LV_SYMBOL_PLUS);
        advance_launch(176);assert(!ouse_core_launch_active() && label(root,"+1"));
        assert(ouse_app_close()==ESP_OK);tick();
        assert(count(root)==objects && timers()==clock_count && ouse_ui_binding_count()==bindings);
        click(root,"Slow Launch");advance_launch(800);
        assert(ouse_core_launch_active() && !label(root,"Ready content"));screenshot("launch-wait.ppm");
        unsigned waiting_flushes=flushes;advance_launch(320);assert(flushes==waiting_flushes);
        lv_obj_remove_flag(ouse_apps_root(slow.id),OUSE_APP_LOADING_FLAG);advance_launch(100);
        assert(!ouse_core_launch_active() && label(root,"Ready content"));assert(ouse_app_close()==ESP_OK);tick();
        click(root,"Slow Launch");advance_launch(48);ouse_core_launch_cancel();
        assert(!ouse_core_launch_active());ouse_app_get_stats(&stats);assert(!stats.active_id[0]);
        assert(ouse_app_close_id(slow.id)==ESP_OK);tick();
        click(root,"Slow Launch");advance_launch(48);touch_step(4,240,true);touch_step(4,240,false);
        assert(!ouse_core_launch_active());assert(ouse_app_close_id(slow.id)==ESP_OK);tick();
        click(root,"Slow Launch");advance_launch(48);
        assert(ouse_core_user_apply(&slow)==ESP_OK && !ouse_core_launch_active());
        free(icon_pixels);slow.icon_image=NULL;assert(ouse_core_user_apply(&slow)==ESP_OK);tick();
        assert(count(root)==objects && timers()==clock_count && ouse_ui_binding_count()==bindings);
        click(root,LV_SYMBOL_WARNING);assert(!ouse_core_launch_active() && ouse_dialog_active());
        ouse_dialog_dismiss();tick();assert(count(root)==objects && timers()==clock_count && ouse_ui_binding_count()==bindings);
        click(root,"Slow Launch");advance_launch(8100);assert(!ouse_core_launch_active() && ouse_dialog_active());
        ouse_dialog_dismiss();assert(ouse_app_close()==ESP_OK);tick();
        click(root,"Slow Launch");advance_launch(48);assert(ouse_core_set_theme(OUSE_THEME_DARK)==ESP_OK);
        assert(!ouse_core_launch_active());assert(ouse_app_close()==ESP_OK);tick();
        ouse_test_memory_pressure=OUSE_MEMORY_WARNING;
        click(root,LV_SYMBOL_PLUS);assert(!ouse_core_launch_active() && label(root,"+1"));
        ouse_test_memory_pressure=OUSE_MEMORY_NORMAL;assert(ouse_app_close()==ESP_OK);tick();
        assert(count(root)==objects && timers()==clock_count && ouse_ui_binding_count()==bindings);
        puts("PASS launch animation: cold/resume, duplicate click, delayed first screen, cancel, icon replacement, timeout, theme interruption, low-memory fallback and complete cleanup");
        return 0;
    }
    if(argc>1 && !strncmp(argv[1],"--boot-animation",16)){
        if(!strcmp(argv[1],"--boot-animation-low-memory")){assert(!label(lv_layer_top(),"Ouse"));ouse_test_memory_pressure=OUSE_MEMORY_NORMAL;assert(ouse_app_open("ouse.settings")==ESP_OK);puts("PASS low memory skips optional boot animation");return 0;}
        assert(label(lv_layer_top(),"Ouse"));
        if(!strcmp(argv[1],"--boot-animation-error")){
            ouse_dialog_show("Boot test error",NULL,NULL);tick();assert(!label(lv_layer_top(),"Ouse") && label(root,"Boot test error"));
        }else if(!strcmp(argv[1],"--boot-animation-timeout")){
            for(unsigned index=0;index<7;++index)tick();assert(!label(lv_layer_top(),"Ouse"));
        }else{
            for(unsigned frame=0;frame<20;++frame){lv_tick_inc(20);lv_timer_handler();}
            screenshot("boot-animation.ppm");assert(label(lv_layer_top(),"Ouse"));
            ouse_core_launcher_restored();tick();assert(!label(lv_layer_top(),"Ouse"));
        }
        assert(!ouse_core_is_started() || ouse_app_open("ouse.settings")==ESP_OK);
        puts("PASS boot animation: nonblocking entrance, readiness dismissal, bounded timeout and native-error priority; desktop usable after cleanup");return 0;
    }
    if(argc>1 && !strcmp(argv[1],"--hardware-keys")){
        ouse_audio_state_t audio;tick();host_side_keys[0]=false;tick();ouse_audio_get(&audio);assert(audio.volume==40);
        host_side_keys[0]=true;tick();host_side_keys[0]=false;tick();ouse_audio_get(&audio);assert(audio.volume==45);
        host_side_keys[1]=true;tick();host_side_keys[1]=false;tick();ouse_audio_get(&audio);assert(audio.volume==40);
        ouse_app_definition_t camera={.id="user.key-camera",.name="Camera",.icon=LV_SYMBOL_IMAGE,.open=file_test_open,.close=file_test_close,.key=host_app_key};
        assert(ouse_core_user_apply(&camera)==ESP_OK);assert(ouse_app_open(camera.id)==ESP_OK);
        host_side_keys[1]=true;tick();host_side_keys[1]=false;tick();ouse_audio_get(&audio);assert(audio.volume==40 && host_side_clicks==1);
        host_side_keys[1]=true;tick();tick();assert(ouse_dialog_active());host_side_keys[1]=false;tick();assert(host_side_clicks==1);
        click(root,"关机");assert(host_control_requests==0);click(root,"取消");assert(!host_control_requests);
        host_side_keys[1]=true;tick();tick();host_side_keys[1]=false;tick();click(root,"关机");click(root,"关机");assert(host_control_requests==1 && host_control_action==OUSE_CONTROL_SHUTDOWN && host_side_clicks==1);
        assert(ouse_app_background()==ESP_OK);assert(ouse_core_sleep_display()==ESP_OK);host_side_keys[0]=true;tick();host_side_keys[0]=false;tick();
        ouse_power_state_t power;ouse_power_get(&power);ouse_audio_get(&audio);assert(!power.asleep && audio.volume==40);
        host_side_keys[1]=true;tick();host_side_keys[1]=false;tick();ouse_audio_get(&audio);assert(audio.volume==35 && host_side_clicks==1);
        puts("PASS hardware keys: boot-held suppression, short volume keys, foreground override, reserved long hold, shutdown confirmation and wake-contact consumption");return 0;
    }
    if(argc>1 && !strcmp(argv[1],"--file-associations")){
        host_files_ready=true;
        ouse_app_definition_t viewer={.id="user.viewer-one",.name="ViewerOne",.icon=LV_SYMBOL_IMAGE,.open=file_test_open,.close=file_test_close,.file_types=OUSE_FILE_JPEG,.open_file=file_test_receive,.context=(void *)1};
        assert(ouse_core_user_apply(&viewer)==ESP_OK);ouse_core_launcher_restored();tick();
        assert(ouse_app_open("ouse.counter")==ESP_OK);
        assert(ouse_core_open_file("user.background","/photo.jpg")==ESP_ERR_INVALID_STATE);
        assert(ouse_core_open_file("ouse.counter","/../photo.jpg")==ESP_ERR_INVALID_ARG);
        assert(ouse_core_open_file("ouse.counter","/photo.JPG")==ESP_OK && file_open_target==1 && !strcmp(file_open_path,"/photo.JPG"));
        viewer.id="user.viewer-two";viewer.name="ViewerTwo";viewer.context=(void *)2;assert(ouse_core_user_apply(&viewer)==ESP_OK);tick();
        assert(ouse_app_open("ouse.counter")==ESP_OK);assert(ouse_core_open_file("ouse.counter","/photo.jpg")==ESP_OK);assert(ouse_dialog_active());
        click(root,"ViewerTwo");click(root,"仅此次");assert(file_open_target==2);tick();assert(!host_file_defaults);
        assert(ouse_app_open("ouse.counter")==ESP_OK);assert(ouse_core_open_file("ouse.counter","/photo.jpg")==ESP_OK);
        click(root,"ViewerTwo");click(root,"始终");tick();assert(host_file_defaults && strstr(host_file_defaults,"user.viewer-two\n"));
        assert(ouse_app_open("ouse.counter")==ESP_OK);unsigned before=file_open_calls;assert(ouse_core_open_file("ouse.counter","/second.jpeg")==ESP_OK);assert(file_open_calls==before+1 && !ouse_dialog_active());
        assert(ouse_app_open("ouse.counter")==ESP_OK);assert(ouse_core_user_remove("user.viewer-two")==ESP_OK);tick();assert(!strstr(host_file_defaults,"user.viewer-two"));
        host_files_ready=false;before=file_open_calls;assert(ouse_core_open_file("ouse.counter","/photo.jpg")==ESP_OK);assert(file_open_calls==before && ouse_dialog_active());ouse_dialog_dismiss();
        puts("PASS file associations: foreground and SD boundaries, exact path/cold-warm delivery, single/multiple handlers, once/always persistence and uninstall cleanup");return 0;
    }
    if(argc>1 && !strcmp(argv[1],"--ble-settings")){
        assert(ouse_app_open("ouse.settings")==ESP_OK);tick();
        lv_obj_t *entry=label(root,"蓝牙");assert(entry);lv_obj_scroll_to_view_recursive(lv_obj_get_parent(entry),LV_ANIM_OFF);tick();click(root,"蓝牙");tick();
        assert(label(root,"OusePocket") && label(root,"已关闭"));
        lv_obj_t *toggle=find_type(root,&lv_switch_class);assert(toggle);lv_obj_add_state(toggle,LV_STATE_CHECKED);lv_obj_send_event(toggle,LV_EVENT_VALUE_CHANGED,NULL);tick();assert(host_ble.enabled && label(root,"可被发现"));
        click(root,"搜索设备");tick();assert(host_ble.scanning && label(root,"正在搜索"));
        host_ble.scanning=false;host_ble.count=1;strcpy(host_ble.peers[0].name,"Home PC");strcpy(host_ble.peers[0].address,"01:02:03:04:05:06");++host_ble.revision;tick();
        click(root,"Home PC");tick();assert(host_ble.connected && label(root,"已连接"));click(root,"断开连接");tick();assert(!host_ble.connected);
        host_ble.pair_pending=true;host_ble.pair_id=7;host_ble.number=123456;++host_ble.revision;tick();assert(ouse_dialog_active() && label(root,"蓝牙配对\n123456"));click(root,"配对");tick();assert(!host_ble.pair_pending && host_ble.bonded && !ouse_dialog_active());
        host_ble.pair_pending=true;host_ble.pair_id=8;++host_ble.revision;tick();assert(ouse_dialog_active());click(root,"取消");tick();assert(!host_ble.pair_pending && !host_ble.bonded);
        assert(ouse_app_close_id("ouse.settings")==ESP_OK);tick();puts("PASS real Bluetooth settings UI: toggle, discovery, peer connect/disconnect, numeric pairing accept and cancel, cleanup");return 0;
    }
    if(argc>1 && !strcmp(argv[1],"--ime-cursor")){
        lv_obj_t *view=lv_obj_create(root);lv_obj_remove_style_all(view);lv_obj_set_size(view,320,416);lv_obj_set_y(view,40);
        lv_obj_t *first=lv_textarea_create(view),*second=lv_textarea_create(view);
        lv_obj_remove_style_all(first);lv_obj_remove_style_all(second);
        lv_obj_set_pos(first,20,20);lv_obj_set_size(first,260,48);lv_obj_set_style_pad_all(first,8,0);ouse_ui_use_font(first,16);
        lv_obj_set_pos(second,20,84);lv_obj_set_size(second,260,48);lv_obj_set_style_pad_all(second,8,0);ouse_ui_use_font(second,16);
        lv_textarea_set_text(first,"");lv_textarea_set_text(second,"");
        assert(ouse_ime_attach(first,view,OUSE_IME_RESIZE,OUSE_IME_TEXT)==ESP_OK);
        assert(ouse_ime_attach(second,view,OUSE_IME_RESIZE,OUSE_IME_PASSWORD)==ESP_OK);
        for(unsigned theme=0;theme<2;++theme){
            lv_textarea_set_text(first,"");lv_textarea_set_text(second,"");
            lv_obj_set_style_pad_all(first,theme ? 0 : 8,0);lv_obj_set_style_pad_all(second,theme ? 0 : 8,0);
            lv_obj_set_height(first,theme ? 22 : 48);lv_obj_set_height(second,theme ? 22 : 48);
            assert(ouse_core_set_theme((ouse_theme_t)theme)==ESP_OK);ouse_ime_focus(first);
            lv_tick_inc(20);lv_timer_handler();assert(lv_obj_has_state(first,LV_STATE_FOCUSED));
            unsigned minimum=caret_pixels(first),maximum=minimum;
            for(unsigned frame=0;frame<25;++frame){lv_tick_inc(50);lv_timer_handler();unsigned count=caret_pixels(first);if(count<minimum)minimum=count;if(count>maximum)maximum=count;}
            assert(maximum>minimum && maximum>0);
            lv_textarea_add_text(first,"abcd");assert(lv_textarea_get_cursor_pos(first)==4);
            ouse_ime_focus(second);assert(!lv_obj_has_state(first,LV_STATE_FOCUSED) && lv_obj_has_state(second,LV_STATE_FOCUSED));
            assert(lv_textarea_get_password_mode(second));lv_textarea_add_text(second,"1234");
            ouse_ime_hide();assert(!lv_obj_has_state(second,LV_STATE_FOCUSED));tick();
            assert(!caret_pixels(first) && !caret_pixels(second));
        }
        lv_obj_delete(view);assert(!ouse_ime_active());
        puts("PASS real caret pixels: empty field visible/blinking, both themes, insertion position, password focus, old-field cleanup and blur");return 0;
    }
    lv_obj_t *ime_view=lv_obj_create(root),*ime_input=lv_textarea_create(ime_view);
    assert(ouse_ime_attach(ime_input,ime_view,OUSE_IME_RESIZE,OUSE_IME_TEXT)==ESP_OK);
    ouse_ime_focus(ime_input);ouse_ime_hide();lv_obj_delete(ime_view);
    if(corrupt){
        tick();ouse_core_launcher_restored();tick();assert(ouse_dialog_active());
        assert(!strcmp(host_launcher_order,"OUSE1\nouse.settings\nouse.settings\n"));
        puts("PASS corrupt/duplicate saved order rejected without silently overwriting preserved file");return 0;
    }
    if(argc>1 && !strcmp(argv[1],"--memory-pressure")){
        assert(ouse_memory_classify(32768,8192,524288)==OUSE_MEMORY_WARNING);
        assert(ouse_memory_classify(32767,20000,2000000)==OUSE_MEMORY_CRITICAL);
        assert(ouse_memory_classify(90000,8191,2000000)==OUSE_MEMORY_CRITICAL);
        assert(ouse_memory_classify(90000,20000,524287)==OUSE_MEMORY_CRITICAL);
        assert(ouse_memory_classify(90000,20000,2000000)==OUSE_MEMORY_NORMAL);
        assert(ouse_app_open("ouse.counter")==ESP_OK);
        ouse_test_memory_pressure=OUSE_MEMORY_CRITICAL;
        assert(ouse_app_open("ouse.settings")==ESP_OK);
        assert(ouse_app_open("ouse.fail")==ESP_ERR_NO_MEM && failed_cleanup==0);
        ouse_app_stats_t stats;ouse_app_get_stats(&stats);
        assert(!strcmp(stats.active_id,"ouse.settings") && stats.live_instances==2 && !stats.preview_bytes);
        assert(ouse_app_open("ouse.counter")==ESP_OK);
        ouse_test_memory_pressure=OUSE_MEMORY_NORMAL;
        assert(ouse_app_close_id("ouse.counter")==ESP_OK);
        assert(ouse_app_close_id("ouse.settings")==ESP_OK);
        puts("PASS memory pressure: boundary classification, reserve rejection before allocation, current app retained, settings/resume admitted, preview allocation skipped, no background app killed");return 0;
    }
    if(argc>1 && !strcmp(argv[1],"--focused-native-ui")) {focused_native_ui(root);return 0;}
    assert(label(root, LV_SYMBOL_SETTINGS) && !label(root, "屏幕亮度"));
    if (pages) {
        tick();
        ouse_core_launcher_restored();tick();
        ouse_app_stats_t state; assert(ouse_app_get_stats(&state)==ESP_OK && state.registered==OUSE_APP_CAPACITY);
        ouse_app_info_t last;assert(ouse_app_get_info(OUSE_APP_CAPACITY-1,&last)==ESP_OK && !strcmp(last.id,extra_ids[OUSE_APP_CAPACITY-4]));
        ouse_app_definition_t overflow=definitions[1];overflow.id="user.overflow";
        assert(ouse_apps_user_apply(&overflow)==ESP_OK);
        ouse_app_info_t info; assert(ouse_app_get_info(7,&info)==ESP_OK && !strcmp(info.id,"ouse.extra6"));
        if(order || reload){
            lv_obj_t *launcher=NULL;
            for(unsigned index=0;index<lv_obj_get_child_count(root);++index){
                lv_obj_t *home=lv_obj_get_child(root,index);
                for(unsigned child=0;child<lv_obj_get_child_count(home);++child){
                    lv_obj_t *candidate=lv_obj_get_child(home,child);
                    if(lv_obj_get_scroll_dir(candidate)==LV_DIR_HOR)launcher=candidate;
                }
            }
            assert(launcher && host_launcher_order);
            if(reload){assert(!strncmp(host_launcher_order,"OUSE1\nouse.counter\nouse.settings\n",32));puts("PASS persisted launcher ID order restored independently of registration order");return 0;}
            assert(!strncmp(host_launcher_order,"OUSE1\nouse.settings\n",20));
            touch_step(58,72,true);for(unsigned step=0;step<15;++step)touch_step(58,72,true);
            for(unsigned step=1;step<=6;++step)touch_step(58+200*(int)step/6,72,true);
            touch_step(258,72,false);tick();
            assert(!ouse_app_get_stats(&state) && !state.active_id[0]);
            char *settings=strstr(host_launcher_order,"ouse.settings\n");assert(settings && settings>host_launcher_order+6);
            assert(settings==strchr(strchr(host_launcher_order+6,'\n')+1,'\n')+1);
            char *saved=_strdup(host_launcher_order);
            assert(ouse_core_set_theme(OUSE_THEME_DARK)==ESP_OK);tick();assert(!strcmp(saved,host_launcher_order));
            touch_step(58,72,true);for(unsigned step=0;step<15;++step)touch_step(58,72,true);
            touch_step(304,72,true);for(unsigned step=0;step<13;++step)touch_step(304,72,true);
            touch_step(258,72,true);touch_step(258,72,false);tick();
            unsigned current,total;ouse_core_launcher_state(&current,&total);assert(current==1);
            assert(strcmp(saved,host_launcher_order));free(saved);
            saved=_strdup(host_launcher_order);host_launcher_save_error=true;
            touch_step(58,72,true);for(unsigned step=0;step<15;++step)touch_step(58,72,true);
            for(unsigned step=1;step<=6;++step)touch_step(58+200*(int)step/6,72,true);
            touch_step(258,72,false);tick();assert(ouse_dialog_active() && !strcmp(saved,host_launcher_order));
            host_launcher_save_error=false;click(root,"重试");tick();assert(!ouse_dialog_active() && strcmp(saved,host_launcher_order));free(saved);
            saved=_strdup(host_launcher_order);
            touch_step(58,72,true);for(unsigned step=0;step<15;++step)touch_step(58,72,true);
            touch_step(258,450,true);touch_step(258,450,false);tick();assert(!strcmp(saved,host_launcher_order));free(saved);
            ouse_app_definition_t added=definitions[1];added.id="user.order-test";assert(ouse_core_user_apply(&added)==ESP_OK);tick();
            assert(strstr(host_launcher_order,"user.order-test\n"));
            saved=_strdup(host_launcher_order);assert(ouse_core_user_apply(&added)==ESP_OK);tick();assert(!strcmp(saved,host_launcher_order));free(saved);
            lv_obj_t *remove_mark=label(root,"X");assert(remove_mark);
            lv_obj_send_event(lv_obj_get_parent(lv_obj_get_parent(remove_mark)),LV_EVENT_LONG_PRESSED,NULL);
            assert(!lv_obj_has_flag(lv_obj_get_parent(remove_mark),LV_OBJ_FLAG_HIDDEN));
            click(root,"X");assert(ouse_dialog_active() && host_uninstall_requests==0);
            click(root,"取消");assert(!ouse_dialog_active() && host_uninstall_requests==0);
            click(root,"X");click(root,"卸载");tick();
            assert(host_uninstall_requests==1 && !strcmp(host_uninstall_target,added.id));
            assert(ouse_core_user_remove(added.id)==ESP_OK);tick();assert(!strstr(host_launcher_order,"user.order-test"));
            puts("PASS launcher long-hold insertion, cross-page drag, no accidental launch, cancelled drop, save failure/retry, theme rebuild, update retention and uninstall pruning");return 0;
        }
        swipe(220,300,60,300);
        lv_obj_t *launcher=NULL;
        for (unsigned i=0;i<lv_obj_get_child_count(root);++i) {
            lv_obj_t *home=lv_obj_get_child(root,i);
            if (!lv_obj_is_visible(home)) continue;
            for (unsigned j=0;j<lv_obj_get_child_count(home);++j) {
                lv_obj_t *grid=lv_obj_get_child(home,j);
                if (lv_obj_get_scroll_dir(grid)==LV_DIR_HOR && lv_obj_get_scroll_x(grid)>0) launcher=grid;
            }
        }
        assert(launcher && lv_obj_has_flag(launcher,LV_OBJ_FLAG_SCROLL_ONE));
        assert(!lv_obj_has_flag(launcher,LV_OBJ_FLAG_SCROLL_ELASTIC));
        unsigned current,total;ouse_core_launcher_state(&current,&total);assert(current==1 && total>=3);
        screenshot("desktop-page2.ppm");
        for(unsigned expected=2;expected<total;++expected){
            swipe(280,300,40,300);ouse_core_launcher_state(&current,&total);assert(current==expected);
        }
        swipe(280,300,40,300);ouse_core_launcher_state(&current,&total);assert(current==total-1);
        for(unsigned expected=total-1;expected>0;--expected){
            swipe(40,300,280,300);ouse_core_launcher_state(&current,&total);assert(current==expected-1);
        }
        swipe(40,300,280,300);ouse_core_launcher_state(&current,&total);assert(current==0);
        swipe(280,300,40,300);ouse_core_launcher_state(&current,&total);assert(current==1);
        puts("PASS dynamic registry beyond 24 entries; single-page swipes in both directions, fast fling and first/last page boundaries");
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
    assert(label(root,"显示与个性化") && label(root,"Ouse"));screenshot("settings.ppm");
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
    allow_info=false;tick();assert(label(root,"关于设备") && label(root,"Waveshare P4 LCD 3.5"));allow_info=true;tick();
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
    click(root,"返回设置");click(root,"语言");click(root,"English");tick();
    assert(label(root,"Language") && !label(root,"语言"));
    assert(ouse_ui_locale()==1 && ouse_ui_font_percent()==100);screenshot("settings-text-large.ppm");
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
    ouse_ime_hide();tick();
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
    for(unsigned i=0;i<CORE_LIFECYCLE_ROUNDS;++i) {
        assert(ouse_app_open("ouse.counter")==ESP_OK);click(root,"+1");
        assert(ouse_app_open("ouse.settings")==ESP_OK);click(root,"显示");
        assert(ouse_app_open("ouse.counter")==ESP_OK && label(root,"1"));
        (void)ouse_app_background();assert(ouse_app_close_id("ouse.counter")==ESP_OK && ouse_app_close_id("ouse.settings")==ESP_OK);
        tick();assert(count(root)==objects && timers()==baseline_timers && ouse_ui_binding_count()==baseline_bindings);
    }
    click(root,LV_SYMBOL_PLUS);tick();click(root,"+1");
    // Back feedback follows the pointer, and a short drag cancels.
    touch_step(4,240,true);touch_step(24,240,true);ouse_shell_get_state(&shell);
    assert(shell.tracking && shell.back_progress>0);screenshot("back-follow.ppm");
    touch_step(24,240,false);tick();assert(label(root,"1"));
    swipe(4,240,84,240);assert(ouse_app_get_stats(&state)==ESP_OK && !state.active_id[0]);
    click(root,LV_SYMBOL_PLUS);tick();swipe(316,240,236,240);assert(ouse_app_get_stats(&state)==ESP_OK && !state.active_id[0]);
    // Pull down exposes an intermediate sheet position before release.
    touch_step(160,14,true);touch_step(160,180,true);ouse_shell_get_state(&shell);
    assert(shell.quick && shell.tracking && shell.quick_y<0 && shell.quick_y>-480);screenshot("quick-follow.ppm");
    touch_step(160,180,false);tick();ouse_shell_get_state(&shell);assert(shell.quick && shell.quick_y==0);
    lv_obj_t *quick_slider=brightness_slider(root);assert(quick_slider);
    lv_obj_t *quick_body=lv_obj_get_parent(lv_obj_get_parent(quick_slider));
    lv_obj_t *usb_tile=lv_obj_get_parent(label(quick_body,"USB OTG"));
    lv_obj_t *theme_tile=lv_obj_get_parent(label(quick_body,"主题"));
    assert(lv_obj_get_width(usb_tile)==lv_obj_get_width(theme_tile) && lv_obj_get_width(theme_tile)<lv_obj_get_width(quick_body));
    host_usb.available=true;lv_obj_send_event(usb_tile,LV_EVENT_SHORT_CLICKED,NULL);assert(host_usb.enabled);
    lv_obj_send_event(usb_tile,LV_EVENT_SHORT_CLICKED,NULL);assert(!host_usb.enabled);host_usb.available=false;
    int quick_height=lv_obj_get_height(quick_body);lv_obj_set_height(quick_body,220);tick();
    scroll_from_child_gap(quick_body,100,90);screenshot("quick-gap-scroll.ppm");
    lv_obj_set_height(quick_body,quick_height);lv_obj_scroll_to_y(quick_body,0,LV_ANIM_OFF);tick();
    lv_slider_set_value(quick_slider,55,LV_ANIM_OFF);lv_obj_send_event(quick_slider,LV_EVENT_VALUE_CHANGED,NULL);
    lv_obj_send_event(quick_slider,LV_EVENT_RELEASED,NULL);assert(brightness==55 && saved.requested_brightness==55);
    saved.pending=false;saved.saved_brightness=55;saved.last_error=ESP_OK;
    tick();screenshot("quick.ppm");
    unsigned writes=saved_requests;
    lv_area_t quick_area;lv_obj_get_coords(quick_slider,&quick_area);int slider_y=(quick_area.y1+quick_area.y2)/2;
    int slider_from=quick_area.x1+(quick_area.x2-quick_area.x1)/4,slider_to=quick_area.x1+3*(quick_area.x2-quick_area.x1)/4;
    touch_step(slider_from,slider_y,true);touch_step(slider_to,slider_y,true);ouse_shell_get_state(&shell);
    assert(shell.quick_y==0 && !shell.tracking && brightness!=55 && saved_requests==writes);
    touch_step(slider_to,slider_y,false);tick();assert(saved_requests==writes+1);
    swipe(160,462,160,350);ouse_shell_get_state(&shell);assert(!shell.quick);
    // A short pull cancels; it must not leave an overlay that eats app input.
    swipe(160,14,160,42);ouse_shell_get_state(&shell);assert(!shell.quick);
    click(root,LV_SYMBOL_PLUS);tick();assert(label(root,"1"));
    touch_step(160,474,true);touch_step(160,400,true);ouse_shell_get_state(&shell);
    assert(shell.tracking && shell.bar_offset<0 && !shell.snapshot_bytes);screenshot("home-follow.ppm");
    touch_step(160,400,false);tick();ouse_shell_get_state(&shell);
    assert(!shell.recents && !shell.tracking && !shell.snapshot_bytes);
    assert(ouse_app_get_stats(&state)==ESP_OK && !state.active_id[0] && state.live_instances==1);
    click(root,LV_SYMBOL_PLUS);tick();assert(label(root,"1"));swipe(160,474,160,450);assert(label(root,"1"));
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
    assert(ouse_core_set_ui(1,100)==ESP_OK);tick();screenshot("quick-en-large.ppm");
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
    printf("PASS native settings pages, switch, live language, save failure/retry, %u lifecycle rounds\n",CORE_LIFECYCLE_ROUNDS);
    puts("PASS real pointer: follow/cancel quick sheet, slider isolation, back feedback, swipe home, hold recents, restore/swipe close/blank/Clear all");
    puts("PASS repeated animation/preview cleanup, shared themes, large text and bounded UI dispatch");
    assert(locks == unlocks + 2); // only the timed-out lock wasn't acquired.
    puts("PASS real LVGL 9.5: desktop/settings/navigation, brightness success+failure, save pending/failure/retry, system refresh, bounded UI dispatch, opaque background, CJK coverage and lifecycle round trips");
    return 0;
}
'''.replace("CHARS", ",".join(str(c) for c in chars)).replace("CORE_VERSION_EXPECTED",(ROOT/"version.txt").read_text().strip()).replace("ouse_devices_take_wifi(","host_take_wifi(").replace("CORE_LIFECYCLE_ROUNDS","200" if "--stress" in sys.argv else "24")
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
add_executable(check check.c ${UI_FONTS} "CORE/ouse_ui.c" "CORE/ouse_widgets.c" "CORE/ouse_dialog.c" "CORE/ouse_screen.c" "CORE/ouse_debug.c" "CORE/ouse_device_ui.c" "CORE/ouse_ime.c" "DEVICES/ouse_devices.c" "DEVICES/../ouse_tasks/ouse_tasks.c" "CORE/ouse_core.c" "CORE/ouse_apps.c" "CORE/ouse_shell.c" "CORE/ouse_theme.c" "CORE/ouse_navigation.cpp" "MAIN/counter_app.c")
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
    if is_app: args += ["/DOUSE_MEMORY_PRESSURE_TEST", "/experimental:c11atomics"]
    args += ["/I"+str(path) for path in (ROOT/"components").glob("*/include")]
    if not is_app: args.append("/DNDEBUG")
    if os.environ.get("OUSE_CORE_ASAN") and is_app: args += ["/fsanitize=address", "/Zi", "/FS"]
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
                        *(["/fsanitize=address", "/Zi"] if os.environ.get("OUSE_CORE_ASAN") else []),
                        "/Fe"+str(OUT/"check.exe")], cwd=OUT, env=env, capture_output=True, text=True, errors="replace")
assert result.returncode == 0, (result.stdout, result.stderr)
os.environ["PATH"] = str(compiler.parent)+os.pathsep+env["PATH"]
widget_only="--focused-widgets" in sys.argv[1:]
current_ui="--focused-current-ui" in sys.argv[1:]
focused=widget_only or current_ui or "--focused-native-ui" in sys.argv[1:]
if not focused:
    subprocess.run([str(OUT / "check.exe")], check=True, cwd=OUT, timeout=180)
    subprocess.run([str(OUT / "check.exe"), "--lifecycle-errors"], check=True, cwd=OUT, timeout=60)
    subprocess.run([str(OUT / "check.exe"), "--launch-animation"], check=True, cwd=OUT, timeout=60)
    subprocess.run([str(OUT / "check.exe"), "--launcher-pages"], check=True, cwd=OUT, timeout=60)
    subprocess.run([str(OUT / "check.exe"), "--launcher-order"], check=True, cwd=OUT, timeout=60)
    subprocess.run([str(OUT / "check.exe"), "--launcher-order-reload"], check=True, cwd=OUT, timeout=60)
    subprocess.run([str(OUT / "check.exe"), "--launcher-order-corrupt"], check=True, cwd=OUT, timeout=60)
    subprocess.run([str(OUT / "check.exe"), "--file-associations"], check=True, cwd=OUT, timeout=60)
    subprocess.run([str(OUT / "check.exe"), "--hardware-keys"], check=True, cwd=OUT, timeout=60)
    for mode in ("--boot-animation", "--boot-animation-timeout", "--boot-animation-error", "--boot-animation-low-memory"):
        subprocess.run([str(OUT / "check.exe"), mode], check=True, cwd=OUT, timeout=60)
    subprocess.run([str(OUT / "check.exe"), "--ime-cursor"], check=True, cwd=OUT, timeout=60)
    subprocess.run([str(OUT / "check.exe"), "--ble-settings"], check=True, cwd=OUT, timeout=60)
if not widget_only:subprocess.run([str(OUT / "check.exe"), "--focused-native-ui"], check=True, cwd=OUT, timeout=60)
subprocess.run([str(OUT / "check.exe"), "--memory-pressure"], check=True, cwd=OUT, timeout=60)
if widget_only or current_ui or not focused:subprocess.run([str(OUT / "check.exe"), "--focused-widgets"], check=True, cwd=OUT, timeout=60)
from PIL import Image
previews=() if widget_only else ("wifi-modal-en-large", "recents-font130", "native-power-compact", "native-timeout-choices", "native-sound-compact", "native-appearance-light", "native-appearance-dark")
if widget_only or current_ui or not focused:previews+=("widgets-switch-current","widgets-switch-dark","widgets-switch-light","widgets-switch-dark-hdpi","widgets-switch-light-hdpi")
if not focused:
    previews+=("boot-animation", "desktop", "settings", "settings-gap-scroll", "display", "system", "settings-dark", "settings-text-large", "back-follow", "quick-follow", "quick-gap-scroll", "home-follow", "quick", "recents", "recents-second", "desktop-dark", "quick-dark", "quick-en-large", "desktop-light", "desktop-page2", "save-error", "power", "audio", "wifi", "wifi-password", "wifi-form-gap-scroll")
for name in previews:
    with Image.open(OUT / (name+".ppm")) as image:
        image.save(OUT / (name+".png"))

print("PASS native core compiled against real LVGL; firmware/device not built or accessed")
print(f"Preview: {OUT / ('widgets-switch-light.png' if widget_only else 'recents-font130.png' if focused else 'desktop.png')}")
