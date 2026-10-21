// C interface to the upstream LGPL-2.1-or-later differential engine.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
bool ouse_lcd_diff_init(void);
void ouse_lcd_diff_invalidate(void);
int ouse_lcd_diff_prepare(const void *image,bool landscape);
int ouse_lcd_diff_next(int scanline,int *x,int *y,int *pixels);
const uint16_t *ouse_lcd_diff_pixels(void);
#ifdef __cplusplus
}
#endif
