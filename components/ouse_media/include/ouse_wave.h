// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
// Streaming RIFF/WAVE PCM16 reader. Fixed 16kHz mono output keeps the board's
// duplex I2S format unchanged. Integer sample selection, not a HiFi resampler.
typedef esp_err_t (*ouse_wave_output_t)(void *context,const int16_t *samples,unsigned count);
typedef struct {
    unsigned char header[40],frame[4];
    unsigned header_size,header_need,stage,channels,rate,frame_size,phase,count;
    uint32_t remaining,data_size,frames,skipped;
    bool format,data,done,pad;
    int16_t output[256];
    ouse_wave_output_t write;
    void *context;
    uint64_t received,riff_bytes,parsed;
} ouse_wave_t;
void ouse_wave_init(ouse_wave_t *wave,ouse_wave_output_t output,void *context);
esp_err_t ouse_wave_feed(ouse_wave_t *wave,const unsigned char *data,size_t size);
esp_err_t ouse_wave_finish(ouse_wave_t *wave);
