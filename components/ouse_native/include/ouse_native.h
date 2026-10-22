// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool active, stop_requested, reported, installing;
    uint32_t task_id;
    int32_t result, exit_code;
    esp_err_t error;
    size_t file_bytes;
    int64_t started_us, finished_us;
    char phase[16], path[128];
} ouse_native_state_t;

// Initialize once from app_main, before the installer starts.
esp_err_t ouse_native_init(void);
// Starts asynchronously; only one module can be active. Downloads/*.elf on SD.
esp_err_t ouse_native_start(const char *path, uint32_t *task_id);
// Copies an uploaded image into private PSRAM before returning. The caller
// always retains ownership of the supplied buffer. Same asynchronous lifecycle.
esp_err_t ouse_native_start_image(const uint8_t *image, size_t bytes, uint32_t *task_id);
// Desktop slot: use the internally saved program, or the bundled demo when
// no program has been installed. File I/O runs on the native worker.
esp_err_t ouse_native_start_desktop(uint32_t *task_id);
// Validate and atomically save an image in internal storage, without executing
// it. Copies input; the package task is noncancellable once accepted.
esp_err_t ouse_native_install_image(const uint8_t *image, size_t bytes, uint32_t *task_id);
// Cooperative cancellation. Never deletes a task executing module instructions.
esp_err_t ouse_native_stop(void);
esp_err_t ouse_native_stop_task(uint32_t task_id);
void ouse_native_get(ouse_native_state_t *out);
