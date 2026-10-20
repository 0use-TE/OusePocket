#pragma once
#include "ouse_core.h"
void ouse_screen_init(const ouse_core_port_t *port,uint8_t brightness);
esp_err_t ouse_screen_brightness(uint8_t value);
esp_err_t ouse_screen_sleep(void);
esp_err_t ouse_screen_wake(void);
// UI task only: feed the same input device as physical touch.
esp_err_t ouse_screen_inject(int x,int y,bool pressed);

esp_err_t ouse_screen_inject_contact(unsigned slot,int x,int y,bool pressed);
