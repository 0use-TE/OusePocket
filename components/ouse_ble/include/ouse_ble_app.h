#pragma once
#include "ouse_tasks.h"
#define OUSE_BLE_DATA_MAX 64
#define OUSE_BLE_EVENTS_MAX 16
typedef enum {OUSE_BLE_APP_SCAN,OUSE_BLE_APP_CONNECT,OUSE_BLE_APP_DISCONNECT,OUSE_BLE_APP_SERVICES,OUSE_BLE_APP_CHARACTERISTICS,OUSE_BLE_APP_DESCRIPTORS,OUSE_BLE_APP_READ,OUSE_BLE_APP_WRITE,OUSE_BLE_APP_SUBSCRIBE,OUSE_BLE_APP_KEYBOARD,OUSE_BLE_APP_CONSUMER,OUSE_BLE_APP_MOUSE,OUSE_BLE_APP_RELEASE,OUSE_BLE_APP_PAIR} ouse_ble_app_operation_t;
typedef struct {char type[20],uuid[37];uint16_t handle,end;unsigned properties,size;int error;bool truncated;uint8_t data[OUSE_BLE_DATA_MAX];} ouse_ble_event_t;
esp_err_t ouse_ble_app_open(const ouse_task_owner_t *owner,bool hid,uint32_t *session);
esp_err_t ouse_ble_app_request(const ouse_task_owner_t *owner,uint32_t session,ouse_ble_app_operation_t operation,const char *address,uint8_t type,uint16_t handle,uint16_t end,const void *data,size_t size);
bool ouse_ble_app_poll(const ouse_task_owner_t *owner,uint32_t session,ouse_ble_event_t *out,unsigned *dropped);
void ouse_ble_app_close(const ouse_task_owner_t *owner);
