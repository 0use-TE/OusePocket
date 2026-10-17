"""Host checks for the production registry and QuickJS PSRAM allocator."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.tools/ouse-psram-check'
OUT.mkdir(parents=True, exist_ok=True)
HOST = ROOT / '.tools/ouse-core-check'
QJS = ROOT / 'managed_components/espressif__quickjs-ng/quickjs-ng'
registry = (ROOT / 'components/ouse_core/ouse_apps.c').read_text(encoding='utf-8')
runtime = (ROOT / 'components/ouse_runtime/ouse_runtime.c').read_text(encoding='utf-8')

def section(source, start, end):
    return source[source.index(start):source.index(end, source.index(start))]

(OUT / 'esp_attr.h').write_text('#pragma once\n#define EXT_RAM_BSS_ATTR\n')
(OUT / 'esp_heap_caps.h').write_text(r'''
#pragma once
#include <assert.h>
#include <stdlib.h>
#include <malloc.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static int fail_allocations;
static unsigned allocation_calls;
static void *heap_caps_malloc(size_t size,int caps)
{assert(caps==3);++allocation_calls;return fail_allocations?NULL:malloc(size);}
static void *heap_caps_calloc(size_t count,size_t size,int caps)
{assert(caps==3);++allocation_calls;return fail_allocations?NULL:calloc(count,size);}
static void *heap_caps_realloc(void *pointer,size_t size,int caps)
{assert(caps==3);++allocation_calls;return fail_allocations?NULL:realloc(pointer,size);}
static size_t heap_caps_get_allocated_size(void *pointer){return _msize(pointer);}
''', encoding='utf-8')
fixture = r'''
#include "ouse_app.h"
#include "ouse_memory.h"
#include "quickjs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define ESP_LOGI(...) ((void)0)
void lv_obj_add_flag(lv_obj_t *object,lv_obj_flag_t flag){(void)object;(void)flag;}
/* REGISTRY */
/* REGISTER */
/* CHECK */
/* ALLOCATOR */
static esp_err_t open_app(lv_obj_t *root,void *context){(void)root;(void)context;return ESP_OK;}
static void close_app(void *context){(void)context;}
static void changed(bool active){(void)active;}
int main(void)
{
    lv_obj_t *host=(lv_obj_t *)1;
    fail_allocations=1;
    assert(ouse_apps_init(host,changed)==ESP_ERR_NO_MEM && !apps.host);
    assert(!js_psram_malloc(NULL,16) && !js_psram_calloc(NULL,2,16));
    assert(!JS_NewRuntime2(&js_psram_allocator,NULL));
    fail_allocations=0;
    assert(ouse_apps_init(host,changed)==ESP_OK);
    assert(OUSE_APP_CAPACITY==24);
    for(unsigned index=0;index<OUSE_APP_CAPACITY;++index){
        char id[48];snprintf(id,sizeof(id),"user.app%u",index);
        ouse_app_definition_t definition={.id=id,.name="App",.icon="A",.open=open_app,.close=close_app};
        assert(ouse_apps_register(&definition)==ESP_OK);
    }
    ouse_apps_seal();
    assert(ouse_apps_user_check("user.overflow")==ESP_ERR_NO_MEM);
    assert(ouse_apps_user_check("user.app23")==ESP_OK);
    assert(ouse_apps_user_check("user.BAD")==ESP_ERR_INVALID_ARG);
    assert(strlen("user.")+42==47);
    assert((OUSE_APP_CAPACITY-1)*48+1<=1200);
    void *memory=js_psram_malloc(NULL,32);assert(memory);
    memset(memory,42,32);fail_allocations=1;
    assert(!js_psram_realloc(NULL,memory,64) && ((unsigned char *)memory)[0]==42);
    fail_allocations=0;memory=js_psram_realloc(NULL,memory,128);assert(memory);
    assert(js_psram_usable_size(memory)>=128);js_psram_free(NULL,memory);
    JSRuntime *runtime=JS_NewRuntime2(&js_psram_allocator,NULL);assert(runtime);
    JS_SetMemoryLimit(runtime,512*1024);
    JSContext *context=JS_NewContext(runtime);assert(context);
    const char *script="let values=[];for(let index=0;index<500;index++)values.push({text:'PSRAM'+index});values.length";
    JSValue result=JS_Eval(context,script,strlen(script),"test",JS_EVAL_TYPE_GLOBAL);
    int count=0;assert(!JS_IsException(result) && JS_ToInt32(context,&count,result)==0 && count==500);
    JS_FreeValue(context,result);
    script="new Array(1000000).fill('full')";
    result=JS_Eval(context,script,strlen(script),"limit",JS_EVAL_TYPE_GLOBAL);
    assert(JS_IsException(result));JS_FreeValue(context,JS_GetException(context));
    JS_FreeContext(context);JS_FreeRuntime(runtime);free(apps.entries);
    assert(allocation_calls>500);
    puts("PASS 24 slots, overflow/update admission, PSRAM-only allocation, OOM, realloc preservation, real QuickJS execution and heap limit");
}
'''
fixture = fixture.replace('/* REGISTRY */', section(registry, 'typedef struct {', 'static void resolve_info('))
fixture = fixture.replace('/* REGISTER */', section(registry, 'esp_err_t ouse_apps_init(', 'static void free_preview('))
fixture = fixture.replace('/* CHECK */', section(registry, 'esp_err_t ouse_apps_user_check(', 'esp_err_t ouse_apps_user_apply('))
fixture = fixture.replace('/* ALLOCATOR */', section(runtime, 'static void *js_psram_calloc(', 'typedef enum { NAV_IDLE'))
(OUT / 'check.c').write_text(fixture, encoding='utf-8')
env = dict(os.environ)
for line in subprocess.check_output(['cmd.exe', '/d', '/c', str(ROOT / '.tools/ouse-kernel-check/environment.cmd')], encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key, value = line.split('=', 1)
        env[key] = value
compiler = Path(env['VCToolsInstallDir']) / 'bin/Hostx64/x64/cl.exe'
includes = [OUT, HOST, ROOT / 'managed_components/lvgl__lvgl', QJS, ROOT / 'components/ouse_core/include']
command = [str(compiler), '/nologo', '/std:c11', '/O2', '/MD', '/utf-8', '/DESP_PLATFORM', '/D_GNU_SOURCE', '/DQUICKJS_NG_BUILD', '/DLV_KCONFIG_IGNORE', '/DLV_LVGL_H_INCLUDE_SIMPLE', '/DLV_CONF_PATH="' + (HOST / 'lv_conf.h').as_posix() + '"']
command += ['/I' + str(path) for path in includes]
command += [str(OUT / 'check.c'), str(ROOT / '.tools/ouse-runtime-check/quickjs-native.lib'), '/Fe' + str(OUT / 'check.exe')]
result = subprocess.run(command, cwd=OUT, env=env, capture_output=True, text=True, errors='replace')
assert result.returncode == 0, (result.stdout, result.stderr)
subprocess.run([str(OUT / 'check.exe')], cwd=OUT, check=True, timeout=30)
