#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static void wave16(unsigned char *p,unsigned value){p[0]=value;p[1]=value>>8;}
static void wave32(unsigned char *p,uint32_t value){wave16(p,value);wave16(p+2,value>>16);}
static unsigned char *make_wave(unsigned rate,unsigned channels,unsigned frames,size_t *size)
{
    *size=44+frames*channels*2;unsigned char *p=calloc(1,*size);
    memcpy(p,"RIFF",4);wave32(p+4,(uint32_t)*size-8);memcpy(p+8,"WAVEfmt ",8);wave32(p+16,16);
    wave16(p+20,1);wave16(p+22,channels);wave32(p+24,rate);wave32(p+28,rate*channels*2);wave16(p+32,channels*2);wave16(p+34,16);
    memcpy(p+36,"data",4);wave32(p+40,frames*channels*2);
    for(unsigned i=0;i<frames;++i){wave16(p+44+i*channels*2,1000);if(channels==2)wave16(p+46+i*channels*2,(uint16_t)-1000);}
    return p;
}
