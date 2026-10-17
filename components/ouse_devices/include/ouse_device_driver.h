// SPDX-License-Identifier: MIT
// Board worker boundary; credentials are never included in public snapshots.
#pragma once
#include "ouse_devices.h"
typedef struct {
    unsigned sleep_seconds,volume;
    bool muted,wifi_enabled;
    char ssid[33],password[65];
} ouse_device_config_t;
typedef struct {
    void (*lock)(void);
    void (*unlock)(void);
    void (*notify)(unsigned worker); // 0 persistence/power, 1 audio, 2 Wi-Fi
} ouse_device_port_t;
typedef struct { uint32_t id; ouse_audio_action_t action; unsigned seconds; } ouse_audio_request_t;
typedef struct {
    uint32_t id;
    ouse_wifi_action_t action;
    bool enabled;
    char ssid[33],password[65];
} ouse_wifi_request_t;
esp_err_t ouse_devices_init(const ouse_device_port_t *port,const ouse_device_config_t *config);
void ouse_devices_get_config(ouse_device_config_t *config);
void ouse_devices_set_available(unsigned worker,bool available,esp_err_t error);
void ouse_devices_publish_power(const ouse_power_state_t *state);
bool ouse_devices_take_audio(ouse_audio_request_t *request);
esp_err_t ouse_devices_queue_media(uint32_t task_id);
bool ouse_devices_take_alarm(ouse_audio_request_t *request);
// Resume the preserved media request after the alarm task has stopped. Nested
// consumer resumes in place; top-level worker enqueues the unclaimed consumer.
uint32_t ouse_devices_resume_media(bool enqueue);
bool ouse_devices_audio_current(uint32_t id);
void ouse_devices_publish_audio(const ouse_audio_state_t *state,uint32_t id);
bool ouse_devices_take_wifi(ouse_wifi_request_t *request);
bool ouse_devices_wifi_current(uint32_t id);
void ouse_devices_publish_wifi(const ouse_wifi_state_t *state,uint32_t id);
void ouse_devices_remember_wifi(const ouse_wifi_request_t *request);
bool ouse_devices_take_save(ouse_device_config_t *config,uint32_t *id);
void ouse_devices_saved(uint32_t id,esp_err_t error);
