// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool available,enabled,mounted,suspended,speaker,microphone,audio_running,audio_enabled;
    bool network_enabled,network_up,network_stopping,uplink,subnet_conflict;
    uint32_t audio_task,network_task,session,underruns,overruns,rx_packets,tx_packets,dropped_packets;
    uint32_t speaker_samples,speaker_nonzero,speaker_peak;
    esp_err_t error,network_error;
    char ip[16];
} ouse_usb_state_t;

// Native OTG HS Type-C, not the CH343 UART installer. Setters only submit state.
esp_err_t ouse_usb_start(void);
void ouse_usb_get(ouse_usb_state_t *state);
esp_err_t ouse_usb_set_enabled(bool enabled);
// Gate codec use without disconnecting the USB device (connection stays detectable).
esp_err_t ouse_usb_set_audio(bool enabled);
esp_err_t ouse_usb_set_network(bool enabled);
// Worker boundary: the board's sole codec worker owns all hardware I/O.
bool ouse_usb_audio_requested(void);
bool ouse_usb_microphone_requested(void);
unsigned ouse_usb_speaker_read(int16_t *samples,unsigned count);
void ouse_usb_microphone_write(const int16_t *samples,unsigned count);
void ouse_usb_audio_running(uint32_t id,bool running,esp_err_t error);
#ifdef __cplusplus
}
#endif
