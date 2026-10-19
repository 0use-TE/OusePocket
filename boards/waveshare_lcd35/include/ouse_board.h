// SPDX-License-Identifier: MIT
#pragma once
#include "ouse_core.h"

#ifdef __cplusplus
extern "C" {
#endif
esp_err_t ouse_board_init(ouse_core_port_t *port);
void ouse_board_start_services(void); // after desktop READY, never under UI lock
esp_err_t ouse_board_power_off(void);
#ifdef __cplusplus
}
#endif
