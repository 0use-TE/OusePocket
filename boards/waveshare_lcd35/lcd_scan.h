// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_lcd_panel_io.h"
// Caller owns the board LCD IO lock; all reads restore the pixel write clock.
bool ouse_board_lcd_scan_raw(int command,uint8_t *data,unsigned size);
esp_err_t ouse_board_lcd_sync_send(const void *image,bool landscape,uint8_t *dma[2]);
void ouse_board_lcd_sync_invalidate(void);
