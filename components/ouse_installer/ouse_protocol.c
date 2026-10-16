// SPDX-License-Identifier: MIT
#include "ouse_protocol.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
uint32_t ouse_crc32(const void *data,size_t size)
{
    const unsigned char *p=data;uint32_t crc=~0u;
    while(size--){crc^=*p++;for(unsigned i=0;i<8;++i)crc=(crc>>1)^(0xedb88320u & (0u-(crc&1)));}
    return ~crc;
}
static bool hex32(const char *p,uint32_t *value)
{
    if(strlen(p)!=8)return false;
    for(unsigned i=0;i<8;++i)if(!isxdigit((unsigned char)p[i]))return false;
    *value=(uint32_t)strtoul(p,NULL,16);return true;
}
bool ouse_frame_parse(char *line,ouse_frame_t *frame)
{
    size_t n=strlen(line);if(n>=OUSE_WIRE_MAX || n<35 || strncmp(line,"OPK1 ",5))return false;
    char *last=strrchr(line,' ');uint32_t crc;
    if(!last || !hex32(last+1,&crc) || ouse_crc32(line,(size_t)(last-line))!=crc)return false;
    *last=0;char *token=line+5,*seq=strchr(token,' ');if(!seq)return false;*seq++=0;
    char *verb=strchr(seq,' ');if(!verb)return false;*verb++=0;
    char *payload=strchr(verb,' ');if(!payload)return false;*payload++=0;
    if(strchr(payload,' ') || !*payload || !*verb || strlen(verb)>12 ||
       !hex32(token,&frame->token) || !hex32(seq,&frame->sequence))return false;
    frame->verb=verb;frame->payload=payload;frame->checksum=crc;return true;
}
static int nibble(char c)
{ if(c>='0' && c<='9')return c-'0';if(c>='a' && c<='f')return c-'a'+10;if(c>='A' && c<='F')return c-'A'+10;return -1; }
bool ouse_hex_decode(const char *text,unsigned char *out,size_t capacity,size_t *length)
{
    size_t n=strlen(text);if(n%2 || n/2>capacity)return false;
    for(size_t i=0;i<n/2;++i){int a=nibble(text[i*2]),b=nibble(text[i*2+1]);if(a<0 || b<0)return false;out[i]=(unsigned char)((a<<4)|b);}
    *length=n/2;return true;
}
