// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include <stdbool.h>
typedef struct {bool ready,busy,recovery_pending;esp_err_t error;} ouse_installer_state_t;
void ouse_installer_get_state(ouse_installer_state_t *out);
// Start once, after DESKTOP READY. UART0 RX is exclusively owned by this service.
// No board reset, console input task, shared timer scheduler or periodic USB pump.
esp_err_t ouse_installer_start(void);
