"""Compile the real system backend with deterministic SDK/RTOS test doubles."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".tools/ouse-system-check"
OUT.mkdir(parents=True, exist_ok=True)
headers = r'''
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <setjmp.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ 400
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_SPIRAM 4
#define NVS_READWRITE 1
#define NVS_READONLY 0
typedef int nvs_handle_t;
typedef int portMUX_TYPE;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0xffffffff
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
esp_err_t esp_flash_get_size(void *, uint32_t *);
size_t heap_caps_get_free_size(unsigned);
size_t heap_caps_get_total_size(unsigned);
int esp_reset_reason(void);
int64_t esp_timer_get_time(void);
const char *esp_get_idf_version(void);
const char *esp_err_to_name(esp_err_t);
esp_err_t nvs_flash_init(void);
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
esp_err_t nvs_get_u8(nvs_handle_t, const char *, uint8_t *);
esp_err_t nvs_set_u8(nvs_handle_t, const char *, uint8_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
QueueHandle_t xQueueCreate(unsigned, unsigned);
int xQueueReceive(QueueHandle_t, void *, unsigned);
int xQueueOverwrite(QueueHandle_t, const void *);
void vQueueDelete(QueueHandle_t);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t, unsigned);
void xSemaphoreGive(SemaphoreHandle_t);
void vSemaphoreDelete(SemaphoreHandle_t);
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, void *);
'''
(OUT / "sdk.h").write_text(headers)
for name in ["esp_err.h", "esp_flash.h", "esp_heap_caps.h", "esp_log.h", "esp_system.h",
             "esp_timer.h", "nvs.h", "nvs_flash.h", "sdkconfig.h", "freertos/FreeRTOS.h",
             "freertos/task.h", "freertos/queue.h", "freertos/semphr.h"]:
    p = OUT / name
    p.parent.mkdir(exist_ok=True)
    p.write_text('#include "sdk.h"\n')
fixture = r'''
#include "ouse_system.h"
#include "sdk.h"
#include <stdio.h>
static void (*worker)(void *);
static unsigned item_size, commits, queue_items;
static uint8_t queue_data[32], disk=65, staged, disk_theme=1, staged_theme;
static int init_error, commit_error, task_fail;
static bool inject, check_pending;
static jmp_buf finished;
esp_err_t esp_flash_get_size(void *chip, uint32_t *size) { (void)chip; *size=16*1024*1024; return ESP_OK; }
size_t heap_caps_get_free_size(unsigned caps) { return caps & MALLOC_CAP_SPIRAM ? 30*1024*1024 : 400*1024; }
size_t heap_caps_get_total_size(unsigned caps) { return caps & MALLOC_CAP_SPIRAM ? 32*1024*1024 : 500*1024; }
int esp_reset_reason(void) { return 1; }
int64_t esp_timer_get_time(void) { return 123000000; }
const char *esp_get_idf_version(void) { return "v6.1"; }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "test error"; }
esp_err_t nvs_flash_init(void) { return init_error; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *h) {
    (void)mode; assert(!strcmp(name,"ouse_core")); *h=1; return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *v) {
    (void)h; if (!strcmp(key,"theme")) { *v=disk_theme; return ESP_OK; }
    assert(!strcmp(key,"brightness")); *v=disk; return disk ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t v) {
    (void)h; if (!strcmp(key,"theme")) staged_theme=v;
    else { assert(!strcmp(key,"brightness")); staged=v; } return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    (void)h; ++commits;
    if (inject) { inject=false; assert(ouse_system_request_brightness(90)==ESP_OK); check_pending=true; }
    if (commit_error) return commit_error;
    disk=staged; disk_theme=staged_theme; return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }
QueueHandle_t xQueueCreate(unsigned n, unsigned size) { assert(n==1 && size<=sizeof(queue_data)); item_size=size; return (void *)1; }
int xQueueReceive(QueueHandle_t q, void *out, unsigned wait) {
    (void)q; (void)wait;
    if (check_pending) {
        ouse_settings_state_t s; assert(ouse_system_get_settings_state(&s)==ESP_OK);
        assert(s.pending && s.requested_brightness==90); check_pending=false;
    }
    if (!queue_items) longjmp(finished,1);
    memcpy(out,queue_data,item_size); queue_items=0; return pdTRUE;
}
int xQueueOverwrite(QueueHandle_t q, const void *in) { (void)q; memcpy(queue_data,in,item_size); queue_items=1; return pdTRUE; }
void vQueueDelete(QueueHandle_t q) { (void)q; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)2; }
int xSemaphoreTake(SemaphoreHandle_t s, unsigned wait) { (void)s; (void)wait; return pdTRUE; }
void xSemaphoreGive(SemaphoreHandle_t s) { (void)s; }
void vSemaphoreDelete(SemaphoreHandle_t s) { (void)s; }
int xTaskCreate(void (*f)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle) {
    (void)name; (void)stack; (void)arg; (void)priority; (void)handle; worker=f; return task_fail ? 0 : pdPASS;
}
static void drain(void) { if (!setjmp(finished)) worker(NULL); }
int main(int argc, char **argv) {
    assert(argc==2);
    ouse_settings_state_t s; ouse_system_info_t info;
    assert(ouse_system_get_info(&info)==ESP_ERR_INVALID_STATE);
    assert(ouse_system_get_info(NULL)==ESP_ERR_INVALID_ARG);
    assert(ouse_system_init(NULL)==ESP_ERR_INVALID_ARG);
    uint8_t brightness=0; assert(ouse_system_init(&brightness)==ESP_ERR_INVALID_ARG);
    brightness=45;
    if (!strcmp(argv[1],"nvs-fail")) init_error=ESP_FAIL;
    if (!strcmp(argv[1],"task-fail")) task_fail=1;
    if (!strcmp(argv[1],"missing")) disk=0;
    if (!strcmp(argv[1],"invalid")) disk=255;
    esp_err_t result=ouse_system_init(&brightness);
    assert(ouse_system_init(&brightness)==ESP_ERR_INVALID_STATE);
    assert(ouse_system_get_info(&info)==ESP_OK);
    assert(info.cpu_mhz==400 && info.flash_bytes==16*1024*1024 && info.uptime_ms==123000);
    if (init_error || task_fail) {
        assert(result!=ESP_OK);
        assert(ouse_system_request_brightness(75)==ESP_ERR_INVALID_STATE);
        assert(ouse_system_get_settings_state(&s)==ESP_OK && s.last_error!=ESP_OK);
        assert(!commits); return 0;
    }
    assert(result==ESP_OK && brightness==(disk==65 ? 65 : 45));
    assert(ouse_system_get_settings_state(&s)==ESP_OK && s.requested_theme==1);
    assert(ouse_system_request_theme(2)==ESP_ERR_INVALID_ARG);
    assert(ouse_system_request_brightness(0)==ESP_ERR_INVALID_ARG);
    assert(ouse_system_request_brightness(75)==ESP_OK && !commits);
    assert(ouse_system_get_settings_state(&s)==ESP_OK && s.pending);
    drain(); assert(disk==75 && commits==1);
    assert(ouse_system_get_settings_state(&s)==ESP_OK && !s.pending && s.saved_brightness==75);
    assert(ouse_system_request_brightness(75)==ESP_OK && !queue_items); // no redundant flash write
    commit_error=ESP_FAIL;
    assert(ouse_system_request_brightness(85)==ESP_OK); drain();
    assert(disk==75 && ouse_system_get_settings_state(&s)==ESP_OK && s.last_error==ESP_FAIL && !s.pending);
    commit_error=0; assert(ouse_system_request_brightness(85)==ESP_OK); drain(); assert(disk==85);
    unsigned before=commits;
    assert(ouse_system_request_brightness(70)==ESP_OK);
    assert(ouse_system_request_brightness(80)==ESP_OK); drain(); assert(disk==80 && commits==before+1);
    inject=true; assert(ouse_system_request_brightness(75)==ESP_OK); drain();
    assert(disk==90 && ouse_system_get_settings_state(&s)==ESP_OK && !s.pending && s.saved_brightness==90);
    assert(ouse_system_request_settings(0,0)==ESP_ERR_INVALID_ARG);
    assert(ouse_system_request_settings(60,2)==ESP_ERR_INVALID_ARG);
    assert(ouse_system_request_settings(55,0)==ESP_OK);
    assert(ouse_system_request_brightness(60)==ESP_OK); drain();
    assert(disk==60 && disk_theme==0);
    assert(ouse_system_get_settings_state(&s)==ESP_OK && s.saved_theme==0 && s.saved_brightness==60);
    puts("PASS persistence backend: boot restore, deferred commit, failure/retry, coalescing, stale completion");
    return 0;
}
'''
(OUT / "check.c").write_text(fixture)
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
system = ROOT / "components/ouse_system"
subprocess.run([str(compiler), "/nologo", "/utf-8", "/std:c11", "/UNDEBUG", "/I"+str(OUT),
    "/I"+str(system/"include"), str(system/"ouse_system.c"), str(OUT/"check.c"),
    "/Fe"+str(OUT/"check.exe")], cwd=OUT, env=env, check=True)
for case in ["normal", "missing", "invalid", "nvs-fail", "task-fail"]:
    subprocess.run([str(OUT/"check.exe"), case], check=True)
print("PASS real backend compiled; NVS/task failure degrades without erase or desktop dependency")
