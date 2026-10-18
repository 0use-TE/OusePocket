#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {uint32_t offset,length;} ouse_avi_index_t;
typedef struct {
    FILE *file;
    ouse_avi_index_t *index;
    uint32_t capacity,count,width,height,largest;
} ouse_avi_writer_t;
typedef struct {uint32_t width,height,interval_us,end;} ouse_avi_info_t;
bool ouse_avi_begin(ouse_avi_writer_t *writer,FILE *file,unsigned width,unsigned height,ouse_avi_index_t *index,unsigned capacity);
bool ouse_avi_frame(ouse_avi_writer_t *writer,const void *jpeg,uint32_t length);
bool ouse_avi_finish(ouse_avi_writer_t *writer,uint64_t elapsed_us);
bool ouse_avi_read_info(FILE *file,ouse_avi_info_t *info);
bool ouse_avi_read_frame(FILE *file,const ouse_avi_info_t *info,void *buffer,size_t capacity,uint32_t *length);
