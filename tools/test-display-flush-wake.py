"""Exercise the production DMA wake implementation with deterministic RTOS events."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".tools/display-wake-check"
OUT.mkdir(parents=True, exist_ok=True)
def put(name, data):
    p = OUT / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(data, encoding="utf-8")

put("lvgl.h", """#pragma once
#include <stdbool.h>
#define LVGL_VERSION_MAJOR 9
#define LVGL_VERSION_MINOR 5
#define LVGL_VERSION_PATCH 0
typedef struct lv_display { volatile int flushing; void (*flush_wait_cb)(struct lv_display *); } lv_display_t;
void lv_display_set_flush_wait_cb(lv_display_t *, void (*)(lv_display_t *));
""")
put("src/display/lv_display_private.h", '#include "lvgl.h"\n')
put("esp_attr.h", "#define IRAM_ATTR\n")
put("freertos/FreeRTOS.h", """#pragma once
typedef unsigned TickType_t;
typedef int BaseType_t;
#define pdFALSE 0
#define portMAX_DELAY (~0u)
int xPortInIsrContext(void);
void test_yield(void);
#define portYIELD_FROM_ISR() test_yield()
""")
put("freertos/task.h", '#include "FreeRTOS.h"\nvoid vTaskDelay(TickType_t);\n')
put("freertos/semphr.h", """#include "FreeRTOS.h"
typedef int *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateBinary(void);
int xSemaphoreTake(SemaphoreHandle_t, TickType_t);
int xSemaphoreGive(SemaphoreHandle_t);
int xSemaphoreGiveFromISR(SemaphoreHandle_t, BaseType_t *);
""")
put("bsp/display.h", "#define BSP_LCD_H_RES 320\n#define BSP_LCD_V_RES 480\n")
put("esp_lv_adapter.h", """#include "lvgl.h"
#define ESP_LV_ADAPTER_PANEL_IF_OTHER 1
#define ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE 0
typedef struct { struct {int interface,hor_res,ver_res,buffer_height;bool use_psram,require_double_buffer;} profile;int tear_avoid_mode;} esp_lv_adapter_display_config_t;
typedef struct lv_indev { int unused; } lv_indev_t;
#define ESP_LV_ADAPTER_TOUCH_MODE_MULTI_CONTROL 2
typedef struct { struct { int mode,pointers; } multi_touch; } esp_lv_adapter_touch_config_t;
""")
put("esp_log.h", "#define ESP_LOGI(...) ((void)0)\n")
put("esp_timer.h", "#include <stdint.h>\n")
source = (ROOT / "boards/waveshare_lcd35/display_flush_wake.c").as_posix()
put("test.c", '#include "' + source + '"\n#include "' + (ROOT / 'boards/waveshare_lcd35/board_display.c').as_posix() + '"\n' + r'''
#include <assert.h>
#include <stdio.h>
static int token,fail_alloc,isr,blocked,takes,gives,yields,delays,allocs;
static int scenario;
static lv_display_t display,other;
lv_indev_t *__real_esp_lv_adapter_register_touch(const esp_lv_adapter_touch_config_t *c){(void)c;return NULL;}
void lv_display_set_flush_wait_cb(lv_display_t *d,void (*cb)(lv_display_t *)){d->flush_wait_cb=cb;}
lv_display_t *__real_esp_lv_adapter_register_display(const esp_lv_adapter_display_config_t *c){
    assert(c->profile.buffer_height==40 && !c->profile.use_psram && c->profile.require_double_buffer);
    assert(!display.flush_wait_cb);return &display;
}
SemaphoreHandle_t xSemaphoreCreateBinary(void) { ++allocs; return fail_alloc?NULL:&token; }
int xPortInIsrContext(void) { return isr; }
void test_yield(void) { ++yields; }
void __real_lv_display_flush_ready(lv_display_t *d) { d->flushing=0; }
int xSemaphoreGive(SemaphoreHandle_t s) { assert(s==&token);token=1;++gives;return 1; }
int xSemaphoreGiveFromISR(SemaphoreHandle_t s,BaseType_t *w) {
    *w=blocked;return xSemaphoreGive(s);
}
void vTaskDelay(TickType_t t) { assert(t==1);++delays;display.flushing=0;other.flushing=0; }
int xSemaphoreTake(SemaphoreHandle_t s,TickType_t t) {
    assert(s==&token && t==portMAX_DELAY);++takes;
    if(token) { token=0;return 1; }
    assert(display.flushing);
    // 1: completion just before blocking; 2: completion after blocking.
    // 3: spurious wake must not allow return with DMA still active.
    if(scenario==3) { scenario=2;return 1; }
    blocked=scenario==2;isr=1;
    __wrap_lv_display_flush_ready(&display);
    isr=0;blocked=0;
    assert(token);token=0;return 1;
}
int main(void) {
    fail_alloc=1;assert(!ouse_display_flush_wake_init(&display));
    display.flushing=1;ouse_display_flush_wait(&display);assert(delays==1);
    fail_alloc=0;assert(ouse_display_flush_wake_init(&display));
    assert(ouse_display_flush_wake_init(&display) && allocs==2);
    assert(!ouse_display_flush_wake_init(&other));
    int before=takes;ouse_display_flush_wait(&display);assert(takes==before);
    for(int mode=1;mode<=3;++mode) {
        scenario=mode;display.flushing=1;ouse_display_flush_wait(&display);
        assert(!display.flushing && !token);
    }
    assert(yields==2 && delays==1);
    // Inline/task completion leaves a token; consume it, then wait for NEW DMA.
    __wrap_lv_display_flush_ready(&display);assert(token);
    scenario=2;before=takes;display.flushing=1;ouse_display_flush_wait(&display);
    assert(takes==before+2 && !display.flushing && !token);
    before=gives;other.flushing=1;isr=1;__wrap_lv_display_flush_ready(&other);isr=0;
    assert(!other.flushing && gives==before);
    other.flushing=1;ouse_display_flush_wait(&other);assert(delays==2);
    esp_lv_adapter_display_config_t config={.profile={.interface=ESP_LV_ADAPTER_PANEL_IF_OTHER,.hor_res=320,.ver_res=480},.tear_avoid_mode=ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE};
    assert(__wrap_esp_lv_adapter_register_display(&config)==&display);
    // Simulate the BSP starting its worker immediately after registration.
    assert(display.flush_wait_cb==ouse_display_flush_wait);
    scenario=2;display.flushing=1;display.flush_wait_cb(&display);assert(!display.flushing);
    puts("PASS initial BSP screen flush already has the safe DMA wait before worker startup");
    puts("PASS production DMA wake: ISR/task completion, early completion, stale/spurious wake, allocation fallback, other display");
}
''')
vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
vs = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires",
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
put("env.cmd", f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n')
env = dict(os.environ)
for line in subprocess.check_output(["cmd.exe", "/d", "/c", str(OUT/"env.cmd")], encoding="mbcs").splitlines():
    if "=" in line and not line.startswith("="):
        k,v=line.split("=",1);env[k]=v
compiler = Path(env["VCToolsInstallDir"]) / "bin/Hostx64/x64/cl.exe"
subprocess.run([str(compiler), "/nologo", "/std:c11", "/W4", "/WX", "/I"+str(OUT),
    "/I"+str(ROOT/"boards/waveshare_lcd35"), str(OUT/"test.c"), "/Fe:"+str(OUT/"test.exe")],
    cwd=OUT, env=env, check=True)
subprocess.run([str(OUT/"test.exe")], check=True)
