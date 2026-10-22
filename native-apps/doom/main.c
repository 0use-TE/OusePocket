// SPDX-License-Identifier: GPL-2.0-or-later
#include "ouse_native_api.h"
#include "engine/doomgeneric.h"
#include "engine/doomkeys.h"
#include "engine/i_video.h"
#include "engine/i_sound.h"

__attribute__((used,section(".ouse_abi"),visibility("default")))
const uint32_t ouse_native_abi[3]={OUSE_NATIVE_MAGIC,2,28};
extern void ouse_game_frame(const unsigned char *,const unsigned char *);
extern int ouse_game_key(int *,int *);
extern void ouse_game_exit(int);
static const ouse_native_api_t *host;
static unsigned char palette[768];
void DG_Init(void){}
void DG_SetWindowTitle(const char *title){host->log(title);}
void DG_DrawFrame(void)
{
    for(unsigned i=0;i<256;++i){palette[i*3]=colors[i].r;palette[i*3+1]=colors[i].g;palette[i*3+2]=colors[i].b;}
    ouse_game_frame(DG_ScreenBuffer,palette);
}
uint32_t DG_GetTicksMs(void)
{if(host->should_stop())ouse_game_exit(0);return host->millis();}
void DG_SleepMs(uint32_t ms)
{
    uint32_t start=host->millis(),elapsed=0;
    do {
        // Keep PCM supplied while Doom waits for its next 35 Hz game tic.
        I_UpdateSound();
        uint32_t left=ms>elapsed?ms-elapsed:0;
        host->sleep_ms(left>4?4:left);
        if(host->should_stop())ouse_game_exit(0);
        elapsed=host->millis()-start;
    } while(elapsed<ms);
}
int DG_GetKey(int *pressed,unsigned char *key)
{
    int k;if(!ouse_game_key(pressed,&k))return 0;
    switch(k){
    case 128:k=KEY_UPARROW;break;case 129:k=KEY_DOWNARROW;break;
    case 130:k=KEY_LEFTARROW;break;case 131:k=KEY_RIGHTARROW;break;
    case 200:case 133:k=KEY_FIRE;break;
    case 201:case 32:k='e';break;
    case 134:k=KEY_RSHIFT;break;case 132:k=KEY_RALT;break;
    case 145:k=KEY_F11;break;case 146:k=KEY_F12;break;
    default:if(k>=135 && k<=144)k=KEY_F1+k-135;break;
    }
    *key=(unsigned char)k;return 1;
}
int app_main(int argc,char **argv)
{
    (void)argc;(void)argv;host=ouse_native_get_api(1);if(!host)return 1;
    char *args[]={"doom","-iwad","/sdcard/Ouse/Downloads/doom1.wad",
        "-config","/sdcard/Ouse/Doom/default.cfg",
        "-extraconfig","/sdcard/Ouse/Doom/extra.cfg",
        "-savedir","/sdcard/Ouse/Doom","-mb","8","-nomusic","-nogui",NULL};
    doomgeneric_Create(sizeof(args)/sizeof(args[0])-1,args);
    while(!host->should_stop()){
        uint32_t before=host->millis();doomgeneric_Tick();
        uint32_t elapsed=host->millis()-before;
        DG_SleepMs(elapsed<28?28-elapsed:1);
    }
    I_ShutdownSound();return 0;
}
