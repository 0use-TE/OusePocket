"""Focused native service checks; no device, serial, Internet or stress loop."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.tools/ouse-services-check'
OUT.mkdir(parents=True, exist_ok=True)
setup = ROOT / '.tools/ouse-kernel-check/environment.cmd'
assert setup.exists(), 'Run test-ouse-kernel.py first'
env = dict(os.environ)
for line in subprocess.check_output(['cmd.exe', '/d', '/c', str(setup)], encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key, value = line.split('=', 1)
        env[key] = value
compiler = Path(env['VCToolsInstallDir']) / 'bin/Hostx64/x64/cl.exe'
headers = {
    'esp_err.h': '''#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT 0x107
const char *esp_err_to_name(int error);
''',
    'platform.h': '''#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <setjmp.h>
#include <assert.h>
typedef int portMUX_TYPE;
typedef int BaseType_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *StreamBufferHandle_t;
typedef void *TaskHandle_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(v) (v)
#define configASSERT(v) assert(v)
extern int64_t test_clock;
extern bool break_on_empty;
extern jmp_buf worker_exit;
extern void (*platform_hook)(void);
QueueHandle_t xQueueCreate(unsigned capacity,unsigned item_size);
int xQueueSend(QueueHandle_t queue,const void *item,unsigned wait);
int xQueueReceive(QueueHandle_t queue,void *item,unsigned wait);
void vQueueDelete(QueueHandle_t queue);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
int xSemaphoreTake(SemaphoreHandle_t mutex,unsigned wait);
int xSemaphoreGive(SemaphoreHandle_t mutex);
void vSemaphoreDelete(SemaphoreHandle_t mutex);
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *task);
void vTaskDelay(unsigned ticks);
StreamBufferHandle_t xStreamBufferCreate(unsigned size,unsigned trigger);
size_t xStreamBufferSend(StreamBufferHandle_t buffer,const void *data,size_t size,unsigned wait);
size_t xStreamBufferReceive(StreamBufferHandle_t buffer,void *data,size_t size,unsigned wait);
size_t xStreamBufferBytesAvailable(StreamBufferHandle_t buffer);
int xStreamBufferReset(StreamBufferHandle_t buffer);
void vStreamBufferDelete(StreamBufferHandle_t buffer);
int64_t esp_timer_get_time(void);
''',
    'ouse_storage.h': '#pragma once\n#define OUSE_STORAGE_BASE "alarm-store"\n',
    'esp_rom_crc.h': '#pragma once\n#include <stdint.h>\n#include <stddef.h>\nuint32_t esp_rom_crc32_le(uint32_t crc,const unsigned char *data,size_t size);\n',
    'unistd.h': '#pragma once\n#include <direct.h>\n#include <io.h>\n#define fsync _commit\n#define fileno _fileno\n#define unlink _unlink\n#define mkdir(path,mode) _mkdir(path)\n',
    'strings.h': '#pragma once\n#include <string.h>\n#define strcasecmp _stricmp\n',
    'lwip/sockets.h': '''#pragma once
typedef struct {int unused;} fd_set;
typedef int socklen_t;
struct timeval {long tv_sec,tv_usec;};
#define FD_ZERO(p) ((void)(p))
#define FD_SET(fd,p) ((void)(fd),(void)(p))
#define SOL_SOCKET 1
#define SO_ERROR 2
int select(int count,void *reads,fd_set *writes,void *errors,struct timeval *timeout);
int getsockopt(int fd,int level,int option,void *value,socklen_t *size);
''',
    'esp_tls.h': '''#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#define ESP_TLS_ERR_SSL_WANT_READ -0x6900
#define ESP_TLS_ERR_SSL_WANT_WRITE -0x6880
typedef struct {int unused;} esp_tls_t;
typedef struct {bool non_block,is_plain_tcp,skip_common_name;int timeout_ms;int (*crt_bundle_attach)(void *);} esp_tls_cfg_t;
esp_tls_t *esp_tls_init(void);
int esp_tls_conn_new_async(const char *host,int size,int port,const esp_tls_cfg_t *cfg,esp_tls_t *tls);
int esp_tls_get_conn_sockfd(esp_tls_t *tls,int *fd);
int esp_tls_conn_destroy(esp_tls_t *tls);
int esp_tls_conn_read(esp_tls_t *tls,void *data,size_t size);
int esp_tls_conn_write(esp_tls_t *tls,const void *data,size_t size);
''',
    'esp_crt_bundle.h': '#pragma once\nint esp_crt_bundle_attach(void *config);\n',
}
for name in ['freertos/FreeRTOS.h', 'freertos/task.h', 'freertos/queue.h', 'freertos/semphr.h', 'freertos/stream_buffer.h', 'esp_timer.h']:
    headers[name] = '#include "platform.h"\n'
for name, content in headers.items():
    path = OUT / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding='utf-8')

includes = [OUT, ROOT/'tools/checks', ROOT/'components/ouse_tasks/include', ROOT/'components/ouse_devices/include',
            ROOT/'components/ouse_alarm/include', ROOT/'components/ouse_media/include', ROOT/'components/ouse_net/include',
            ROOT/'components/ouse_core/include', Path('D:/esp/v6.1/esp-idf/components/http_parser')]
common = [str(compiler), '/nologo', '/std:c11', '/O2', '/MD', '/utf-8', '/D_CRT_SECURE_NO_WARNINGS', '/D_CRT_NONSTDC_NO_DEPRECATE']
common += ['/I'+str(p) for p in includes]
platform = ROOT/'tools/checks/services-platform.c'
registry = ROOT/'components/ouse_tasks/ouse_tasks.c'
devices = ROOT/'components/ouse_devices/ouse_devices.c'
cases = {
    'http': [ROOT/'components/ouse_net/ouse_http.c', Path('D:/esp/v6.1/esp-idf/components/http_parser/http_parser.c')],
    'wave': [ROOT/'components/ouse_media/ouse_wave.c', ROOT/'components/ouse_alarm/ouse_alarm_schedule.c'],
    'alarm': [registry, devices, ROOT/'components/ouse_alarm/ouse_alarm_schedule.c'],
    'media': [registry, devices, ROOT/'components/ouse_media/ouse_wave.c'],
    'net': [registry],
}
for name, sources in cases.items():
    if sys.argv[1:] and name not in sys.argv[1:]:
        continue
    fixture = (ROOT/f'tools/checks/services-{name}.c').read_text(encoding='utf-8')
    for key, relative in [('ALARM_SOURCE', 'components/ouse_alarm/ouse_alarm.c'),
                          ('MEDIA_SOURCE', 'components/ouse_media/ouse_media.c'),
                          ('NET_SOURCE', 'components/ouse_net/ouse_net.c')]:
        fixture = fixture.replace(key, (ROOT/relative).as_posix())
    path = OUT/f'{name}-check.c'
    path.write_text(fixture, encoding='utf-8')
    result = subprocess.run(common+list(map(str, [platform, path]+sources))+['/Fe'+str(OUT/f'{name}.exe')],
                            cwd=OUT, env=env, capture_output=True, text=True, errors='replace')
    assert result.returncode == 0, (name, result.stdout, result.stderr)
    subprocess.run([str(OUT/f'{name}.exe')], cwd=OUT, check=True, timeout=15)
