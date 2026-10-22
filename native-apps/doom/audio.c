// SPDX-License-Identifier: GPL-2.0-or-later
// DMX effects mixed into the host's single 16 kHz mono codec stream.
#include <stdio.h>
#include <string.h>
#include "engine/i_sound.h"
#include "engine/w_wad.h"
#include "engine/z_zone.h"
extern int ouse_game_audio_open(void);
extern unsigned ouse_game_audio_position(void);
extern unsigned ouse_game_audio_write(const int16_t *,unsigned);
extern void ouse_game_audio_close(void);
#define CHANNELS 16
static struct {const byte *data;unsigned length,pos,frac,step;int volume;} channels[CHANNELS];
static int16_t pending[256];
static unsigned pending_count,pending_at,sent;
static boolean ready;
static boolean init(boolean prefix)
{(void)prefix;sent=pending_count=pending_at=0;ready=ouse_game_audio_open();return ready;}
static void shutdown(void){ready=false;memset(channels,0,sizeof(channels));ouse_game_audio_close();}
static int lump(sfxinfo_t *sfx)
{char name[12];snprintf(name,sizeof(name),"ds%s",sfx->name);return W_GetNumForName(name);}
static void params(int ch,int vol,int sep)
{(void)sep;if(ch>=0 && ch<CHANNELS)channels[ch].volume=vol;}
static int start(sfxinfo_t *sfx,int ch,int vol,int sep)
{
    (void)sep;if(!ready || ch<0 || ch>=CHANNELS)return -1;
    int id=sfx->lumpnum,length=W_LumpLength(id);if(length<8)return -1;
    const byte *data=W_CacheLumpNum(id,PU_STATIC);
    unsigned rate=data[2]|(data[3]<<8),count=data[4]|(data[5]<<8)|(data[6]<<16)|((unsigned)data[7]<<24);
    if(data[0]!=3 || data[1] || !rate || count>(unsigned)length-8 || !count)return -1;
    // Hold the original WAD sound cache until session teardown. Shared effects
    // can play on several channels, so no channel may individually purge it.
    channels[ch].data=data+8;channels[ch].length=count;channels[ch].pos=channels[ch].frac=0;
    channels[ch].step=((uint32_t)rate<<16)/16000;channels[ch].volume=vol;return ch;
}
static void stop(int ch){if(ch>=0 && ch<CHANNELS)channels[ch].data=NULL;}
static boolean playing(int ch){return ch>=0 && ch<CHANNELS && channels[ch].data!=NULL;}
static void update(void)
{
    if(!ready)return;
    unsigned target=ouse_game_audio_position()+2400;
    while(sent<target){
        if(pending_at==pending_count){
            pending_at=0;pending_count=target-sent;if(pending_count>256)pending_count=256;
            for(unsigned i=0;i<pending_count;++i){
                int mixed=0;
                for(unsigned ch=0;ch<CHANNELS;++ch){
                    if(!channels[ch].data)continue;
                    mixed+=((int)channels[ch].data[channels[ch].pos]-128)*channels[ch].volume;
                    channels[ch].frac+=channels[ch].step;
                    channels[ch].pos+=channels[ch].frac>>16;channels[ch].frac&=65535;
                    if(channels[ch].pos>=channels[ch].length)channels[ch].data=NULL;
                }
                // 127-volume effect maps to about half the DAC range.
                if(mixed>32767)mixed=32767;if(mixed<-32768)mixed=-32768;
                pending[i]=(int16_t)mixed;
            }
        }
        unsigned n=ouse_game_audio_write(pending+pending_at,pending_count-pending_at);
        pending_at+=n;sent+=n;if(pending_at<pending_count)break;
    }
}
static snddevice_t devices[]={SNDDEVICE_SB};
sound_module_t DG_sound_module={devices,1,init,shutdown,lump,update,params,start,stop,playing,NULL};
music_module_t DG_music_module={0};
int use_libsamplerate=0;
float libsamplerate_scale=1.0f;
