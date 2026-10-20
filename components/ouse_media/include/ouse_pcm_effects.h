// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#define OUSE_EFFECT_VOICES 5
#define OUSE_EFFECT_BYTES_MAX 64000
#ifndef OUSE_EFFECT_ALLOC
#define OUSE_EFFECT_ALLOC malloc
#endif
typedef struct { unsigned char *pcm;size_t size,offset;unsigned gain; } ouse_effect_voice_t;
typedef struct { ouse_effect_voice_t voices[OUSE_EFFECT_VOICES]; } ouse_effects_t;
// Caller serializes all operations. PCM16 LE, mono, 16000 Hz, copied ownership.
static inline void ouse_effects_clear(ouse_effects_t *effects)
{
    for(unsigned i=0;i<OUSE_EFFECT_VOICES;i++){free(effects->voices[i].pcm);memset(&effects->voices[i],0,sizeof(effects->voices[i]));}
}
static inline bool ouse_effects_add(ouse_effects_t *effects,const void *data,size_t size,unsigned gain)
{
    if(!data || !size || (size&1) || size>OUSE_EFFECT_BYTES_MAX || gain>100)return false;
    for(unsigned i=0;i<OUSE_EFFECT_VOICES;i++)if(!effects->voices[i].pcm){
        unsigned char *copy=OUSE_EFFECT_ALLOC(size);if(!copy)return false;memcpy(copy,data,size);
        effects->voices[i]=(ouse_effect_voice_t){copy,size,0,gain};return true;
    }
    return false; // Same bounded five-voice policy as the original game.
}
static inline void ouse_effects_mix(ouse_effects_t *effects,int16_t *output,unsigned count)
{
    for(unsigned n=0;n<count;n++){
        int32_t value=output[n];
        for(unsigned i=0;i<OUSE_EFFECT_VOICES;i++){
            ouse_effect_voice_t *voice=&effects->voices[i];if(!voice->pcm)continue;
            const unsigned char *p=voice->pcm+voice->offset;
            value+=(int16_t)((unsigned)p[0]|((unsigned)p[1]<<8))*(int32_t)voice->gain/100;
            voice->offset+=2;if(voice->offset==voice->size){free(voice->pcm);memset(voice,0,sizeof(*voice));}
        }
        output[n]=(int16_t)(value>32767?32767:value< -32768?-32768:value);
    }
}
