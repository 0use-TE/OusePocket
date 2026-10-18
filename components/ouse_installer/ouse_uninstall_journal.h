// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
// Single installer owner; fixed v1 record with CRC. A durable intent is the
// uninstall commit point. Every cleanup stage is idempotent after reboot.
esp_err_t ouse_uninstall_intent_read(char id[48]);
esp_err_t ouse_uninstall_intent_write(const char *id);
esp_err_t ouse_uninstall_intent_clear(void);
esp_err_t ouse_reset_intent_read(char id[48]);
esp_err_t ouse_reset_intent_write(const char *id);
esp_err_t ouse_reset_intent_clear(void);
