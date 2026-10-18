#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "ouse_devices.h"
typedef struct { int gain; } ouse_audio_mixer_t;
static inline void ouse_audio_mix(ouse_audio_mixer_t *mixer,int16_t *samples,unsigned count,
                                 bool active,ouse_audio_action_t action,uint64_t offset,uint64_t total)
{
    static const int16_t sine[32]={0,780,1531,2222,2828,3326,3696,3923,4000,3923,3696,3326,2828,2222,1531,780,
        0,-780,-1531,-2222,-2828,-3326,-3696,-3923,-4000,-3923,-3696,-3326,-2828,-2222,-1531,-780};
    int target=active ? (action==OUSE_AUDIO_TEST ? 16384 : 8192) : 32768;
    unsigned frequency=action==OUSE_AUDIO_TEST ? 440 : 880;
    for(unsigned index=0;index<count;++index){
        if(mixer->gain<target){mixer->gain+=64;if(mixer->gain>target)mixer->gain=target;}
        else if(mixer->gain>target){mixer->gain-=64;if(mixer->gain<target)mixer->gain=target;}
        uint64_t position=offset+index;
        int tone=0;
        if(active && position<total && (action==OUSE_AUDIO_TEST || position%16000<8000)){
            unsigned envelope=position%16000<320 ? position%16000 : 320;
            if(action!=OUSE_AUDIO_TEST && 8000-position%16000<envelope)envelope=8000-position%16000;
            if(total-position<envelope)envelope=(unsigned)(total-position);
            tone=sine[(position*frequency%16000)*32/16000]*(int)envelope/320;
        }
        int mixed=(int)samples[index]*mixer->gain/32768+tone;
        samples[index]=(int16_t)(mixed>32767 ? 32767 : mixed<-32768 ? -32768 : mixed);
    }
}
