#pragma once
#include "ouse_tasks.h"
#define OUSE_CHANNEL_MAX 2
#define OUSE_CHANNEL_BYTES 2048
#define OUSE_CHANNEL_EVENTS 8
typedef enum {OUSE_CHANNEL_WEBSOCKET,OUSE_CHANNEL_MQTT,OUSE_CHANNEL_PING} ouse_channel_kind_t;
typedef struct {char url[513],username[65],password[129],client_id[65];} ouse_channel_config_t;
typedef struct {uint32_t id;bool connected,closed;esp_err_t error;unsigned dropped;} ouse_channel_state_t;
typedef struct {char type[20],topic[129];unsigned char data[OUSE_CHANNEL_BYTES];size_t size;bool binary;double value;} ouse_channel_event_t;
esp_err_t ouse_channels_init(void);
esp_err_t ouse_channel_open(const ouse_task_owner_t *owner,ouse_channel_kind_t kind,const ouse_channel_config_t *config,uint32_t *id);
esp_err_t ouse_channel_send(const ouse_task_owner_t *owner,uint32_t id,const char *topic,const void *data,size_t size,bool binary,int qos,bool retain);
esp_err_t ouse_channel_subscribe(const ouse_task_owner_t *owner,uint32_t id,const char *topic,int qos);
bool ouse_channel_get(const ouse_task_owner_t *owner,uint32_t id,ouse_channel_state_t *state);
bool ouse_channel_poll(const ouse_task_owner_t *owner,uint32_t id,ouse_channel_event_t *event);
bool ouse_channel_close(const ouse_task_owner_t *owner,uint32_t id);
void ouse_channels_close_owner(const ouse_task_owner_t *owner);
