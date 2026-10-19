#pragma once
#include "ouse_tasks.h"
#define OUSE_PACKAGE_LIMIT 524288
typedef struct {
    ouse_task_owner_t owner;
    uint32_t task,version_code;
    size_t size;
    bool local;
    char location[1025],app_id[48],sha256[65];
} ouse_package_request_t;
typedef struct {
    uint32_t task;
    size_t received,total;
    bool busy;
    char phase[24],app_id[48];
    esp_err_t error;
} ouse_package_state_t;
esp_err_t ouse_packages_init(void);
esp_err_t ouse_packages_submit(const ouse_package_request_t *request,uint32_t *id);
bool ouse_packages_take(ouse_package_request_t *out);
void ouse_packages_get(ouse_package_state_t *out);
void ouse_packages_progress(const char *phase,size_t received,size_t total,const char *id);
void ouse_packages_finish(esp_err_t error,bool cancelled);
