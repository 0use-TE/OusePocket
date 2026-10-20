"""Compile the real recorder WAV finalizer against deterministic PCM/SD doubles."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.tools/recorder-storage-check'
OUT.mkdir(parents=True, exist_ok=True)

def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for position in range(opening, len(source)):
        if source[position] == '{':
            depth += 1
        elif source[position] == '}':
            depth -= 1
            if not depth:
                return source[start:position + 1]
    raise ValueError(signature)

recorder = (ROOT / 'components/ouse_recorder/ouse_recorder.c').read_text(encoding='utf-8')
files = (ROOT / 'components/ouse_files/ouse_app_files.c').read_text(encoding='utf-8')
fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/utime.h>
#include <io.h>
#include <direct.h>
#define mkdir(path,mode) _mkdir(path)
#define S_ISDIR(mode) (((mode)&_S_IFDIR)!=0)
#define strncasecmp _strnicmp
#define OUSE_APP_FILES_ROOT "data/Apps"
#include <fcntl.h>
#include <time.h>
#include "ouse_tasks.h"
#define ESP_ERR_INVALID_SIZE 0x104
#define OUSE_FILES_PATH_MAX 128
#define OUSE_APP_FILE_PATH 192
#define ROOT "data"
#define strcasecmp _stricmp
#define fsync _commit
#define fileno _fileno
#define unlink _unlink
#define utime _utime
#define utimbuf _utimbuf
#define open(path,flags,mode) _open(path,(flags)|_O_BINARY,mode)
#define pdMS_TO_TICKS(value) (value)
typedef struct {bool active,stopping;uint32_t task_id,revision,duration_ms;int64_t ended;esp_err_t error;char path[128];} state_t;
static state_t snapshot,*state=&snapshot;
static ouse_task_owner_t owner={.app_id="user.recorder",.instance=1};
static esp_err_t ouse_app_files_prepare(const char *id){return ESP_OK;}
static bool producer_done=true,ready=true,cancelled;
static unsigned pcm_left,pcm_offset,receive_calls,finished;
static esp_err_t finished_error;
static bool finished_cancel;
static int pcm;
static void lock(void){}
static void unlock(void){}
static bool card_ready(void){return ready;}
static bool ouse_files_lease_begin(void){return ready;}
static void ouse_files_lease_end(void){}
static size_t xStreamBufferReceive(int buffer,void *target,size_t capacity,unsigned timeout){
 (void)buffer;(void)timeout;++receive_calls;size_t count=pcm_left<capacity?pcm_left:capacity;
 for(size_t index=0;index<count;++index)((unsigned char*)target)[index]=(unsigned char)(pcm_offset+index);
 pcm_left-=(unsigned)count;pcm_offset+=(unsigned)count;return count;
}
static size_t xStreamBufferBytesAvailable(int buffer){(void)buffer;return pcm_left;}
bool ouse_task_get(const ouse_task_owner_t *owner,uint32_t id,ouse_task_snapshot_t *out){(void)owner;(void)id;(void)out;return false;}
bool ouse_task_cancel_requested(uint32_t id){assert(id==42);return cancelled;}
void ouse_task_finish_result(uint32_t id,esp_err_t error,bool cancel,const ouse_task_result_t *result){
 assert(id==42&&result->present);++finished;finished_error=error;finished_cancel=cancel;
 assert(!state->active&&result->value.audio.recorded_ms==state->duration_ms);
}
/* FUNCTIONS */
static uint32_t read32(const unsigned char *value){return value[0]|((uint32_t)value[1]<<8)|((uint32_t)value[2]<<16)|((uint32_t)value[3]<<24);}
static void begin(const char *path,unsigned bytes){
 snapshot=(state_t){.active=true,.task_id=42};strcpy(snapshot.path,path);pcm_left=bytes;pcm_offset=0;receive_calls=0;finished=0;finished_error=0;cancelled=false;ready=true;
}
static void verify(const char *path,unsigned bytes){
 FILE *file=fopen(path,"rb");assert(file);unsigned char header[44];assert(fread(header,1,44,file)==44);
 assert(!memcmp(header,"RIFF",4)&&read32(header+4)==bytes+36&&!memcmp(header+8,"WAVEfmt ",8));
 assert(header[20]==1&&header[22]==1&&read32(header+24)==16000&&read32(header+28)==32000&&header[32]==2&&header[34]==16);
 assert(!memcmp(header+36,"data",4)&&read32(header+40)==bytes);
 for(unsigned index=0;index<bytes;++index)assert(fgetc(file)==(unsigned char)index);
 assert(fgetc(file)==EOF);fclose(file);
}
int main(void){
 char absolute[180];assert(recording_path("user.recorder","/Ouse/Apps/user.recorder/Recordings/test.wav",absolute));assert(!recording_path("user.recorder","Music/test.wav",absolute));assert(!recording_path("user.recorder","/Ouse/Apps/user.recorder/Recordings/../test.wav",absolute));assert(!recording_path("user.recorder","/Ouse/Apps/user.recorder/Recordings/test.mp3",absolute));
 begin("/Ouse/Apps/user.recorder/Recordings/good.wav",64000);save_recording();assert(finished==1&&finished_error==ESP_OK&&!finished_cancel&&state->duration_ms==2000&&state->ended>1577836800&&receive_calls>10);verify("data/Apps/user.recorder/Recordings/good.wav",64000);assert(!fopen("data/Apps/user.recorder/Recordings/good.wav.part","rb"));
 begin("/Ouse/Apps/user.recorder/Recordings/closed.wav",32000);cancelled=true;save_recording();assert(finished_error==ESP_OK&&finished_cancel);verify("data/Apps/user.recorder/Recordings/closed.wav",32000);
 begin("/Ouse/Apps/user.recorder/Recordings/empty.wav",0);save_recording();assert(finished_error!=ESP_OK&&!state->path[0]);assert(!fopen("data/Apps/user.recorder/Recordings/empty.wav","rb"));assert(!fopen("data/Apps/user.recorder/Recordings/empty.wav.part","rb"));
 begin("/Ouse/Apps/user.recorder/Recordings/no-card.wav",4096);ready=false;save_recording();assert(finished_error!=ESP_OK&&!state->path[0]&&finished==1&&!state->active);
 begin("/Ouse/Apps/user.recorder/Recordings/good.wav",32000);save_recording();assert(finished_error!=ESP_OK);verify("data/Apps/user.recorder/Recordings/good.wav",64000);
 puts("PASS real recorder finalizer: bounded PCM chunks, WAV sizes/rate/payload, fsync/rename, end time, close retention, empty/absent-card failures, no overwrite, path isolation");
 return 0;
}
'''
functions = "\n".join(function(files, signature) for signature in ("static bool valid_app(","static bool relative_path(","bool ouse_app_files_path("))+"\n"
for signature in ('static bool recording_path(', 'static bool prepare_recordings(', 'static void u32(', 'static bool wave_header(', 'static void save_recording('):
    functions += function(recorder, signature) + '\n'
(OUT / 'check.c').write_text(fixture.replace('/* FUNCTIONS */', functions), encoding='utf-8')
environment = dict(os.environ)
command = ROOT / '.tools/ouse-audio-check/environment.cmd'
for line in subprocess.check_output(['cmd.exe', '/d', '/c', str(command)], encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key, value = line.split('=', 1)
        environment[key] = value
compiler = Path(environment['VCToolsInstallDir']) / 'bin/Hostx64/x64/cl.exe'
result = subprocess.run([str(compiler), '/nologo', '/std:c11', '/O2', '/utf-8', '/D_CRT_SECURE_NO_WARNINGS',
                         '/I' + str(ROOT / '.tools/ouse-core-check'), '/I' + str(ROOT / 'components/ouse_tasks/include'),
                         'check.c', '/Fecheck.exe'], cwd=OUT, env=environment, capture_output=True, text=True, errors='replace')
assert result.returncode == 0, (result.stdout, result.stderr)
(OUT / 'data/Apps/user.recorder/Recordings').mkdir(parents=True, exist_ok=True)
for name in ('good.wav', 'good.wav.part', 'closed.wav', 'closed.wav.part', 'empty.wav', 'empty.wav.part', 'no-card.wav.part'):
    (OUT / 'data/Apps/user.recorder/Recordings' / name).unlink(missing_ok=True)
subprocess.run([str(OUT / 'check.exe')], cwd=OUT, check=True, timeout=15)
