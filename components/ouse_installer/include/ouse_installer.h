// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
// Start once, after DESKTOP READY. UART0 RX is exclusively owned by this service.
// No board reset, console input task, shared timer scheduler or periodic USB pump.
esp_err_t ouse_installer_start(void);
