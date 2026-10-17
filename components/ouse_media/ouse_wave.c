// SPDX-License-Identifier: MIT
#include "ouse_wave.h"
#include <string.h>
enum {RIFF,CHUNK,FORMAT,SKIP,DATA,PAD};
static unsigned u16(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static uint32_t u32(const unsigned char *p){return (uint32_t)u16(p)|(uint32_t)u16(p+2)<<16;}
void ouse_wave_init(ouse_wave_t *w,ouse_wave_output_t output,void *context)
{memset(w,0,sizeof(*w));w->header_need=12;w->write=output;w->context=context;}
static esp_err_t flush(ouse_wave_t *w)
{
    if(!w->count)return ESP_OK;
    esp_err_t error=w->write ? w->write(w->context,w->output,w->count) : ESP_OK;w->count=0;return error;
}
static esp_err_t sample(ouse_wave_t *w)
{
    int value=(int16_t)u16(w->frame);
    if(w->channels==2)value=(value+(int16_t)u16(w->frame+2))/2;
    ++w->frames;w->frame_size=0;w->phase+=16000;
    while(w->phase>=w->rate){w->phase-=w->rate;w->output[w->count++]=(int16_t)value;
        if(w->count==256){esp_err_t error=flush(w);if(error!=ESP_OK)return error;}
    }
    return ESP_OK;
}
esp_err_t ouse_wave_feed(ouse_wave_t *w,const unsigned char *bytes,size_t size)
{
    if(!w || (!bytes && size))return ESP_ERR_INVALID_ARG;
    if(size>128*1024*1024u-w->received)return ESP_ERR_INVALID_SIZE;
    w->received+=size;
    while(size){
        if(w->done)return ESP_OK; // bounded trailing RIFF chunks are ignored
        if(w->stage==RIFF || w->stage==CHUNK || w->stage==FORMAT){
            unsigned n=w->header_need-w->header_size;if(n>size)n=size;
            memcpy(w->header+w->header_size,bytes,n);w->header_size+=n;bytes+=n;size-=n;w->parsed+=n;
            if(w->header_size<w->header_need)continue;
            if(w->stage==RIFF){
                if(memcmp(w->header,"RIFF",4)||memcmp(w->header+8,"WAVE",4))return ESP_ERR_NOT_SUPPORTED;
                w->riff_bytes=(uint64_t)u32(w->header+4)+8;
                if(w->riff_bytes<44 || w->riff_bytes>128*1024*1024u)return ESP_ERR_INVALID_SIZE;
                w->stage=CHUNK;w->header_need=8;
            }else if(w->stage==CHUNK){
                w->remaining=u32(w->header+4);w->pad=w->remaining&1;
                if(w->parsed>w->riff_bytes || w->remaining+(uint64_t)w->pad>w->riff_bytes-w->parsed)return ESP_ERR_INVALID_SIZE;
                if(!memcmp(w->header,"fmt ",4)){
                    if(w->format || w->remaining<16 || w->remaining>40)return ESP_ERR_NOT_SUPPORTED;
                    w->stage=FORMAT;w->header_need=w->remaining;
                }else if(!memcmp(w->header,"data",4)){
                    if(!w->format || w->data || !w->remaining || w->remaining%(w->channels*2))return ESP_ERR_INVALID_SIZE;
                    w->data=true;w->data_size=w->remaining;w->stage=DATA;
                }else w->stage=w->remaining ? SKIP : CHUNK;
            }else{
                w->channels=u16(w->header+2);w->rate=u32(w->header+4);
                if(u16(w->header)!=1 || (w->channels!=1 && w->channels!=2) || u16(w->header+14)!=16 ||
                   w->rate<8000 || w->rate>48000 || u16(w->header+12)!=w->channels*2 || u32(w->header+8)!=w->rate*w->channels*2)return ESP_ERR_NOT_SUPPORTED;
                w->format=true;w->remaining=0;w->stage=w->pad ? PAD : CHUNK;w->header_need=8;
            }
            w->header_size=0;
        }else if(w->stage==PAD){++bytes;--size;++w->parsed;w->stage=CHUNK;w->header_need=8;}
        else if(w->stage==SKIP){
            size_t n=w->remaining<size ? w->remaining : size;w->remaining-=n;bytes+=n;size-=n;w->skipped+=n;w->parsed+=n;
            if(w->skipped>65536)return ESP_ERR_INVALID_SIZE;
            if(!w->remaining){w->stage=w->pad ? PAD : CHUNK;w->header_need=8;}
        }else{
            unsigned need=w->channels*2-w->frame_size,n=need<size ? need : size;if(n>w->remaining)n=w->remaining;
            memcpy(w->frame+w->frame_size,bytes,n);w->frame_size+=n;bytes+=n;size-=n;w->remaining-=n;w->parsed+=n;
            if(w->frame_size==w->channels*2){esp_err_t error=sample(w);if(error!=ESP_OK)return error;}
            if(!w->remaining){w->done=true;return flush(w);}
        }
    }
    return ESP_OK;
}
esp_err_t ouse_wave_finish(ouse_wave_t *w)
{
    if(!w || !w->data || !w->done || w->frame_size || w->received<w->riff_bytes)return ESP_ERR_INVALID_SIZE;
    return flush(w);
}
