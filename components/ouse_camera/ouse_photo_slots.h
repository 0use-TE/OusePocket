#pragma once
#include "ouse_camera.h"
#include <string.h>
// Never overwrite a slot still referenced by the displayed page. One spare
// page accommodates even a completely different page without black frames.
static inline bool ouse_photo_slots(ouse_photo_entry_t *next,unsigned count,
    const ouse_photo_entry_t *old,unsigned old_count,bool *cached)
{
    if(count>OUSE_PHOTO_PAGE || old_count>OUSE_PHOTO_PAGE)return false;
    uint32_t used=0;
    for(unsigned i=0;i<old_count;++i){if(old[i].image>=OUSE_PHOTO_THUMBNAILS)return false;used|=1u<<old[i].image;}
    for(unsigned i=0;i<count;++i){
        cached[i]=false;
        for(unsigned j=0;j<old_count;++j)if(!strcmp(next[i].path,old[j].path) && next[i].bytes==old[j].bytes && next[i].modified==old[j].modified){
            next[i].image=old[j].image;cached[i]=true;break;
        }
        if(!cached[i]){
            unsigned slot=0;while(slot<OUSE_PHOTO_THUMBNAILS && (used&(1u<<slot)))++slot;
            if(slot==OUSE_PHOTO_THUMBNAILS)return false;
            next[i].image=slot;used|=1u<<slot;
        }
    }
    return true;
}
