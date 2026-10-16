// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
#define OUSE_WIFI_AP_MAX 12
typedef struct {
    bool available,valid,present,external,charging,full,asleep;
    int percent,millivolts;
    unsigned sleep_seconds;
    esp_err_t error;
    uint32_t revision;
} ouse_power_state_t;
typedef enum { OUSE_AUDIO_IDLE,OUSE_AUDIO_STARTING,OUSE_AUDIO_TONE,OUSE_AUDIO_RECORDING,
    OUSE_AUDIO_PLAYBACK,OUSE_AUDIO_ERROR } ouse_audio_mode_t;
typedef struct {
    bool available,muted;
    unsigned volume,peak,recorded_ms,position_ms;
    ouse_audio_mode_t mode;
    esp_err_t error;
    uint32_t revision,request_id,completed_id;
} ouse_audio_state_t;
typedef struct { char ssid[33]; int rssi; uint8_t channel; bool secured; } ouse_wifi_ap_t;
typedef struct {
    bool available,enabled,ready,scanning,connecting,connected;
    char ssid[33],ip[16];
    int rssi,reason;
    unsigned count;
    ouse_wifi_ap_t aps[OUSE_WIFI_AP_MAX];
    esp_err_t error;
    uint32_t revision,request_id,completed_id;
} ouse_wifi_state_t;
// All get functions copy cached state. Requests enqueue/coalesce, never do I/O.
esp_err_t ouse_power_get(ouse_power_state_t *state);
esp_err_t ouse_power_set_timeout(unsigned seconds); // 0, 15, 30, 60, 120, 300
void ouse_power_set_asleep(bool asleep); // UI task, backlight handled by core
esp_err_t ouse_audio_get(ouse_audio_state_t *state);
esp_err_t ouse_audio_set_volume(unsigned volume); // 0..100
esp_err_t ouse_audio_set_muted(bool muted);
esp_err_t ouse_audio_test_tone(void);
esp_err_t ouse_audio_record(unsigned seconds); // 1..3, mono PCM in RAM only
esp_err_t ouse_audio_play_recording(void);
esp_err_t ouse_audio_stop(void);
esp_err_t ouse_wifi_get(ouse_wifi_state_t *state);
esp_err_t ouse_wifi_set_enabled(bool enabled);
esp_err_t ouse_wifi_scan(void);
esp_err_t ouse_wifi_connect(const char *ssid,const char *password);
esp_err_t ouse_wifi_disconnect(void);
esp_err_t ouse_wifi_forget(void);
uint32_t ouse_devices_revision(unsigned group); // power=0 audio=1 wifi=2
typedef struct { bool pending; esp_err_t error; uint32_t revision; } ouse_device_save_state_t;
void ouse_devices_save_state(ouse_device_save_state_t *state);
void ouse_devices_retry_save(void);
#ifdef __cplusplus
}
#endif
