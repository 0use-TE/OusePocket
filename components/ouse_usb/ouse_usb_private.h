// SPDX-License-Identifier: MIT
#pragma once
#include "ouse_usb.h"
#include "tinyusb.h"
void ouse_usb_descriptors(tinyusb_config_t *config);
esp_err_t ouse_usb_network_start(void);
void ouse_usb_network_poll(bool enabled);
void ouse_usb_network_snapshot(ouse_usb_state_t *state);
