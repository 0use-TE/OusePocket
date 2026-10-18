"""Exercise the real frame/return callbacks without opening a physical camera."""
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/camera-driver-check'
OUT.mkdir(parents=True,exist_ok=True)
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
board=(ROOT/'boards/waveshare_lcd35/board_camera.c').read_text(encoding='utf-8')
callbacks=board[board.index('static esp_err_t stop(void)'):board.index('static esp_err_t start(void)')]+board[board.index('static void put(void)'):board.index('void ouse_board_camera_attach(')]
fixture=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define ESP_OK 0
#define ESP_LOGI(...) ((void)0)
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 259
#define ESP_ERR_INVALID_SIZE 260
#define ESP_ERR_TIMEOUT 263
#define V4L2_BUF_TYPE_VIDEO_CAPTURE 1
#define V4L2_MEMORY_USERPTR 2
#define V4L2_BUF_FLAG_ERROR 4
#define VIDIOC_DQBUF 10
#define VIDIOC_QBUF 11
#define VIDIOC_STREAMOFF 12
typedef int esp_err_t;
typedef struct {const unsigned char *pixels;unsigned width,height,stride;size_t bytes;} ouse_camera_frame_t;
struct v4l2_buffer {unsigned type,memory,index,bytesused,flags;size_t length;union {uintptr_t userptr;} m;};
static int descriptor=7,mode;
static bool streaming=true,held,queue_error,initialized;
static unsigned width=4,height=4,dequeues,queues,frames;
static size_t frame_bytes=32,buffer_lengths[2]={64,64};
static unsigned char pixels[2][64];
static void *buffers[2]={pixels[0],pixels[1]};
static struct v4l2_buffer current;
static int close(int handle){assert(handle==7);return 0;}
static esp_err_t esp_video_deinit(void){return mode==6 ? ESP_FAIL : ESP_OK;}
static int ioctl(int handle,unsigned operation,void *argument)
{
    if(operation==VIDIOC_STREAMOFF){assert(handle==7 && *(int *)argument==V4L2_BUF_TYPE_VIDEO_CAPTURE);return mode==5 ? -1 : 0;}
    struct v4l2_buffer *buffer=argument;
    assert(handle==descriptor && buffer->type==V4L2_BUF_TYPE_VIDEO_CAPTURE && buffer->memory==V4L2_MEMORY_USERPTR);
    if(operation==VIDIOC_DQBUF){
        ++dequeues;assert(!buffer->m.userptr && !buffer->length);
        if(mode==1){errno=EIO;return -1;}
        buffer->index=mode==3 ? 2 : 0;buffer->bytesused=mode==4 ? 16 : 32;return 0;
    }
    assert(operation==VIDIOC_QBUF && buffer->index<2);
    assert(buffer->m.userptr==(uintptr_t)buffers[buffer->index] && buffer->length==buffer_lengths[buffer->index]);
    ++queues;return mode==2 ? -1 : 0;
}
/* CALLBACKS */
int main(void)
{
    ouse_camera_frame_t output;
    assert(frame(&output)==ESP_OK && held && output.pixels==pixels[0] && output.bytes==32 && output.stride==4);
    assert(frame(&output)==ESP_ERR_INVALID_STATE && dequeues==1);
    put();assert(!held && queues==1);put();assert(queues==1);
    mode=1;assert(frame(&output)==ESP_ERR_TIMEOUT && !held);
    mode=2;assert(frame(&output)==ESP_OK);put();assert(queue_error);assert(frame(&output)==ESP_FAIL);
    queue_error=false;mode=3;assert(frame(&output)==ESP_ERR_INVALID_SIZE && queue_error && !held);
    queue_error=false;mode=4;assert(frame(&output)==ESP_ERR_TIMEOUT && !held && !queue_error);
    streaming=false;assert(frame(&output)==ESP_ERR_INVALID_STATE);
    buffers[0]=malloc(64);buffers[1]=malloc(64);streaming=initialized=true;
    mode=5;assert(stop()==ESP_FAIL && buffers[0] && buffers[1] && initialized && streaming && descriptor==7);
    mode=6;assert(stop()==ESP_FAIL && buffers[0] && buffers[1] && initialized && !streaming && descriptor==-1);
    mode=0;assert(stop()==ESP_OK && !buffers[0] && !buffers[1] && !initialized && !streaming);
    puts("PASS real camera callbacks: USERPTR return, exclusive frame hold, EIO timeout, bad-frame recovery, and retained DMA buffers until stop/deinit acknowledgement");
}
'''.replace('/* CALLBACKS */',callbacks)
source=OUT/'driver.c'
source.write_text(fixture,encoding='utf-8')
result=subprocess.run([str(compiler),'/nologo','/std:c11','/O2','/MD','/utf-8',str(source),'/Fe'+str(OUT/'driver.exe')],cwd=OUT,env=env,capture_output=True,text=True,errors='replace')
assert result.returncode==0,(result.stdout,result.stderr)
subprocess.run([str(OUT/'driver.exe')],cwd=OUT,check=True,timeout=10)
