// SPDX-License-Identifier: GPL-2.0-or-later
// OusePocket platform adapter for Retro-Go's Nofrendo core.
#include <stdio.h>
#include "ouse_native_api.h"
#include "core/nofrendo.h"

__attribute__((used,section(".ouse_abi"),visibility("default")))
const uint32_t ouse_native_abi[3]={OUSE_NATIVE_MAGIC,2,28};
extern void ouse_game_frame(const unsigned char *,const unsigned char *);
extern int ouse_game_key(int *,int *);
extern int ouse_game_audio_open(void);
extern unsigned ouse_game_audio_write(const int16_t *,unsigned);
extern void ouse_game_audio_close(void);
extern const char *ouse_game_nes_rom(void);
static const ouse_native_api_t *host;
static uint8_t linear[256*240],held[256];
static uint8_t *palette;

uint32_t nofrendo_crc32(uint32_t crc,const void *data,size_t size)
{
    const uint8_t *p=data;crc=~crc;
    while(size--){crc^=*p++;for(unsigned b=0;b<8;++b)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);}
    return ~crc;
}
static void blit(uint8_t *pixels)
{
    for(unsigned y=0;y<240;++y)memcpy(linear+y*256,NES_SCREEN_GETPTR(pixels,0,y),256);
    ouse_game_frame(linear,palette);
}
static unsigned button(unsigned key)
{
    switch(key){
    case 'w':case 128:return NES_PAD_UP;case 's':case 129:return NES_PAD_DOWN;
    case 'a':case 130:return NES_PAD_LEFT;case 'd':case 131:return NES_PAD_RIGHT;
    case 'z':case 'j':case 32:return NES_PAD_A;
    case 'x':case 'k':return NES_PAD_B;
    case 13:return NES_PAD_START;case 9:case 134:return NES_PAD_SELECT;
    default:return 0;
    }
}
static unsigned read_input(bool *reset,bool *quit)
{
    int down,key;
    // State-based console input needs a full emulated frame for each edge.
    // Even a back-to-back serial down/up must reach the emulated controller.
    for(unsigned n=0;n<32 && ouse_game_key(&down,&key);++n){
        if(key<1 || key>255)continue;
        held[key]=down;
        if(key=='r' && down)*reset=true;
        if(key==27 && down)*quit=true;
        if(button(key))break;
    }
    unsigned state=0;for(unsigned k=1;k<256;++k)if(held[k])state|=button(k);
    return state;
}
static bool valid_rom(const char *path)
{
    FILE *f=fopen(path,"rb");if(!f)return false;
    uint8_t h[16];bool ok=fread(h,1,16,f)==16 && !memcmp(h,"NES\x1a",4);
    if(fseek(f,0,SEEK_END))ok=false;
    long size=ftell(f);fclose(f);
    if(!ok || size<16 || size>2*1024*1024 || !h[4] || (h[7]&12)==8)return false;
    size_t expected=16u+((h[6]&4)?512u:0u)+(size_t)h[4]*16384u+(size_t)h[5]*8192u;
    return expected<=(size_t)size;
}
static bool submit(const int16_t *samples,unsigned count)
{
    uint32_t progress=host->millis();
    while(count && !host->should_stop()){
        unsigned n=ouse_game_audio_write(samples,count);samples+=n;count-=n;
        if(n)progress=host->millis();
        else if(host->millis()-progress>250){
            host->log("NES audio unavailable; continuing without sound");ouse_game_audio_close();return false;
        }
        if(count)host->sleep_ms(1);
    }
    return true;
}
int app_main(int argc,char **argv)
{
    (void)argc;(void)argv;host=ouse_native_get_api(1);if(!host)return 1;
    const char *path=ouse_game_nes_rom();
    if(!valid_rom(path)){host->log("Invalid ROM: use a complete iNES .nes file (up to 2 MiB)");return 2;}
    nes_t *nes=nes_init(SYS_DETECT,16000,false,NULL);if(!nes)return 3;
    if(nes_loadfile(path)<0){nes_shutdown();return 4;}
    palette=nofrendo_buildpalette(NES_PALETTE_NESCLASSIC,24);
    uint8_t *video=calloc(NES_SCREEN_PITCH*NES_SCREEN_HEIGHT,1);
    if(!palette || !video){free(palette);free(video);nes_shutdown();return 5;}
    nes_setvidbuf(video);nes->blit_func=blit;
    char save[100];snprintf(save,sizeof(save),"/sdcard/Ouse/NES/%08lx.sav",(unsigned long)nes->cart->checksum);
    if(nes->cart->battery)rom_loadsram(save);
    bool sound=ouse_game_audio_open(),quit=false;
    unsigned frame=0,fraction=0;uint32_t started=host->millis();
    host->log("NES loaded: WASD/arrows, Z/X, Enter/Tab; R resets");
    while(!host->should_stop() && !quit){
        bool reset=false;unsigned state=read_input(&reset,&quit);
        if(reset){nes_reset(false);nes_setvidbuf(video);}
        input_update(0,state);
        // Retain the fractional sample so 60 frames generate exactly 16000.
        fraction+=16000;nes->apu->samples_per_frame=fraction/nes->refresh_rate;fraction%=nes->refresh_rate;
        nes_emulate(frame%2==0);
        if(sound)sound=submit(nes->apu->buffer,nes->apu->samples_per_frame);
        ++frame;uint32_t target=(uint32_t)((uint64_t)frame*1000u/nes->refresh_rate);
        while(!host->should_stop() && host->millis()-started<target)host->sleep_ms(1);
        if(host->millis()-started>target+250){started=host->millis();frame=0;}
    }
    ouse_game_audio_close();
    if(nes->cart->battery)rom_savesram(save);
    nes_shutdown();free(video);free(palette);return 0;
}
