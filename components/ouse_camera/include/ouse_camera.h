#pragma once
#include "ouse_tasks.h"
#ifdef __cplusplus
extern "C" {
#endif
#define OUSE_CAMERA_WIDTH 320
#define OUSE_CAMERA_HEIGHT 240
#define OUSE_PHOTO_PAGE 12
#define OUSE_PHOTO_THUMBNAILS (2*OUSE_PHOTO_PAGE)
#define OUSE_CAMERA_IMAGES (2+OUSE_PHOTO_THUMBNAILS)
typedef struct {const unsigned char *pixels;unsigned width,height,stride;size_t bytes;} ouse_camera_frame_t;
typedef struct {
    esp_err_t (*start)(void);
    esp_err_t (*frame)(ouse_camera_frame_t *);
    void (*put)(void);
    esp_err_t (*stop)(void);
} ouse_camera_driver_t;
typedef enum {OUSE_CAMERA_START,OUSE_CAMERA_CAPTURE,OUSE_PHOTOS_LIST,OUSE_PHOTOS_OPEN,OUSE_CAMERA_RECORD,OUSE_CAMERA_RECORD_STOP,OUSE_PHOTOS_REMOVE} ouse_camera_operation_t;
typedef struct {char path[128];size_t bytes;bool video;int64_t modified;unsigned image;} ouse_photo_entry_t;
typedef struct {
    bool running,busy,in_use,more,recovery_pending,recording,playing;esp_err_t error;uint32_t task_id,revision,offset,recorded_ms;
    unsigned count;char path[128],error_stage[16];ouse_photo_entry_t entries[OUSE_PHOTO_PAGE];
} ouse_camera_state_t;
esp_err_t ouse_camera_init(const ouse_camera_driver_t *driver);
esp_err_t ouse_camera_request(const ouse_task_owner_t *owner,ouse_camera_operation_t operation,const char *path,uint32_t offset,uint32_t *id);
bool ouse_camera_get(const ouse_task_owner_t *owner,ouse_camera_state_t *out);
bool ouse_camera_copy(const ouse_task_owner_t *owner,unsigned image,uint32_t *revision,void *pixels,size_t capacity);
void ouse_camera_stop(const ouse_task_owner_t *owner);
void ouse_photos_stop(const ouse_task_owner_t *owner);
void ouse_camera_forget(const ouse_task_owner_t *owner);
bool ouse_camera_idle(void);
bool ouse_camera_media_active(void);
unsigned ouse_camera_image_width(unsigned image);
unsigned ouse_camera_image_height(unsigned image);
#ifdef __cplusplus
}
#endif
