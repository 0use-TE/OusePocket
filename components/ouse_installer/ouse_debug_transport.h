// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include <stddef.h>
esp_err_t ouse_debug_command(const char *verb,const char *payload,char *result,size_t capacity);
void ouse_debug_poll(void);
