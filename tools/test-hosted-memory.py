"""Exercise the product-generated Hosted memory-pressure paths after configuration."""
import os
import re
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/hosted-memory-check'
OUT.mkdir(parents=True,exist_ok=True)
generated=ROOT/'build/esp-idf/main/ouse_hosted_memory'
sdio=(generated/'sdio_drv.c').read_text(encoding='utf-8')
pool=(generated/'mempool.c').read_text(encoding='utf-8')
start=sdio.rindex('static esp_err_t sdio_push_data_to_queue(')
rx=sdio[start:sdio.index('\n#endif',start)]
free=pool[pool.index('void mempool_free('):]
start=sdio.rindex('static uint8_t * sdio_rx_get_buffer(')
get_buffer=sdio[start:sdio.index('\n// this frees',start)]
assert all(text not in sdio for text in ('assert(pkt_rxbuff)','assert(sendbuf)','assert(*buf)','assert(rxbuff)','assert(copy_payload)'))
gate=re.sub(r'^#include[^\n]*\n','',(ROOT/'boards/waveshare_lcd35/board_sd_bus.c').read_text(encoding='utf-8'),flags=re.MULTILINE)
vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
installation=subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
environment=OUT/'environment.cmd'
environment.write_text(f'@echo off\ncall "{installation}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n',encoding='mbcs')
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(environment)],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1)
        env[key]=value
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
fixture=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#define H_USE_MEMPOOL 1
#define H_MEM_STATS 0
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGV(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 257
#define ESP_ERR_INVALID_STATE 259
#define ESP_ERR_INVALID_SIZE 260
#define portMAX_DELAY 0xffffffffU
#define configASSERT(value) assert(value)
typedef struct {struct {size_t capacity,sector_size;} csd;} sdmmc_card_t;
typedef int sdmmc_command_t,sdmmc_host_t;
typedef struct {unsigned depth;} StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
static SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t *storage){storage->depth=0;return storage;}
static void xSemaphoreTakeRecursive(SemaphoreHandle_t semaphore,unsigned timeout){assert(timeout==portMAX_DELAY);++semaphore->depth;}
static void xSemaphoreGiveRecursive(SemaphoreHandle_t semaphore){assert(semaphore->depth);--semaphore->depth;}
#define MEMSET_REQUIRED 1
#define HOSTED_MEM_ALIGNMENT_64 64
typedef int esp_err_t;
struct mempool_entry {struct mempool_entry *next;};
struct mempool {struct {struct mempool_entry *first;} head;void *spinlock;unsigned free_count;};
#define SLIST_FOREACH(entry,head,entries) for(entry=(head)->first;entry;entry=entry->next)
#define SLIST_INSERT_HEAD(head,entry,entries) do {(entry)->next=(head)->first;(head)->first=(entry);} while(0)
static unsigned locks,freed,sleeps,allocation_calls,queues;
static int fail_count;
static bool invalid;
static void lock(void *unused){(void)unused;assert(!locks);++locks;}
static void unlock(void *unused){(void)unused;assert(locks==1);--locks;}
static void release(void *pointer){assert(!locks);++freed;free(pointer);}
static unsigned sleep_ms(unsigned duration){assert(duration==5);++sleeps;return 0;}
typedef struct {void (*_h_lock_mempool)(void *);void (*_h_unlock_mempool)(void *);void (*_h_free_align)(void *);unsigned (*_h_msleep)(unsigned);void *(*_h_malloc_align)(size_t,size_t);} functions_t;
static struct {functions_t *funcs;} g_h;
static struct {int write_index;struct {uint8_t *buf;size_t buf_size;} buffer[2];} double_buf;
static void *aligned(size_t bytes,size_t alignment){assert(alignment==64);if(fail_count)return NULL;return malloc(bytes);}
static void *sdio_buffer_alloc(unsigned zero){(void)zero;++allocation_calls;if(fail_count){if(fail_count>0)--fail_count;return NULL;}return calloc(1,32);}
static void sdio_buffer_free(void *pointer){release(pointer);}
static bool is_valid_sdio_rx_packet(uint8_t *buffer,uint16_t *length,uint16_t *offset){(void)buffer;*length=8;*offset=8;return !invalid;}
static int sdio_push_pkt_to_queue(void *pointer,unsigned length,unsigned offset){assert(length==8 && offset==8);++queues;release(pointer);return 0;}
/* RX */
/* GET */
/* FREE */
/* GATE */
esp_err_t __real_sdmmc_host_do_transaction(int slot,sdmmc_command_t *command){(void)slot;(void)command;assert(bus->depth==2);return ESP_OK;}
static unsigned single_writes;
esp_err_t __real_sdmmc_write_sectors(sdmmc_card_t *card,const void *source,size_t start,size_t count){(void)source;assert(count==1);assert(bus->depth==1);assert(__wrap_sdmmc_host_do_transaction(0,NULL)==ESP_OK);assert(__wrap_sdmmc_host_do_transaction(0,NULL)==ESP_OK);if(card){assert(start==single_writes);++single_writes;return ESP_OK;}return ESP_FAIL;}
esp_err_t __real_sdmmc_read_sectors(sdmmc_card_t *card,void *destination,size_t start,size_t count){(void)card;(void)destination;(void)start;(void)count;assert(bus->depth==1);return __wrap_sdmmc_host_do_transaction(0,NULL);}
esp_err_t __real_sdmmc_card_init(const sdmmc_host_t *host,sdmmc_card_t *card){(void)host;(void)card;assert(bus->depth==1);return __wrap_sdmmc_host_do_transaction(0,NULL);}
int main(void)
{
    static functions_t funcs={lock,unlock,release,sleep_ms,aligned};g_h.funcs=&funcs;
    assert(sdio_rx_get_buffer(8) && double_buf.buffer[0].buf_size==8);
    fail_count=-1;assert(!sdio_rx_get_buffer(64) && !double_buf.buffer[0].buf && !double_buf.buffer[0].buf_size);
    fail_count=0;assert(sdio_rx_get_buffer(32) && double_buf.buffer[0].buf_size==32);release(double_buf.buffer[0].buf);
    uint8_t bytes[32]={0};
    fail_count=-1;assert(sdio_push_data_to_queue(bytes,16)==ESP_ERR_NO_MEM && sleeps==20 && queues==0 && allocation_calls==21);
    fail_count=2;sleeps=allocation_calls=0;assert(sdio_push_data_to_queue(bytes,32)==ESP_OK && sleeps==2 && queues==2);
    unsigned before=freed;assert(sdio_push_data_to_queue(bytes,8)==ESP_FAIL && freed==before+1);
    invalid=true;before=allocation_calls;assert(sdio_push_data_to_queue(bytes,16)==ESP_FAIL && allocation_calls==before);invalid=false;
    struct mempool pool={0};before=freed;
    for(unsigned count=0;count<16;++count)mempool_free(&pool,calloc(1,32));
    assert(pool.free_count==16 && freed==before+12 && !locks);
    unsigned cached=0;while(pool.head.first){struct mempool_entry *entry=pool.head.first;pool.head.first=entry->next;free(entry);++cached;}assert(cached==4);
    assert(__wrap_sdmmc_write_sectors(NULL,NULL,0,1)==ESP_ERR_INVALID_STATE);ouse_board_sd_bus_init();
    assert(__wrap_sdmmc_write_sectors(NULL,NULL,0,1)==ESP_FAIL && bus->depth==0);
    assert(__wrap_sdmmc_read_sectors(NULL,NULL,0,1)==ESP_OK && bus->depth==0);
    assert(__wrap_sdmmc_card_init(NULL,NULL)==ESP_OK && bus->depth==0);
    sdmmc_card_t card={.csd={.capacity=16,.sector_size=512}};unsigned char sectors[1536];
    assert(__wrap_sdmmc_write_sectors(&card,sectors,0,3)==ESP_OK && single_writes==3 && bus->depth==0);
    assert(__wrap_sdmmc_write_sectors(&card,sectors,15,3)==ESP_ERR_INVALID_SIZE && bus->depth==0);
    return 0;
}
'''.replace('/* RX */',rx).replace('/* FREE */',free).replace('/* GET */',get_buffer).replace('/* GATE */',gate)
source=OUT/'memory.c'
source.write_text(fixture,encoding='utf-8')
result=subprocess.run([str(compiler),'/nologo','/std:c11','/O2','/MD','/utf-8',str(source),'/Fe'+str(OUT/'memory.exe')],cwd=OUT,env=env,capture_output=True,text=True,errors='replace')
assert result.returncode==0,(result.stdout,result.stderr)
subprocess.run([str(OUT/'memory.exe')],cwd=OUT,check=True,timeout=10)
print('PASS generated Hosted overlay: bounded RX retries/drop, recovery after pressure, malformed-packet buffer release, four cached DMA buffers per pool, excess buffers returned to heap')
