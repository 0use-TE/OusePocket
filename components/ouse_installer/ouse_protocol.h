// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define OUSE_WIRE_MAX 1200
uint32_t ouse_crc32(const void *data,size_t size);
typedef struct { uint32_t token,sequence; char *verb,*payload; uint32_t checksum; } ouse_frame_t;
// Mutates line after CRC verification; malformed lines are silently discarded.
bool ouse_frame_parse(char *line,ouse_frame_t *frame);
bool ouse_hex_decode(const char *text,unsigned char *out,size_t capacity,size_t *length);
