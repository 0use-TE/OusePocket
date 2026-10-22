// SPDX-License-Identifier: MIT
#pragma once
#include "ouse_app.h"
#include <stdbool.h>
#include <stdint.h>
const ouse_app_definition_t *ouse_quake2_app(void);
esp_err_t ouse_native_start_quake2(uint32_t *id);
esp_err_t ouse_native_install_quake2(const uint8_t *image,size_t bytes,uint32_t *id);
// Runtime-only hooks; one native worker owns a game session.
esp_err_t ouse_game_begin(void);
void ouse_game_end(void);
esp_err_t ouse_game_register_symbols(void);
bool ouse_game_copy_frame(uint16_t *destination,unsigned width,unsigned height,unsigned *serial);
esp_err_t ouse_game_execute(int (*entry)(int,char **),int argc,char **argv,int *exit_code);
// Serial input is a lease: held keys are released after 750ms without a
// heartbeat. Call with both down/up or refresh held keys while controlling.
esp_err_t ouse_game_input(unsigned key,bool down,int dx,int dy);
void ouse_game_release_input(void);
int64_t ouse_game_input_activity(void);
void ouse_game_get_stats(unsigned *frames,unsigned *keys,unsigned *allocations,size_t *bytes);
