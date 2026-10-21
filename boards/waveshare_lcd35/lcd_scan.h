// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_lcd_panel_io.h"
// Caller owns the board LCD IO lock; all reads restore the pixel write clock.
bool ouse_board_lcd_scan_raw(int command,uint8_t *data,unsigned size);
bool ouse_board_lcd_frame_window(int64_t *deadline);
void ouse_board_lcd_sync_invalidate(void);
