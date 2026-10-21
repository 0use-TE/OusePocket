// SPDX-License-Identifier: LGPL-2.1-or-later
// Adapter for Arvind Singh's ILI9341_T4 diff engine; see t4diff/README.md.
#include "lcd_diff.h"
#include "esp_heap_caps.h"
#ifdef __GNUC__
// Upstream's statistics use volatile counters; retain the pinned source under
// IDF's newer C++ standard without changing its original counter operations.
#pragma GCC diagnostic ignored "-Wvolatile"
#endif
#include "t4diff/DiffBuffTemplate.h"
#include <new>

namespace {
struct Geometry {static constexpr int LX=320,LY=480,MAX_WRITE_LINE=20,MIN_SCANLINE_SPACE=16;};
using Diff=T4Diff::DiffBuffT<Geometry>;
using Base=T4Diff::DiffBuffBaseT<Geometry>;
using Full=T4Diff::DiffBuffDummyT<Geometry>;
alignas(Diff) unsigned char object[sizeof(Diff)];
Diff *diff;
Full full;
Base *current;
uint16_t *mirror;
bool valid;
}
bool ouse_lcd_diff_init(void)
{
    if(diff)return true;
    mirror=(uint16_t*)heap_caps_malloc(320*480*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    auto buffer=(uint8_t*)heap_caps_malloc(8192,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!mirror || !buffer){heap_caps_free(mirror);mirror=nullptr;heap_caps_free(buffer);return false;}
    diff=new(object) Diff(buffer,8192);
    return true;
}
void ouse_lcd_diff_invalidate(void){valid=false;}
int ouse_lcd_diff_prepare(const void *image,bool landscape)
{
    current=valid?(Base*)diff:(Base*)&full;
    // In landscape, logical (x,y) maps to canonical (319-y,x), matching
    // the board's MX portrait / MV landscape MADCTL settings.
    current->computeDiff(mirror,(const uint16_t*)image,landscape?1:0,6,true,0);
    valid=true;
    return current->scanlineStartInit();
}
int ouse_lcd_diff_next(int scanline,int *x,int *y,int *pixels)
{return current->readDiff(*x,*y,*pixels,scanline);}
const uint16_t *ouse_lcd_diff_pixels(void){return mirror;}
