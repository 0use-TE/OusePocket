// SPDX-License-Identifier: MIT
#include "ouse_system.h"
#include "ouse_tasks.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include <string.h>

typedef struct { uint8_t brightness, theme, locale, font_percent; uint32_t sequence; } save_request_t;
static QueueHandle_t requests;
static SemaphoreHandle_t producers;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static ouse_settings_state_t settings;
static uint32_t sequence, flash_bytes;
static bool initialized;
static struct {uint32_t largest[2],minimum[2];uint64_t sampled_ms;} memory;
void ouse_system_sample_memory(void)
{
    uint32_t largest[2],minimum[2];const uint32_t caps[]={MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT};
    for(unsigned i=0;i<2;++i){largest[i]=heap_caps_get_largest_free_block(caps[i]);minimum[i]=heap_caps_get_minimum_free_size(caps[i]);}
    portENTER_CRITICAL(&state_lock);memcpy(memory.largest,largest,sizeof(largest));memcpy(memory.minimum,minimum,sizeof(minimum));memory.sampled_ms=esp_timer_get_time()/1000;portEXIT_CRITICAL(&state_lock);
}

static void settings_worker(void *argument)
{
    (void)argument;
    save_request_t request;
    for (;;) {
        if (xQueueReceive(requests, &request, portMAX_DELAY) != pdTRUE) continue;
        uint32_t task_id=0;
        const ouse_task_owner_t owner={.app_id="ouse.system"};
        // Persistence must remain functional even if diagnostic capacity is
        // exhausted; the rejected counter makes a missing record explicit.
        if(ouse_task_create(&owner,OUSE_TASK_SETTINGS,"appearanceSave",OUSE_TASK_FINISH_ON_CLOSE,false,&task_id)==ESP_OK)
            (void)ouse_task_start(task_id,false);
        nvs_handle_t handle;
        esp_err_t error = nvs_open("ouse_core", NVS_READWRITE, &handle);
        if (error == ESP_OK) {
            error = nvs_set_u8(handle, "brightness", request.brightness);
            if (error == ESP_OK) error = nvs_set_u8(handle, "theme", request.theme);
            if (error == ESP_OK) error = nvs_set_u8(handle, "locale", request.locale);
            if (error == ESP_OK) error = nvs_commit(handle);
            nvs_close(handle);
        }
        if(task_id)ouse_task_finish(task_id,error,false);
        portENTER_CRITICAL(&state_lock);
        if (error == ESP_OK) { settings.saved_brightness = request.brightness; settings.saved_theme = request.theme; settings.saved_locale=request.locale; settings.saved_font_percent=request.font_percent; }
        if (sequence == request.sequence) {
            settings.pending = false;
            settings.last_error = error;
        }
        portEXIT_CRITICAL(&state_lock);
        if (error == ESP_OK) ESP_LOGI("OuseSystem", "Settings committed: brightness=%u%% theme=%u locale=%u font=%u%%",
                                    request.brightness,request.theme,request.locale,request.font_percent);
        else ESP_LOGE("OuseSystem", "Settings commit failed: %s", esp_err_to_name(error));
    }
}

esp_err_t ouse_system_init(uint8_t *brightness)
{
    if (!brightness || *brightness < 10 || *brightness > 100) return ESP_ERR_INVALID_ARG;
    if (initialized) return ESP_ERR_INVALID_STATE;
    initialized = true;
    settings = (ouse_settings_state_t){.requested_brightness = *brightness, .requested_font_percent=100,.saved_font_percent=100};
    esp_err_t error = esp_flash_get_size(NULL, &flash_bytes);
    if (error != ESP_OK) { settings.last_error = error; return error; }
    error = nvs_flash_init();
    if (error != ESP_OK) { settings.last_error = error; return error; }
    nvs_handle_t handle;
    error = nvs_open("ouse_core", NVS_READONLY, &handle);
    if (error == ESP_OK) {
        uint8_t saved = 0;
        error = nvs_get_u8(handle, "brightness", &saved);
        uint8_t theme=0;
        esp_err_t theme_error=nvs_get_u8(handle,"theme",&theme);
        if (theme_error==ESP_OK && theme<=1) settings.requested_theme=settings.saved_theme=theme;
        uint8_t locale=0;
        esp_err_t le=nvs_get_u8(handle,"locale",&locale);
        if (le!=ESP_OK && le!=ESP_ERR_NVS_NOT_FOUND) {
            nvs_close(handle); settings.last_error=le; return settings.last_error;
        }
        if (le==ESP_OK && locale<=1) settings.requested_locale=settings.saved_locale=locale;
        nvs_close(handle);
        if (theme_error!=ESP_OK && theme_error!=ESP_ERR_NVS_NOT_FOUND) { settings.last_error=theme_error; return theme_error; }
        if (error == ESP_OK && saved >= 10 && saved <= 100) {
            *brightness = saved;
            settings.saved_brightness = saved;
        } else if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
            settings.last_error = error; return error;
        }
    } else if (error != ESP_ERR_NVS_NOT_FOUND) {
        settings.last_error = error; return error;
    }
    settings.requested_brightness = *brightness;
    requests = xQueueCreate(1, sizeof(save_request_t));
    producers = xSemaphoreCreateMutex();
    if (!requests || !producers || xTaskCreate(settings_worker, "ouse_settings", 4096, NULL, 2, NULL) != pdPASS) {
        if (requests) vQueueDelete(requests);
        if (producers) vSemaphoreDelete(producers);
        requests = NULL; producers = NULL;
        settings.last_error = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI("OuseSystem", "Settings ready; initial brightness=%u%%", *brightness);
    return ESP_OK;
}

static esp_err_t request_settings(int brightness, int theme, int locale, int font_percent)
{
    if (!requests) return ESP_ERR_INVALID_STATE;
    if(!ouse_tasks_submission_begin())return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(producers, 0) != pdTRUE){ouse_tasks_submission_end();return ESP_ERR_TIMEOUT;}
    portENTER_CRITICAL(&state_lock);
    if (brightness<0) brightness=settings.requested_brightness;
    if (theme<0) theme=settings.requested_theme;
    if (locale<0) locale=settings.requested_locale;
    if (font_percent<0) font_percent=settings.requested_font_percent;
    if (!settings.pending && settings.last_error == ESP_OK && settings.saved_brightness == brightness && settings.saved_theme == theme && settings.saved_locale==locale && settings.saved_font_percent==font_percent) {
        portEXIT_CRITICAL(&state_lock);
        xSemaphoreGive(producers);
        ouse_tasks_submission_end();
        return ESP_OK;
    }
    save_request_t request = {.brightness = (uint8_t)brightness, .theme = (uint8_t)theme, .locale=(uint8_t)locale, .font_percent=(uint8_t)font_percent, .sequence = ++sequence};
    settings.requested_brightness = (uint8_t)brightness;
    settings.requested_theme = (uint8_t)theme;
    settings.requested_locale=(uint8_t)locale; settings.requested_font_percent=(uint8_t)font_percent;
    settings.pending = true;
    settings.last_error = ESP_OK;
    portEXIT_CRITICAL(&state_lock);
    xQueueOverwrite(requests, &request);
    xSemaphoreGive(producers);
    ouse_tasks_submission_end();
    return ESP_OK;
}

esp_err_t ouse_system_request_brightness(uint8_t brightness)
{
    if (brightness<10 || brightness>100) return ESP_ERR_INVALID_ARG;
    return request_settings(brightness,-1,-1,-1);
}
esp_err_t ouse_system_request_theme(uint8_t theme)
{
    if (theme>1) return ESP_ERR_INVALID_ARG;
    return request_settings(-1,theme,-1,-1);
}
esp_err_t ouse_system_request_settings(uint8_t brightness, uint8_t theme)
{
    if (brightness<10 || brightness>100 || theme>1) return ESP_ERR_INVALID_ARG;
    return request_settings(brightness,theme,-1,-1);
}

esp_err_t ouse_system_request_ui(uint8_t locale,uint8_t font_percent)
{
    if (locale>1 || font_percent!=100) return ESP_ERR_INVALID_ARG;
    return request_settings(-1,-1,locale,font_percent);
}

esp_err_t ouse_system_get_settings_state(ouse_settings_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    if (!initialized) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&state_lock);
    *state = settings;
    portEXIT_CRITICAL(&state_lock);
    return ESP_OK;
}

esp_err_t ouse_system_get_info(ouse_system_info_t *info)
{
    if (!info) return ESP_ERR_INVALID_ARG;
    if (!initialized) return ESP_ERR_INVALID_STATE;
    *info = (ouse_system_info_t){
        .internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        .internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        .psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        .psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        .flash_bytes = flash_bytes, .cpu_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .reset_reason = esp_reset_reason(), .uptime_ms = (uint64_t)esp_timer_get_time() / 1000,
        .idf_version = esp_get_idf_version(),
    };
    portENTER_CRITICAL(&state_lock);info->internal_largest=memory.largest[0];info->psram_largest=memory.largest[1];
    info->internal_minimum=memory.minimum[0];info->psram_minimum=memory.minimum[1];info->memory_sample_ms=memory.sampled_ms;portEXIT_CRITICAL(&state_lock);
    return ESP_OK;
}
