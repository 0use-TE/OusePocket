// SPDX-License-Identifier: MIT
#pragma once
// Display lists contain only copied data and package-owned image references.
// No JS values or pointers supplied by applications cross into the UI thread.
#define CANVAS_MAX_COMMANDS 256
#define CANVAS_TEXT_BYTES 128
typedef enum { CANVAS_FILL, CANVAS_STROKE, CANVAS_LINE, CANVAS_ARC,
    CANVAS_IMAGE, CANVAS_TEXT, CANVAS_TRIANGLE } canvas_op_t;
typedef struct {
    canvas_op_t op;
    int x,y,w,h,x2,y2,x3,y3,radius,line_width,start,end;
    int sx,sy,sw,sh;
    uint32_t color;
    unsigned opacity;
    const lv_image_dsc_t *image;
    char text[CANVAS_TEXT_BYTES+1];
} canvas_command_t;
typedef struct { unsigned count; canvas_command_t commands[]; } canvas_frame_t;
static inline size_t canvas_frame_bytes(unsigned count)
{ return sizeof(canvas_frame_t)+count*sizeof(canvas_command_t); }
