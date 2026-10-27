"""Production ambiguous-write handling: verify exact bytes, never replay writes."""
from pathlib import Path
import re,os,subprocess
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'.tools/sd-write-check';OUT.mkdir(parents=True,exist_ok=True)
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(ROOT/'.tools/ouse-core-check/environment.cmd')],encoding='mbcs').splitlines():
 if '=' in line and not line.startswith('='):
  k,v=line.split('=',1);env[k]=v
cl=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
production=re.sub(r'^#include[^\n]*\n','',(ROOT/'boards/waveshare_lcd35/board_sd_write.c').read_text(encoding='utf-8'),flags=re.MULTILINE)
fixture=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_CRC 0x109
#define ESP_ERR_TIMEOUT 0x107
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_DMA 2
#define SCF_CMD_AC 0
#define SCF_RSP_R1 16
#define ESP_LOGW(...) ((void)0)
#define pdMS_TO_TICKS(n) (n)
typedef int esp_err_t;
typedef struct {unsigned opcode,arg,flags,timeout_ms;uint32_t response[4];int error;} sdmmc_command_t;
typedef struct {struct {unsigned sector_size;} csd;struct {int slot;int (*do_transaction)(int,sdmmc_command_t *);}host;unsigned rca;}sdmmc_card_t;
static unsigned writes,reads,statuses,sleeps,busy;
static bool mismatch,allocation_failed,read_failed,status_failed,command_failed;
static int write_error=ESP_ERR_INVALID_CRC;
static unsigned char expected[512];
static void *heap_caps_aligned_alloc(size_t a,size_t n,int caps){assert(a==64&&n==512&&caps==3);return allocation_failed?NULL:malloc(n);}
static void vTaskDelay(unsigned ms){assert(ms==10);++sleeps;}
static int status(int slot,sdmmc_command_t *c){assert(slot==0&&c->opcode==13&&c->arg==0x12340000);++statuses;c->response[0]=statuses<=busy?0xe00:0x900;c->error=command_failed?ESP_FAIL:ESP_OK;return status_failed?ESP_FAIL:ESP_OK;}
int __real_sdmmc_write_sectors(sdmmc_card_t *c,const void *p,size_t sector,size_t count){(void)c;assert(p==expected&&sector==42&&count==1);++writes;return write_error;}
int __real_sdmmc_read_sectors(sdmmc_card_t *c,void *p,size_t sector,size_t count){(void)c;assert(sector==42&&count==1);++reads;memcpy(p,expected,512);if(mismatch)((unsigned char*)p)[511]^=1;return read_failed?ESP_FAIL:ESP_OK;}
/* PRODUCTION */
static void reset(void){writes=reads=statuses=sleeps=busy=0;mismatch=allocation_failed=read_failed=status_failed=command_failed=false;}
int main(void){sdmmc_card_t c={.csd.sector_size=512,.host={.slot=0,.do_transaction=status},.rca=0x1234};for(unsigned i=0;i<512;++i)expected[i]=i;
 allocation_failed=true;assert(ouse_board_sd_verified_sector(&c,expected,42,1)==ESP_ERR_INVALID_CRC&&writes==1&&!reads);
 reset();busy=2;assert(ouse_board_sd_verified_sector(&c,expected,42,1)==ESP_OK);assert(writes==1&&reads==1&&statuses==3&&sleeps==2);
 reset();mismatch=true;assert(ouse_board_sd_verified_sector(&c,expected,42,1)==ESP_ERR_INVALID_CRC&&writes==1&&reads==1);
 reset();read_failed=true;assert(ouse_board_sd_verified_sector(&c,expected,42,1)==ESP_ERR_INVALID_CRC&&writes==1);
 reset();busy=100;assert(ouse_board_sd_verified_sector(&c,expected,42,1)==ESP_ERR_INVALID_CRC&&writes==1&&!reads&&statuses==25);
 reset();status_failed=true;assert(ouse_board_sd_verified_sector(&c,expected,42,1)==ESP_ERR_INVALID_CRC&&writes==1&&!reads);
 reset();command_failed=true;assert(ouse_board_sd_verified_sector(&c,expected,42,1)==ESP_ERR_INVALID_CRC&&writes==1&&!reads);
 reset();write_error=ESP_ERR_TIMEOUT;assert(ouse_board_sd_verified_sector(&c,expected,42,1)==ESP_ERR_TIMEOUT&&writes==1&&!reads&&!statuses);
 reset();write_error=ESP_OK;assert(ouse_board_sd_verified_sector(&c,expected,42,1)==ESP_OK&&writes==1&&!reads&&!statuses);
 free(verification_sector);puts("PASS CRC acknowledgement: bounded busy/status wait, exact 512-byte readback, mismatch/read/command/timeout/OOM failures preserved; every case issues exactly one write");return 0;}
'''.replace('/* PRODUCTION */',production)
(OUT/'check.c').write_text(fixture,encoding='utf-8')
subprocess.run([str(cl),'/nologo','/std:c11','/utf-8','/O2','/MD',str(OUT/'check.c'),'/Fe'+str(OUT/'check.exe')],cwd=OUT,env=env,check=True)
subprocess.run([str(OUT/'check.exe')],cwd=OUT,check=True,timeout=10)
