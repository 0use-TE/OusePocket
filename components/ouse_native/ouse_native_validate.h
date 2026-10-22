// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>
#define OUSE_NATIVE_FILE_LIMIT (1024u * 1024u)
#define OUSE_NATIVE_IMAGE_LIMIT (8u * 1024u * 1024u)
// Bounds-check the supported ELF32 RISC-V subset before the upstream loader,
// whose relocate API has no input-length argument. Returns ABI 1/2, or 0.
int ouse_native_validate(const uint8_t *data, size_t length);
