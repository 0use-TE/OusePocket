// OusePocket adaptation; standard C replacements avoid board-specific SDK ABI.
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#define MALLOC_CAP_INTERNAL 1
#define heap_caps_malloc(n,c) ((c)&MALLOC_CAP_INTERNAL?NULL:malloc(n))
#define heap_caps_free(p) free(p)
#define heap_caps_get_free_size(c) 0
#define heap_caps_get_largest_free_block(c) 0
