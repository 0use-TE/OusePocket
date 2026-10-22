// SPDX-License-Identifier: GPL-2.0-or-later
// OusePocket standalone single-player adaptation of ESP32_QUAKE2.
#include "ouse_native_api.h"
#include "engine/quake2.h"
#include "engine/qcommon/qcommon.h"
#include "engine/client/snd_loc.h"

__attribute__((used,section(".ouse_abi"),visibility("default")))
const uint32_t ouse_native_abi[3]={OUSE_NATIVE_MAGIC,2,28};
extern void ouse_game_frame(const unsigned char *pixels,const unsigned char *palette);
extern int ouse_game_key(int *down,int *key);
extern void ouse_game_mouse(int *dx,int *dy);
extern void ouse_game_exit(int code);
static const ouse_native_api_t *host;
static unsigned char colors[768];
int QG_Milliseconds(void){return host->millis();}
void QG_SetPalette(unsigned char *palette){memcpy(colors,palette,sizeof(colors));}
void QG_DrawFrame(void *pixels){ouse_game_frame(pixels,colors);}
int QG_GetKey(int *down,int *key){return ouse_game_key(down,key);}
void QG_GetMouseMove(int *dx,int *dy){ouse_game_mouse(dx,dy);}
void HandleInput(void);
extern int ouse_game_audio_open(void);
extern unsigned ouse_game_audio_position(void);
extern unsigned ouse_game_audio_write(const int16_t *,unsigned);
extern void ouse_game_audio_close(void);
#define AUDIO_SAMPLES 8192u
static unsigned audio_sent;
qboolean SNDDMA_Init(void)
{
    dma.buffer=malloc(AUDIO_SAMPLES*2);
    if(!dma.buffer)return false;
    if(!ouse_game_audio_open()){free(dma.buffer);dma.buffer=NULL;return false;}
    memset(dma.buffer,0,AUDIO_SAMPLES*2);audio_sent=0;
    dma.channels=1;dma.samplebits=16;dma.speed=16000;
    dma.samples=AUDIO_SAMPLES;dma.samplepos=0;dma.submission_chunk=1;return true;
}
int SNDDMA_GetDMAPos(void){return ouse_game_audio_position()&(AUDIO_SAMPLES-1);}
void SNDDMA_Shutdown(void){ouse_game_audio_close();free(dma.buffer);dma.buffer=NULL;}
void SNDDMA_BeginPainting(void){}
void SNDDMA_Submit(void)
{
    if(!dma.buffer)return;
    while(audio_sent<(unsigned)paintedtime){
        unsigned offset=audio_sent&(AUDIO_SAMPLES-1),count=(unsigned)paintedtime-audio_sent;
        if(count>AUDIO_SAMPLES-offset)count=AUDIO_SAMPLES-offset;
        unsigned sent=ouse_game_audio_write((int16_t *)dma.buffer+offset,count);
        audio_sent+=sent;if(sent<count)break;
    }
}
int app_main(int argc,char **argv)
{
    (void)argc;(void)argv;host=ouse_native_get_api(1);
    if(!host)return 1;
    char *args[]={"quake2","+set","basedir","/sdcard/Ouse/Quake2",
        "+set","s_initsound","1","+set","s_loadas8bit","1","+set","s_mixahead","0.15","+set","nostdout","0",
        "+set","maxclients","1","+set","deathmatch","0",
        "+set","freelook","1","+set","vid_fullscreen","0",
        "+set","cd_nocd","1","+menu_main",NULL};
    Quake2_Init((int)(sizeof(args)/sizeof(args[0]))-1,args);
    // The original demo starts a cinematic before demo1. Avoid its extra
    // decoder memory on this first single-player port.
    Cbuf_AddText("alias newgame \"killserver; maxclients 1; deathmatch 0; map demo1\"\n");
    // Apply the serial controller layout after default.cfg and saved config.
    Cbuf_AddText("bind w +forward\nbind s +back\nbind a +moveleft\nbind d +moveright\n"
        "bind 1 \"use Blaster\"\nbind 2 \"use Shotgun\"\nbind 3 \"use Super Shotgun\"\n"
        "bind 4 \"use Machinegun\"\nbind 5 \"use Chaingun\"\nbind 6 \"use Grenade Launcher\"\n"
        "bind 7 \"use Rocket Launcher\"\nbind 8 \"use HyperBlaster\"\nbind 9 \"use Railgun\"\n"
        "bind 0 \"use BFG10K\"\n");
    int before=QG_Milliseconds();
    while(!host->should_stop()){
        HandleInput();int now=QG_Milliseconds(),delta=now-before;
        if(delta<1){host->sleep_ms(1);continue;}
        if(delta>200)delta=200;before=now;
        Quake2_Frame(delta);host->sleep_ms(1);
    }
    CL_Shutdown();Qcommon_Shutdown();
    return 0;
}
