// SPDX-License-Identifier: MIT
#pragma once
#include "ouse_runtime.h"
#define OUSE_BPK_MAX 524288
#define OUSE_BPK_MANIFEST_MAX 8192
#define OUSE_BPK_ICON_SIDE_MAX 96
#define OUSE_BPK_ICON_PIXELS_MAX 36864
#define OUSE_BPK_ASSET_PATH_MAX 95
typedef struct {
    char id[32], path[OUSE_BPK_ASSET_PATH_MAX+1];
    unsigned type;
    unsigned char *data;
    size_t size;
    lv_image_dsc_t image;
} ouse_bpk_asset_t;
typedef struct {
    ouse_script_package_t package;
    ouse_script_app_t state;
    ouse_app_definition_t definition;
    char id[48],name[48],name_zh[48],name_en[48];
    char *page,*script;
    lv_image_dsc_t icon_image;
    unsigned char *icon_pixels;
    ouse_bpk_asset_t asset_storage[OUSE_RUNTIME_ASSETS_MAX];
    ouse_script_image_t images[OUSE_RUNTIME_ASSETS_MAX];
    ouse_script_asset_t assets[OUSE_RUNTIME_ASSETS_MAX];
    unsigned asset_count;
} ouse_bpk_t;
esp_err_t ouse_bpk_parse(const unsigned char *bytes,size_t length,ouse_bpk_t **output);
esp_err_t ouse_bpk_catalog(const unsigned char *bytes,size_t length,ouse_bpk_t **output);
void ouse_bpk_free(ouse_bpk_t *package);
esp_err_t ouse_bpk_make_lazy(ouse_bpk_t *package,
    esp_err_t (*load)(const char *,uint32_t,const ouse_script_package_t **,void **),void (*release)(void *));
