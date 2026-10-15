#include "ouse_theme.h"
// SPDX-License-Identifier: MIT
#include "ouse_apps_internal.h"
#include <string.h>
#include "esp_log.h"

typedef struct {
    ouse_app_info_t info;
    esp_err_t (*open)(lv_obj_t *, void *);
    void (*close)(void *);
    void (*pause)(void *), (*resume)(void *);
    lv_obj_t *root;
    lv_draw_buf_t *preview;
    lv_image_dsc_t image;
    ouse_theme_t preview_theme;
    uint64_t touched;
    void *context;
} entry_t;
static struct {
    lv_obj_t *host;
    uint64_t serial;
    void (*changed)(bool);
    entry_t entries[OUSE_APP_CAPACITY];
    unsigned count;
    int active;
    bool sealed, transitioning;
    uint32_t opened, closed, failed;
} apps;

esp_err_t ouse_apps_validate(const ouse_app_definition_t *app)
{
    if (!app || !app->id || !app->name || !app->icon || !app->open || !app->close ||
        !*app->id || strlen(app->id) >= 48 || !*app->name || strlen(app->name) >= 48 ||
        !*app->icon || strlen(app->icon) >= 16) return ESP_ERR_INVALID_ARG;
    if ((app->pause == NULL) != (app->resume == NULL)) return ESP_ERR_INVALID_ARG;
    for (const char *p = app->id; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' || *p == '-'))
            return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t ouse_apps_init(lv_obj_t *host, void (*changed)(bool))
{
    if (!host || !changed) return ESP_ERR_INVALID_ARG;
    if (apps.host) return ESP_ERR_INVALID_STATE;
    apps.host = host; apps.changed = changed; apps.active = -1;
    lv_obj_add_flag(host, LV_OBJ_FLAG_HIDDEN);
    return ESP_OK;
}

esp_err_t ouse_apps_register(const ouse_app_definition_t *app)
{
    esp_err_t error = ouse_apps_validate(app);
    if (error != ESP_OK) return error;
    if (!apps.host || apps.sealed) return ESP_ERR_INVALID_STATE;
    for (unsigned i = 0; i < apps.count; ++i)
        if (!strcmp(apps.entries[i].info.id, app->id)) return ESP_ERR_INVALID_STATE;
    if (apps.count == OUSE_APP_CAPACITY) return ESP_ERR_NO_MEM;
    entry_t *entry = &apps.entries[apps.count++];
    strcpy(entry->info.id, app->id); strcpy(entry->info.name, app->name); strcpy(entry->info.icon, app->icon);
    entry->open = app->open; entry->close = app->close; entry->context = app->context;
    entry->pause = app->pause; entry->resume = app->resume;
    ESP_LOGI("OuseApps", "Registered: %s", entry->info.id);
    return ESP_OK;
}

void ouse_apps_seal(void) { apps.sealed = true; }

static int find(const char *id)
{
    if (!id) return -1;
    for (unsigned i=0; i<apps.count; ++i) if (!strcmp(apps.entries[i].info.id,id)) return (int)i;
    return -1;
}
static void free_preview(entry_t *entry)
{
    if (entry->preview) lv_draw_buf_destroy(entry->preview);
    entry->preview=NULL; memset(&entry->image,0,sizeof(entry->image));
}
static void capture_preview(entry_t *entry)
{
    bool host_hidden=lv_obj_has_flag(apps.host,LV_OBJ_FLAG_HIDDEN);
    bool root_hidden=lv_obj_has_flag(entry->root,LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(apps.host,LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(entry->root,LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(entry->root);
    free_preview(entry);
    entry->preview=lv_snapshot_take(entry->root,LV_COLOR_FORMAT_RGB565);
    entry->preview_theme=ouse_theme_get();
    if (entry->preview) lv_draw_buf_to_image(entry->preview,&entry->image);
    if (root_hidden) lv_obj_add_flag(entry->root,LV_OBJ_FLAG_HIDDEN);
    if (host_hidden) lv_obj_add_flag(apps.host,LV_OBJ_FLAG_HIDDEN);
}
static void background_instance(void)
{
    if (apps.active<0) return;
    entry_t *entry=&apps.entries[apps.active];
    if (entry->pause) entry->pause(entry->context);
    capture_preview(entry);
    lv_obj_add_flag(entry->root,LV_OBJ_FLAG_HIDDEN);
    apps.active=-1;
    ESP_LOGI("OuseApps","Background: %s",entry->info.id);
}
static void close_instance(int index)
{
    entry_t *entry=&apps.entries[index];
    if (!entry->root) return;
    if (apps.active==index) apps.active=-1;
    entry->close(entry->context);
    lv_obj_delete(entry->root); entry->root=NULL;
    free_preview(entry);
    ++apps.closed;
    ESP_LOGI("OuseApps","Closed: %s",entry->info.id);
}

esp_err_t ouse_app_open(const char *id)
{
    if (!id) return ESP_ERR_INVALID_ARG;
    if (!apps.host || !apps.sealed || apps.transitioning) return ESP_ERR_INVALID_STATE;
    int index=find(id);
    if (index<0) return ESP_ERR_NOT_FOUND;
    if (apps.active==index) return ESP_OK;
    apps.transitioning=true;
    background_instance();
    entry_t *entry=&apps.entries[index];
    lv_obj_remove_flag(apps.host,LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(apps.host);
    esp_err_t error=ESP_OK;
    if (entry->root) {
        free_preview(entry);
        lv_obj_remove_flag(entry->root,LV_OBJ_FLAG_HIDDEN);
        if (entry->resume) entry->resume(entry->context);
        ESP_LOGI("OuseApps","Resumed: %s",entry->info.id);
    } else {
        entry->root=lv_obj_create(apps.host);
        lv_obj_remove_style_all(entry->root);
        lv_obj_set_pos(entry->root,0,0);
        lv_obj_set_size(entry->root,lv_obj_get_width(apps.host),lv_obj_get_height(apps.host));
        ouse_theme_apply(entry->root, OUSE_STYLE_APP, 0);
        lv_obj_set_style_bg_opa(entry->root,LV_OPA_COVER,0);
        lv_obj_remove_flag(entry->root,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_update_layout(entry->root);
        error=entry->open(entry->root,entry->context);
        if (error==ESP_OK) {
            ++apps.opened;
            ESP_LOGI("OuseApps","Opened: %s",entry->info.id);
        } else {
            entry->close(entry->context);
            lv_obj_delete(entry->root); entry->root=NULL;
            ++apps.failed;
            ESP_LOGE("OuseApps","Open failed: %s (%s)",entry->info.id,esp_err_to_name(error));
        }
    }
    if (error==ESP_OK) { apps.active=index; entry->touched=++apps.serial; }
    else lv_obj_add_flag(apps.host,LV_OBJ_FLAG_HIDDEN);
    apps.changed(error==ESP_OK);
    apps.transitioning=false;
    return error;
}

esp_err_t ouse_app_background(void)
{
    if (!apps.host || !apps.sealed || apps.transitioning) return ESP_ERR_INVALID_STATE;
    apps.transitioning=true;
    background_instance();
    lv_obj_add_flag(apps.host,LV_OBJ_FLAG_HIDDEN);
    apps.changed(false);
    apps.transitioning=false;
    return ESP_OK;
}

esp_err_t ouse_app_close_id(const char *id)
{
    if (!id) return ESP_ERR_INVALID_ARG;
    if (!apps.host || !apps.sealed || apps.transitioning) return ESP_ERR_INVALID_STATE;
    int index=find(id);
    if (index<0) return ESP_ERR_NOT_FOUND;
    bool foreground=apps.active==index;
    apps.transitioning=true;
    close_instance(index);
    if (foreground) { lv_obj_add_flag(apps.host,LV_OBJ_FLAG_HIDDEN); apps.changed(false); }
    apps.transitioning=false;
    return ESP_OK;
}

esp_err_t ouse_app_close(void)
{
    if (!apps.host || !apps.sealed || apps.transitioning) return ESP_ERR_INVALID_STATE;
    if (apps.active<0) return ESP_OK;
    return ouse_app_close_id(apps.entries[apps.active].info.id);
}

esp_err_t ouse_app_get_recent(unsigned index, ouse_app_info_t *info)
{
    if (!info) return ESP_ERR_INVALID_ARG;
    if (!apps.host) return ESP_ERR_INVALID_STATE;
    uint64_t ceiling=UINT64_MAX;
    for (unsigned rank=0; rank<=index; ++rank) {
        int selected=-1; uint64_t newest=0;
        for (unsigned i=0; i<apps.count; ++i) {
            entry_t *entry=&apps.entries[i];
            if (entry->root && entry->touched<ceiling && entry->touched>newest) { newest=entry->touched; selected=(int)i; }
        }
        if (selected<0) return ESP_ERR_NOT_FOUND;
        if (rank==index) { *info=apps.entries[selected].info; return ESP_OK; }
        ceiling=newest;
    }
    return ESP_ERR_NOT_FOUND;
}
const lv_image_dsc_t *ouse_app_get_preview(const char *id)
{
    int index=find(id);
    if (index>=0 && apps.entries[index].root && (!apps.entries[index].preview || apps.entries[index].preview_theme!=ouse_theme_get()))
        capture_preview(&apps.entries[index]);
    return index>=0 && apps.entries[index].preview ? &apps.entries[index].image : NULL;
}

esp_err_t ouse_app_get_info(unsigned index, ouse_app_info_t *info)
{
    if (!info) return ESP_ERR_INVALID_ARG;
    if (!apps.host) return ESP_ERR_INVALID_STATE;
    if (index >= apps.count) return ESP_ERR_NOT_FOUND;
    *info = apps.entries[index].info;
    return ESP_OK;
}

esp_err_t ouse_app_get_stats(ouse_app_stats_t *stats)
{
    if (!stats) return ESP_ERR_INVALID_ARG;
    if (!apps.host) return ESP_ERR_INVALID_STATE;
    *stats = (ouse_app_stats_t){.registered = apps.count, .live_instances = 0,
        .opened = apps.opened, .closed = apps.closed, .failed_opens = apps.failed};
    for (unsigned i=0; i<apps.count; ++i) {
        if (apps.entries[i].root) ++stats->live_instances;
        if (apps.entries[i].preview) stats->preview_bytes+=apps.entries[i].preview->data_size;
    }
    if (apps.active >= 0) strcpy(stats->active_id, apps.entries[apps.active].info.id);
    return ESP_OK;
}
