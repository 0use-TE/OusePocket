#pragma once
#include <stdbool.h>
#include <stdint.h>
#define OUSE_POINTER_CAPACITY 8
#define OUSE_POINTER_CONTACTS 2
typedef struct {unsigned node,type,pointer_id;uint32_t generation,time;int x,y,dx,dy;} ouse_pointer_event_t;
typedef struct {
    ouse_pointer_event_t events[OUSE_POINTER_CAPACITY];
    unsigned head,count,node;
    uint32_t generation;
    int x,y;
    bool captured;
} ouse_pointer_t;
static inline bool ouse_pointer_push(ouse_pointer_t *pointer,unsigned type,unsigned node,uint32_t generation,int x,int y,uint32_t time)
{
    if(type==0){
        if(pointer->captured || pointer->count>OUSE_POINTER_CAPACITY-2)return false;
        pointer->captured=true;pointer->node=node;pointer->generation=generation;pointer->x=x;pointer->y=y;
    }else if(!pointer->captured || pointer->node!=node || pointer->generation!=generation)return false;
    ouse_pointer_event_t event={.node=node,.type=type,.generation=generation,.time=time,.x=x,.y=y,.dx=x-pointer->x,.dy=y-pointer->y};
    if(type==1){
        if(pointer->count){
            unsigned last=(pointer->head+pointer->count-1)%OUSE_POINTER_CAPACITY;
            if(pointer->events[last].type==1){event.dx+=pointer->events[last].dx;event.dy+=pointer->events[last].dy;pointer->events[last]=event;pointer->x=x;pointer->y=y;return true;}
        }
        if(pointer->count>=OUSE_POINTER_CAPACITY-1)return false;
    }
    pointer->events[(pointer->head+pointer->count++)%OUSE_POINTER_CAPACITY]=event;
    pointer->x=x;pointer->y=y;if(type>=2)pointer->captured=false;return true;
}
static inline bool ouse_pointer_pop(ouse_pointer_t *pointer,ouse_pointer_event_t *event)
{
    if(!pointer->count)return false;
    *event=pointer->events[pointer->head];pointer->head=(pointer->head+1)%OUSE_POINTER_CAPACITY;--pointer->count;return true;
}
