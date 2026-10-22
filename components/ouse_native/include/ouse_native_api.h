// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

#define OUSE_NATIVE_ABI 1u
#define OUSE_NATIVE_MAGIC 0x54414e4fu /* ONAT, little endian */

// ABI v1: all callbacks are synchronous and may only be called on the entry
// task. Do not keep these pointers, create threads, or retain resources after
// app_main returns. Native modules are trusted code, not a sandbox.
typedef struct {
    uint32_t version, size;
    void (*log)(const char *text);
    uint32_t (*millis)(void);
    int (*should_stop)(void);
    void (*sleep_ms)(uint32_t milliseconds);
    void (*report)(int32_t value);
} ouse_native_api_t;

#ifdef __cplusplus
extern "C" {
#endif
const ouse_native_api_t *ouse_native_get_api(uint32_t version);
#ifdef __cplusplus
}
#endif

// Include once in a module. The build tool keeps this section during linking.
#define OUSE_NATIVE_APP() \
    __attribute__((used, section(".ouse_abi"), visibility("default"))) \
    const uint32_t ouse_native_abi[3] = { \
        OUSE_NATIVE_MAGIC, OUSE_NATIVE_ABI, sizeof(ouse_native_api_t) }
