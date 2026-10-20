#pragma once
#include "ouse_files.h"
#define OUSE_APP_FILE_PATH 192
#define OUSE_APP_FILE_CHUNK 4096
typedef enum {OUSE_APP_FILE_LIST,OUSE_APP_FILE_STAT,OUSE_APP_FILE_READ,OUSE_APP_FILE_WRITE,OUSE_APP_FILE_WRITE_CHUNK,OUSE_APP_FILE_MKDIR,OUSE_APP_FILE_REMOVE,OUSE_APP_FILE_RENAME,OUSE_APP_FILE_DOWNLOAD} ouse_app_file_operation_t;
typedef void (*ouse_app_file_complete_t)(void *,uint32_t,esp_err_t,char *);
esp_err_t ouse_app_files_init(void);
bool ouse_app_files_path(const char *app_id,const char *relative,char *absolute,size_t capacity);
// Native storage workers only; caller must hold an SD lease.
esp_err_t ouse_app_files_prepare(const char *app_id);
esp_err_t ouse_app_files_submit(const ouse_task_owner_t *owner,ouse_app_file_operation_t operation,
    const char *path,uint32_t offset,const void *data,size_t bytes,uint32_t *id,
    ouse_app_file_complete_t complete,void *context);
