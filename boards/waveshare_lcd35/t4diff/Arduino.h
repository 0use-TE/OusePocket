// Minimal build adapter for the unmodified upstream diff engine. No Arduino runtime.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "esp_timer.h"
#define FLASHMEM
#ifndef __GNUC__
#define T4DIFF_ALWAYS_INLINE
#define T4DIFF_INLINE_COMPUTE_KERNEL inline
#endif
class elapsedMicros {
    int64_t start=esp_timer_get_time();
public:
    operator uint32_t() const {return (uint32_t)(esp_timer_get_time()-start);}
};
class Stream {
public:
    template<class T> void print(const T&) {}
    template<class T> void print(const T&,int) {}
};
inline Stream Serial;
