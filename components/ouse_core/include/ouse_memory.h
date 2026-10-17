#pragma once
#include <stdlib.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_attr.h"
#define OUSE_PSRAM_BSS EXT_RAM_BSS_ATTR
#else
#define OUSE_PSRAM_BSS
#endif

static inline void *ouse_psram_malloc(size_t size)
{
#ifdef ESP_PLATFORM
    return heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    return malloc(size);
#endif
}

static inline void *ouse_psram_calloc(size_t count,size_t size)
{
#ifdef ESP_PLATFORM
    return heap_caps_calloc(count,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    return calloc(count,size);
#endif
}
