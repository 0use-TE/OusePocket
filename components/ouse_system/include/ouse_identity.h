// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    bool ready,pending,pin_enabled,setup_done,verified;
    uint8_t setup_step;
    char name[25]; // Up to 24 UTF-8 bytes; also used as BLE scan-response name.
    uint32_t revision,completed,retry_seconds;
    esp_err_t error;
} ouse_identity_state_t;
esp_err_t ouse_identity_init(void);
void ouse_identity_get(ouse_identity_state_t *out);
// Nonblocking. Completion is reported in completed/error/verified, never log PINs.
esp_err_t ouse_identity_name(const char *name,uint32_t *request);
esp_err_t ouse_identity_step(unsigned step,uint32_t *request); // 1 Wi-Fi, 2 PIN, 3 guide, 4 done
esp_err_t ouse_identity_pin(const char *old_pin,const char *new_pin,uint32_t *request);
esp_err_t ouse_identity_verify(const char *pin,uint32_t *request);
bool ouse_identity_valid_name(const char *name);
bool ouse_identity_valid_pin(const char *pin);
