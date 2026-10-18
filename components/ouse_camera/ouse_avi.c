#include "ouse_avi.h"
#include <string.h>

static uint32_t get32(const unsigned char *bytes)
{return (uint32_t)bytes[0]|(uint32_t)bytes[1]<<8|(uint32_t)bytes[2]<<16|(uint32_t)bytes[3]<<24;}
static void put32(unsigned char *bytes,uint32_t value)
{for(unsigned shift=0;shift<4;++shift)bytes[shift]=(unsigned char)(value>>(shift*8));}
static bool write32(FILE *file,uint32_t value)
{unsigned char bytes[4];put32(bytes,value);return fwrite(bytes,1,4,file)==4;}
static bool write_jpeg(FILE *file,const unsigned char *bytes,uint32_t length)
{
    for(uint32_t offset=0;offset<length;){uint32_t count=length-offset;if(count>4096)count=4096;
        if(fwrite(bytes+offset,1,count,file)!=count)return false;
        offset+=count;}
    return true;
}
static void header(unsigned char bytes[224],const ouse_avi_writer_t *writer,uint32_t interval,uint32_t size,uint32_t movi_size)
{
    memset(bytes,0,224);
    memcpy(bytes,"RIFF",4);put32(bytes+4,size);memcpy(bytes+8,"AVI LIST",8);put32(bytes+16,192);memcpy(bytes+20,"hdrlavih",8);put32(bytes+28,56);
    put32(bytes+32,interval);put32(bytes+36,writer->largest*(1000000/interval+1));put32(bytes+44,0x10);put32(bytes+48,writer->count);
    put32(bytes+56,1);put32(bytes+60,writer->largest);put32(bytes+64,writer->width);put32(bytes+68,writer->height);
    memcpy(bytes+88,"LIST",4);put32(bytes+92,116);memcpy(bytes+96,"strlstrh",8);put32(bytes+104,56);memcpy(bytes+108,"vidsMJPG",8);
    put32(bytes+128,interval);put32(bytes+132,1000000);put32(bytes+140,writer->count);put32(bytes+144,writer->largest);put32(bytes+148,UINT32_MAX);
    bytes[160]=(unsigned char)writer->width;bytes[161]=(unsigned char)(writer->width>>8);bytes[162]=(unsigned char)writer->height;bytes[163]=(unsigned char)(writer->height>>8);
    memcpy(bytes+164,"strf",4);put32(bytes+168,40);put32(bytes+172,40);put32(bytes+176,writer->width);put32(bytes+180,writer->height);
    bytes[184]=1;bytes[186]=24;memcpy(bytes+188,"MJPG",4);put32(bytes+192,writer->width*writer->height*3);
    memcpy(bytes+212,"LIST",4);put32(bytes+216,movi_size);memcpy(bytes+220,"movi",4);
}
bool ouse_avi_begin(ouse_avi_writer_t *writer,FILE *file,unsigned width,unsigned height,ouse_avi_index_t *index,unsigned capacity)
{
    if(!writer || !file || !index || !capacity || !width || !height || width>2560 || height>1920)return false;
    *writer=(ouse_avi_writer_t){.file=file,.index=index,.capacity=capacity,.width=width,.height=height};
    unsigned char bytes[224];header(bytes,writer,100000,216,4);return fwrite(bytes,1,sizeof(bytes),file)==sizeof(bytes);
}
bool ouse_avi_frame(ouse_avi_writer_t *writer,const void *jpeg,uint32_t length)
{
    if(!writer || !writer->file || !jpeg || length<4 || length>4*1024*1024 || writer->count>=writer->capacity)return false;
    const unsigned char *bytes=jpeg;if(bytes[0]!=0xff || bytes[1]!=0xd8 || bytes[length-2]!=0xff || bytes[length-1]!=0xd9)return false;
    long offset=ftell(writer->file);if(offset<224 || (uint64_t)offset+length+8+16*(uint64_t)(writer->count+1)>1024U*1024*1024)return false;
    if(fwrite("00dc",1,4,writer->file)!=4 || !write32(writer->file,length) || !write_jpeg(writer->file,jpeg,length) || ((length&1) && fputc(0,writer->file)==EOF))return false;
    writer->index[writer->count++]=(ouse_avi_index_t){.offset=(uint32_t)offset-220,.length=length};if(length>writer->largest)writer->largest=length;return true;
}
bool ouse_avi_finish(ouse_avi_writer_t *writer,uint64_t elapsed_us)
{
    if(!writer || !writer->file || !writer->count || ferror(writer->file))return false;
    long movi_end=ftell(writer->file);if(movi_end<224)return false;
    if(fwrite("idx1",1,4,writer->file)!=4 || !write32(writer->file,writer->count*16))return false;
    for(unsigned index=0;index<writer->count;++index)if(fwrite("00dc",1,4,writer->file)!=4 || !write32(writer->file,0x10) || !write32(writer->file,writer->index[index].offset) || !write32(writer->file,writer->index[index].length))return false;
    long end=ftell(writer->file);if(end<0)return false;
    uint64_t duration=elapsed_us/writer->count;if(duration<1000)duration=1000;if(duration>10000000)duration=10000000;
    unsigned char bytes[224];header(bytes,writer,(uint32_t)duration,(uint32_t)end-8,(uint32_t)movi_end-220);
    return fseek(writer->file,0,SEEK_SET)==0 && fwrite(bytes,1,sizeof(bytes),writer->file)==sizeof(bytes) && fseek(writer->file,end,SEEK_SET)==0 && fflush(writer->file)==0;
}
bool ouse_avi_read_info(FILE *file,ouse_avi_info_t *info)
{
    unsigned char bytes[224];if(!file || !info || fseek(file,0,SEEK_SET) || fread(bytes,1,sizeof(bytes),file)!=sizeof(bytes))return false;
    if(memcmp(bytes,"RIFF",4) || memcmp(bytes+8,"AVI ",4) || memcmp(bytes+108,"vidsMJPG",8) || memcmp(bytes+220,"movi",4) || !get32(bytes+48))return false;
    uint32_t size=get32(bytes+216);*info=(ouse_avi_info_t){.width=get32(bytes+64),.height=get32(bytes+68),.interval_us=get32(bytes+32),.end=size+220};
    if(size<4 || size>1024U*1024*1024 || !info->width || !info->height || info->width>2560 || info->height>1920 || info->interval_us<1000 || info->interval_us>10000000)return false;
    if(fseek(file,0,SEEK_END))return false;
    long end=ftell(file);return end>=0 && info->end<=(uint32_t)end && fseek(file,224,SEEK_SET)==0;
}
bool ouse_avi_read_frame(FILE *file,const ouse_avi_info_t *info,void *buffer,size_t capacity,uint32_t *length)
{
    if(!file || !info || !buffer || !length)return false;
    long position=ftell(file);if(position<224 || (uint64_t)position+8>info->end)return false;
    unsigned char bytes[8];if(fread(bytes,1,8,file)!=8 || memcmp(bytes,"00dc",4))return false;
    uint32_t size=get32(bytes+4);if(size<4 || size>capacity || (uint64_t)position+8+size+(size&1)>info->end)return false;
    if(fread(buffer,1,size,file)!=size || ((size&1) && fgetc(file)==EOF))return false;
    *length=size;return true;
}
