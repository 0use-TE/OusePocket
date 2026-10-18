#pragma once
#include <stddef.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
typedef enum {OUSE_MEMORY_NORMAL,OUSE_MEMORY_WARNING,OUSE_MEMORY_CRITICAL} ouse_memory_pressure_t;
static inline ouse_memory_pressure_t ouse_memory_classify(size_t internal,size_t largest,size_t external)
{
    if(internal<32*1024 || largest<8*1024 || external<512*1024)return OUSE_MEMORY_CRITICAL;
    if(internal<64*1024 || largest<16*1024 || external<1024*1024)return OUSE_MEMORY_WARNING;
    return OUSE_MEMORY_NORMAL;
}
static inline ouse_memory_pressure_t ouse_memory_pressure(void)
{
#ifdef ESP_PLATFORM
    return ouse_memory_classify(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
#elif defined(OUSE_MEMORY_PRESSURE_TEST)
    extern ouse_memory_pressure_t ouse_test_memory_pressure;
    return ouse_test_memory_pressure;
#else
    return OUSE_MEMORY_NORMAL;
#endif
}
