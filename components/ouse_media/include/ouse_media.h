// SPDX-License-Identifier: MIT
#pragma once
#include "ouse_tasks.h"
#ifdef __cplusplus
extern "C" {
#endif
#define OUSE_MEDIA_BUFFER_BYTES 8192
#define OUSE_MEDIA_ASSET_MAX (512 * 1024)
typedef struct {
    char app_id[48];
    uint32_t task_id,revision;
    bool active,paused,interrupted,buffering;
    unsigned position_ms,duration_ms,buffered_bytes;
    esp_err_t error;
} ouse_media_state_t;
// Boot once; producer owns transport/parser, existing board task owns codec.
esp_err_t ouse_media_init(void);
bool ouse_media_available(void);
// Exactly one source: http(s) URL or copied package binary asset. First format
// is WAV PCM16 mono/stereo 8..48kHz, converted to board 16kHz mono natively.
// One global session; busy rejects, never silently replaces another app's song.
// Native playback survives Home; the runtime stops it on explicit app close.
esp_err_t ouse_media_play(const ouse_task_owner_t *owner,const char *url,const void *data,size_t size,uint32_t *id);
bool ouse_media_get(const char *app_id,ouse_media_state_t *out);
esp_err_t ouse_media_set_info(const char *app_id,const char *title,const char *artist);
esp_err_t ouse_media_pause(const char *app_id,bool paused);
esp_err_t ouse_media_stop(const char *app_id);
esp_err_t ouse_media_stop_owner(const ouse_task_owner_t *owner);
// Installer worker only, after closing an uninstalled app. Await actual cleanup.
esp_err_t ouse_media_purge(const char *app_id);
// Sole board codec consumer. No JS/UI pointers are ever exchanged.
void ouse_media_consumer_started(uint32_t id);
unsigned ouse_media_read(uint32_t id,int16_t *samples,unsigned capacity,bool *done,esp_err_t *error);
void ouse_media_consumed(uint32_t id,unsigned samples);
void ouse_media_interrupted(uint32_t id,bool interrupted);
void ouse_media_consumer_done(uint32_t id,esp_err_t error,bool cancelled);
#ifdef __cplusplus
}
#endif
