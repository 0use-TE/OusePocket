#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#define OUSE_BLE_PEERS_MAX 16
typedef struct {char name[49],address[18],service[37];uint8_t type;int8_t rssi;bool bonded;} ouse_ble_peer_t;
typedef struct {
    bool available,enabled,starting,ready,scanning,advertising,connected,encrypted,bonded,pair_pending;
    uint32_t revision,pair_id,number;
    int error;
    char address[18],peer[18];
    unsigned count,bonds;
    ouse_ble_peer_t peers[OUSE_BLE_PEERS_MAX];
} ouse_ble_state_t;
esp_err_t ouse_ble_start(esp_err_t (*prepare_radio)(void));
void ouse_ble_get(ouse_ble_state_t *out);
esp_err_t ouse_ble_enable(bool enabled);
esp_err_t ouse_ble_scan(void);
esp_err_t ouse_ble_discoverable(void);
esp_err_t ouse_ble_connect(const char *address,uint8_t type);
esp_err_t ouse_ble_disconnect(void);
esp_err_t ouse_ble_forget(const char *address,uint8_t type);
esp_err_t ouse_ble_confirm(uint32_t pair_id,bool accept);
