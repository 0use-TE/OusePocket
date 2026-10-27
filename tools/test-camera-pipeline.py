"""Production capture transaction and thumbnail slot allocator, without hardware."""
from pathlib import Path
import os,subprocess
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'.tools/camera-pipeline-check';OUT.mkdir(parents=True,exist_ok=True);(OUT/'Pictures').mkdir(exist_ok=True)
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(ROOT/'.tools/ouse-core-check/environment.cmd')],encoding='mbcs').splitlines():
 if '=' in line and not line.startswith('='):
  k,v=line.split('=',1);env[k]=v
cl=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
source=(ROOT/'components/ouse_camera/ouse_camera_capture.inc').read_text(encoding='utf-8')
capture=source[source.index('static esp_err_t capture_frame('):]
fixture=r'''
#include "ouse_photo_slots.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <io.h>
#include <errno.h>
#include <time.h>
#define open _open
#define close _close
#define fdopen _fdopen
#define fileno _fileno
#define fsync _commit
#define unlink _unlink
#define O_WRONLY _O_WRONLY
#define O_CREAT _O_CREAT
#define O_EXCL _O_EXCL
#define ESP_ERR_INVALID_SIZE 260
#define ESP_LOGW(...) ((void)0)
#define pdMS_TO_TICKS(n) (n)
#define PHOTO_LIMIT (4*1024*1024)
#define JPEG_ENC_ALLOC_OUTPUT_BUFFER 1
#define JPEG_ENCODE_IN_FORMAT_RGB565 1
#define JPEG_DOWN_SAMPLING_YUV420 1
typedef struct {ouse_task_owner_t owner;uint32_t id;} job_t;
typedef void *jpeg_encoder_handle_t;
typedef struct {unsigned timeout_ms;} jpeg_encode_engine_cfg_t;
typedef struct {unsigned buffer_direction;} jpeg_encode_memory_alloc_cfg_t;
typedef struct {unsigned width,height,src_type,sub_sample,image_quality;} jpeg_encode_cfg_t;
static bool hardware=true,cancelled,alive=true,sd=true,bad_storage,encode_failed;
static unsigned nonce=123,sequence,attempts,puts_count,delays,timeouts;
static char stage[16];static unsigned char frame_pixels[32];
static ouse_task_owner_t camera_owner={.instance=1};
static bool same(const ouse_task_owner_t *a,const ouse_task_owner_t *b){return a->instance==b->instance;}
static bool live(const ouse_task_owner_t *o){(void)o;return alive;}
static bool wanted(const ouse_task_owner_t *o){(void)o;return alive;}
bool ouse_task_cancel_requested(uint32_t id){(void)id;return cancelled;}
static bool sd_ready(void){return sd;}
static void capture_stage(const job_t *j,const char *s){(void)j;strcpy(stage,s);}
static void vTaskDelay(unsigned n){assert(n==20);++delays;}
static esp_err_t get_frame(ouse_camera_frame_t *f){++attempts;if(attempts<=timeouts)return ESP_ERR_TIMEOUT;*f=(ouse_camera_frame_t){frame_pixels,4,4,4,32};return ESP_OK;}
static void put_frame(void){++puts_count;}
static ouse_camera_driver_t driver={.frame=get_frame,.put=put_frame};
static esp_err_t jpeg_new_encoder_engine(const jpeg_encode_engine_cfg_t *c,jpeg_encoder_handle_t *h){(void)c;*h=(void*)1;return ESP_OK;}
static void *jpeg_alloc_encoder_mem(size_t n,const jpeg_encode_memory_alloc_cfg_t *c,size_t *cap){(void)c;*cap=n;return malloc(n);}
static esp_err_t jpeg_encoder_process(jpeg_encoder_handle_t h,const jpeg_encode_cfg_t *c,const void *in,size_t n,void *out,size_t cap,uint32_t *len){(void)h;(void)c;(void)in;(void)n;assert(cap>=6);memcpy(out,"\xff\xd8ok\xff\xd9",6);*len=6;return encode_failed?ESP_FAIL:ESP_OK;}
static void jpeg_del_encoder_engine(jpeg_encoder_handle_t h){assert(h);}
static bool photo_path(const char *s,char out[180]){snprintf(out,180,"%s%s",bad_storage?"Missing/":"",s);return true;}
static int ouse_files_refresh(void){return 0;}
/* CAPTURE */
static void slots(void){
 ouse_photo_entry_t old[12]={0},next[12]={0};bool cached[12];
 for(unsigned i=0;i<12;++i){snprintf(old[i].path,128,"Pictures/%u.jpg",i);old[i].bytes=100;old[i].image=i;}
 // Three-row shift, then reverse, retaining nine stable buffers each time.
 for(unsigned round=0;round<100;++round){unsigned offset=round%2?0:3;uint32_t reserved=0,assigned=0;
  for(unsigned i=0;i<12;++i)reserved|=1u<<old[i].image;
  for(unsigned i=0;i<12;++i){memset(&next[i],0,sizeof(next[i]));snprintf(next[i].path,128,"Pictures/%u.jpg",i+offset);next[i].bytes=100;}
  assert(ouse_photo_slots(next,12,old,12,cached));
  for(unsigned i=0;i<12;++i){uint32_t bit=1u<<next[i].image;assert(!(assigned&bit));assigned|=bit;if(!cached[i])assert(!(reserved&bit));}
  memcpy(old,next,sizeof(old));
 }
 for(unsigned i=0;i<12;++i){snprintf(next[i].path,128,"Pictures/new%u.jpg",i);next[i].bytes=100;}
 assert(ouse_photo_slots(next,12,old,12,cached));for(unsigned i=0;i<12;++i)assert(!cached[i]);
 memcpy(old,next,sizeof(old));next[0].bytes++;assert(ouse_photo_slots(next,12,old,12,cached));assert(!cached[0]);for(unsigned i=1;i<12;++i)assert(cached[i]);
 puts("PASS thumbnail slots: forward/back overlap, unchanged paths keep slots, replacement pages and modified files never overwrite current thumbnails");
}
int main(void){slots();job_t j={.owner={.instance=1},.id=1};char saved[128];
 timeouts=1;assert(capture(&j,saved)==ESP_OK);assert(attempts==2&&puts_count==1&&delays==1&&!stage[0]);FILE *f=fopen(saved,"rb");assert(f);unsigned char data[6];assert(fread(data,1,6,f)==6&&!memcmp(data,"\xff\xd8ok\xff\xd9",6));fclose(f);_unlink(saved);
 attempts=delays=0;timeouts=10;assert(capture(&j,saved)==ESP_ERR_TIMEOUT&&attempts==4&&delays==3&&!strcmp(stage,"camera"));
 cancelled=true;attempts=0;assert(capture(&j,saved)==ESP_ERR_INVALID_STATE&&attempts==0);cancelled=false;
 timeouts=0;attempts=0;encode_failed=true;assert(capture(&j,saved)==ESP_FAIL&&!strcmp(stage,"encode"));encode_failed=false;
 bad_storage=true;attempts=0;assert(capture(&j,saved)==ESP_FAIL&&attempts==1&&!strcmp(stage,"storage"));
 sd=false;attempts=0;assert(capture(&j,saved)==ESP_ERR_INVALID_STATE&&attempts==0&&!strcmp(stage,"storage"));
 puts("PASS capture: transient first-frame retry saves one complete file, bounded timeout/cancellation, encode/storage diagnostics and no retry of writes");return 0;
}
'''.replace('/* CAPTURE */',capture)
(OUT/'check.c').write_text(fixture,encoding='utf-8')
includes=[ROOT/'components/ouse_camera',ROOT/'components/ouse_camera/include',ROOT/'components/ouse_tasks/include',ROOT/'.tools/ouse-core-check']
cmd=[str(cl),'/nologo','/std:c11','/utf-8','/O2','/MD','/D_CRT_SECURE_NO_WARNINGS']+['/I'+str(p) for p in includes]
subprocess.run(cmd+[str(OUT/'check.c'),'/Fe'+str(OUT/'check.exe')],cwd=OUT,env=env,check=True)
subprocess.run([str(OUT/'check.exe')],cwd=OUT,check=True,timeout=15)
