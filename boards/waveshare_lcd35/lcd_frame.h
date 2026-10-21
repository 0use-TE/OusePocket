// SPDX-License-Identifier: MIT
#pragma once
#include "lvgl.h"
#include "esp_lcd_panel_io.h"

esp_lcd_panel_io_handle_t ouse_board_lcd_frame_io(void);
bool ouse_board_lcd_io_lock_init(void);
void ouse_board_lcd_io_lock(void);
void ouse_board_lcd_io_unlock(void);
void ouse_board_lcd_worker_dma(bool active);
void ouse_board_lcd_frame_init(lv_display_t *display);
void ouse_board_lcd_frame_orientation_begin(void);
void ouse_board_lcd_frame_orientation_end(bool landscape);
