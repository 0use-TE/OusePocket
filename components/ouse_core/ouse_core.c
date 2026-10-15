#include "ouse_theme.h"
// SPDX-License-Identifier: MIT
#include "ouse_core.h"
#include "ouse_apps_internal.h"
#include "ouse_shell.h"
#include <string.h>

#if LVGL_VERSION_MAJOR != 9 || LVGL_VERSION_MINOR != 5
#error "Ouse Core is validated against LVGL 9.5"
#endif
#if !LV_FONT_MONTSERRAT_24
#error "Enable the built-in Montserrat 24 font for the default desktop"
#endif

LV_FONT_DECLARE(ouse_font_ui_16)

typedef struct {
    bool started;
    ouse_core_port_t port;
    uint8_t brightness;
    lv_obj_t *screen;
    lv_obj_t *home;
    lv_obj_t *app_host;
    lv_obj_t *theme_button;
    lv_timer_t *status_timer;
    ouse_app_info_t icons[OUSE_APP_CAPACITY];
    lv_obj_t *settings;
    lv_obj_t *slider;
    lv_obj_t *brightness_text;
    lv_obj_t *message;
    lv_obj_t *info;
    lv_obj_t *info_text;
    unsigned pending_ui;
    uint32_t last_sample;
    bool save_request_failed, brightness_failed;
} core_state_t;

static core_state_t core;
extern void ouse_navigation_grid(int,int,unsigned,int *,int *,int *,int *);

static lv_obj_t *text(lv_obj_t *parent, const char *value, int x, int y, int width)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, value);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, width);
    lv_obj_set_pos(label, x, y);
    return label;
}

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}

static void show_home(lv_event_t *event)
{
    (void)event;
    (void)ouse_app_background();
}

static void app_changed(bool active)
{
    if (active) { lv_obj_add_flag(core.home, LV_OBJ_FLAG_HIDDEN); ouse_shell_foreground(); }
    else lv_obj_remove_flag(core.home, LV_OBJ_FLAG_HIDDEN);
}

static void launch_app(lv_event_t *event)
{
    const char *id = lv_event_get_user_data(event);
    esp_err_t error = ouse_app_open(id);
    if (error!=ESP_OK) LV_LOG_WARN("Application open failed");
}

static void show_settings(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(core.home, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(core.info, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(core.settings, LV_OBJ_FLAG_HIDDEN);
}

static void refresh_info(void)
{
    ouse_system_info_t info;
    if (!core.port.get_system_info || core.port.get_system_info(core.port.context, &info) != ESP_OK) {
        lv_label_set_text(core.info_text, "系统信息读取失败");
        return;
    }
    lv_label_set_text_fmt(core.info_text,
        "CPU: %lu MHz\n\nRAM: %lu / %lu KiB\n\nPSRAM: %lu / %lu KiB\n\nFlash: %lu MiB\n\n运行时间: %llu s\n\nESP-IDF: %s",
        (unsigned long)info.cpu_mhz,
        (unsigned long)(info.internal_free / 1024), (unsigned long)(info.internal_total / 1024),
        (unsigned long)(info.psram_free / 1024), (unsigned long)(info.psram_total / 1024),
        (unsigned long)(info.flash_bytes / (1024 * 1024)),
        (unsigned long long)(info.uptime_ms / 1000), info.idf_version ? info.idf_version : "?");
}

static void theme_label(void)
{
    if (core.theme_button) lv_label_set_text(lv_obj_get_child(core.theme_button,0),
        ouse_theme_get()==OUSE_THEME_DARK ? "外观：深色" : "外观：浅色");
}
esp_err_t ouse_core_set_theme(ouse_theme_t theme)
{
    if (theme!=OUSE_THEME_LIGHT && theme!=OUSE_THEME_DARK) return ESP_ERR_INVALID_ARG;
    if (!core.started) return ESP_ERR_INVALID_STATE;
    ouse_theme_set(theme); theme_label();
    esp_err_t error=core.port.save_theme ? core.port.save_theme(core.port.context,(uint8_t)theme,core.brightness) : ESP_OK;
    core.save_request_failed=error!=ESP_OK;
    return error;
}
static void toggle_theme(lv_event_t *event)
{
    (void)event;
    esp_err_t error=ouse_core_set_theme(ouse_theme_get()==OUSE_THEME_DARK ? OUSE_THEME_LIGHT : OUSE_THEME_DARK);
    if (error!=ESP_OK && core.message) lv_label_set_text(core.message,"主题保存失败，请重试");
}
static void show_info(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(core.settings, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(core.info, LV_OBJ_FLAG_HIDDEN);
    refresh_info();
    core.last_sample = lv_tick_get();
}

static void refresh_status(lv_timer_t *timer)
{
    (void)timer;
    if (!core.info || !core.settings || (!lv_obj_is_visible(core.settings) && !lv_obj_is_visible(core.info))) return;
    if (!lv_obj_has_flag(core.info, LV_OBJ_FLAG_HIDDEN) && lv_tick_elaps(core.last_sample) >= 1000) {
        refresh_info(); core.last_sample = lv_tick_get();
    }
    if (lv_obj_has_flag(core.settings, LV_OBJ_FLAG_HIDDEN) || !core.port.get_settings_state) return;
    if (core.save_request_failed || core.brightness_failed) return;
    ouse_settings_state_t state;
    if (core.port.get_settings_state(core.port.context, &state) != ESP_OK) {
        lv_label_set_text(core.message, "保存状态读取失败");
    } else if (state.pending) {
        lv_label_set_text(core.message, "正在保存亮度");
    } else if (state.last_error != ESP_OK) {
        lv_label_set_text(core.message, "保存失败，请点击保存重试");
    } else if (state.saved_brightness == core.brightness) {
        lv_label_set_text(core.message, "已保存，重启保留");
    } else {
        lv_label_set_text(core.message, "松手保存，或点击保存");
    }
}

static void save_brightness(lv_event_t *event)
{
    (void)event;
    if (!core.port.save_brightness || core.brightness_failed) return;
    esp_err_t error = core.port.save_brightness(core.port.context, core.brightness);
    core.save_request_failed = error != ESP_OK;
    lv_label_set_text(core.message, error == ESP_OK ? "正在保存亮度" : "保存失败，请点击保存重试");
}

static lv_obj_t *button(lv_obj_t *parent, const char *value, int x, int y,
                        int width, int height, lv_event_cb_t callback)
{
    lv_obj_t *object = lv_button_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_radius(object, 16, 0);
    ouse_theme_apply(object, OUSE_STYLE_BUTTON, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    ouse_theme_apply(object, OUSE_STYLE_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(object);
    lv_label_set_text(label, value);
    lv_obj_center(label);
    return object;
}

static esp_err_t apply_brightness(uint8_t requested)
{
    esp_err_t error=core.port.set_brightness(core.port.context,requested);
    if (error==ESP_OK) {
        core.brightness_failed=false; core.save_request_failed=false; core.brightness=requested;
        if (core.message) lv_label_set_text(core.message,core.port.save_brightness ? "松手保存，或点击保存" : "仅本次启动生效");
    } else {
        core.brightness_failed=true;
        if (core.message) lv_label_set_text(core.message,"亮度设置失败，请重试");
    }
    if (core.slider) lv_slider_set_value(core.slider,core.brightness,LV_ANIM_OFF);
    if (core.brightness_text) lv_label_set_text_fmt(core.brightness_text,"%u%%",(unsigned)core.brightness);
    return error;
}
static void brightness_changed(lv_event_t *event)
{
    (void)apply_brightness((uint8_t)lv_slider_get_value(lv_event_get_target_obj(event)));
}
static uint8_t shell_brightness(void) { return core.brightness; }
static esp_err_t shell_save(void)
{
    return core.port.save_brightness ? core.port.save_brightness(core.port.context,core.brightness) : ESP_ERR_NOT_SUPPORTED;
}
static esp_err_t shell_settings_state(ouse_settings_state_t *state)
{
    return core.port.get_settings_state ? core.port.get_settings_state(core.port.context,state) : ESP_ERR_NOT_SUPPORTED;
}
static void shell_back(void)
{
    ouse_app_stats_t state;
    if (ouse_app_get_stats(&state)!=ESP_OK) return;
    if (!strcmp(state.active_id,"ouse.settings") && core.info && !lv_obj_has_flag(core.info,LV_OBJ_FLAG_HIDDEN)) show_settings(NULL);
    else (void)ouse_app_background();
}

static esp_err_t settings_open(lv_obj_t *root, void *context)
{
    (void)context;
    const int width = lv_obj_get_width(root);
    const int height = lv_obj_get_height(root) + 48;
    core.settings = panel(root, 0, 0, width, height - 48);
    button(core.settings, "返回", 18, 6, 74, 40, show_home);
    text(core.settings, "设置", 114, 16, width - 140);
    text(core.settings, "屏幕亮度", 22, 80, 160);
    core.brightness_text = text(core.settings, "", width - 82, 80, 62);
    lv_label_set_text_fmt(core.brightness_text, "%u%%", (unsigned)core.brightness);
    core.slider = lv_slider_create(core.settings);
    lv_obj_set_pos(core.slider, 32, 132);
    lv_obj_set_size(core.slider, width - 64, 12);
    lv_slider_set_range(core.slider, 10, 100);
    lv_slider_set_value(core.slider, core.brightness, LV_ANIM_OFF);
    lv_obj_set_ext_click_area(core.slider, 20);
    ouse_theme_apply(core.slider, OUSE_STYLE_BUTTON, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(core.slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(core.slider, 6, LV_PART_MAIN);
    ouse_theme_apply(core.slider, OUSE_STYLE_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(core.slider, LV_OPA_COVER, LV_PART_INDICATOR);
    ouse_theme_apply(core.slider, OUSE_STYLE_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(core.slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(core.slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(core.slider, 8, LV_PART_KNOB);
    lv_obj_add_event_cb(core.slider, brightness_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(core.slider, save_brightness, LV_EVENT_RELEASED, NULL);
    core.message = text(core.settings, core.port.save_brightness ? "" : "仅本次启动生效", 22, 166, width - 44);
    lv_obj_set_width(core.message, width - 124);
    ouse_theme_apply(core.message, OUSE_STYLE_MUTED, 0);
    if (core.port.save_brightness) button(core.settings, "保存", width - 88, 168, 66, 34, save_brightness);
    button(core.settings, "系统信息", 22, 218, width - 44, 34, show_info);
    core.theme_button=button(core.settings,"",22,262,width-44,34,toggle_theme);
    theme_label();
    text(core.settings, "Ouse Core " OUSE_CORE_VERSION, 22, 310, width - 44);
    button(core.settings, "回到桌面", 18, height - 120, width - 36, 48, show_home);
    core.info = panel(root, 0, 0, width, height - 48);
    button(core.info, "返回设置", 18, 6, 106, 40, show_settings);
    text(core.info, "系统信息", 144, 16, width - 158);
    core.info_text = text(core.info, "", 22, 72, width - 44);
    text(core.info, "内存：空闲 / 可分配总量", 22, height - 112, width - 44);
    text(core.info, "CPU 显示频率，非占用率", 22, height - 86, width - 44);
    lv_obj_add_flag(core.info, LV_OBJ_FLAG_HIDDEN);
    core.status_timer = lv_timer_create(refresh_status, 300, NULL);
    refresh_status(NULL);
    return ESP_OK;
}

static void settings_close(void *context)
{
    (void)context;
    if (core.status_timer) lv_timer_delete(core.status_timer);
    core.status_timer = NULL; core.theme_button = NULL;
    core.settings = NULL; core.info = NULL; core.slider = NULL;
    core.brightness_text = NULL; core.message = NULL; core.info_text = NULL;
    core.brightness_failed = false; core.save_request_failed = false;
}

static void settings_pause(void *context) { (void)context; if (core.status_timer) lv_timer_pause(core.status_timer); }
static void settings_resume(void *context) { (void)context; if (core.status_timer) lv_timer_resume(core.status_timer); refresh_status(NULL); }

esp_err_t ouse_core_start(const ouse_core_config_t *config)
{
    if (core.started) return ESP_ERR_INVALID_STATE;
    if (!config || !config->port.display || !config->port.touch ||
        !config->port.lock || !config->port.unlock || !config->port.set_brightness ||
        !config->port.board_name || config->initial_brightness < 10 ||
        config->initial_brightness > 100) return ESP_ERR_INVALID_ARG;
    if (config->initial_theme!=OUSE_THEME_LIGHT && config->initial_theme!=OUSE_THEME_DARK) return ESP_ERR_INVALID_ARG;
    if (config->app_count >= OUSE_APP_CAPACITY || (config->app_count && !config->apps)) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < config->app_count; ++i) {
        esp_err_t error = ouse_apps_validate(&config->apps[i]);
        if (error != ESP_OK) return error;
        if (!strcmp(config->apps[i].id, "ouse.settings")) return ESP_ERR_INVALID_ARG;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(config->apps[i].id, config->apps[j].id)) return ESP_ERR_INVALID_ARG;
    }
    const ouse_core_port_t *port = &config->port;
    if (!port->lock(port->context, 2000)) return ESP_ERR_TIMEOUT;
    const int width = lv_display_get_horizontal_resolution(port->display);
    const int height = lv_display_get_vertical_resolution(port->display);
    if (width < 320 || height < 480) {
        port->unlock(port->context);
        return ESP_ERR_NOT_SUPPORTED;
    }
    const esp_err_t brightness_error = port->set_brightness(port->context, config->initial_brightness);
    if (brightness_error != ESP_OK) {
        port->unlock(port->context);
        return brightness_error;
    }
    core.port = *port;
    ouse_theme_set(config->initial_theme);
    core.brightness = config->initial_brightness;
    lv_display_set_default(port->display);
    core.screen = lv_obj_create(NULL);
    lv_obj_remove_flag(core.screen, LV_OBJ_FLAG_SCROLLABLE);
    ouse_theme_apply(core.screen, OUSE_STYLE_DESKTOP, 0);
    lv_obj_set_style_bg_opa(core.screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(core.screen, 0, 0);
    lv_obj_set_style_border_width(core.screen, 0, 0);
    lv_obj_set_style_text_font(core.screen, &ouse_font_ui_16, 0);
    core.home = panel(core.screen, 0, 40, width, height - 64);
    int gx,gy,gpage,page_width;
    ouse_navigation_grid(width-16,height-82,0,&gx,&gy,&gpage,&page_width);
    lv_obj_t *grid = panel(core.home, (width-page_width)/2, 18, page_width, height-82);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(grid, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(grid,LV_SCROLL_SNAP_START);
    lv_obj_set_scrollbar_mode(grid,LV_SCROLLBAR_MODE_OFF);
    lv_obj_t *page=NULL; int last_page=-1;
    core.app_host = panel(core.screen, 0, 40, width, height - 64);
    ouse_app_definition_t settings = {.id = "ouse.settings", .name = "设置", .icon = LV_SYMBOL_SETTINGS,
        .open = settings_open, .close = settings_close, .pause = settings_pause, .resume = settings_resume};
    // All definitions and duplicate ids were checked before acquiring the UI lock.
    (void)ouse_apps_init(core.app_host, app_changed);
    (void)ouse_apps_register(&settings);
    for (size_t i = 0; i < config->app_count; ++i) (void)ouse_apps_register(&config->apps[i]);
    ouse_apps_seal();
    for (unsigned i = 0; i <= config->app_count; ++i) {
        (void)ouse_app_get_info(i, &core.icons[i]);
        int x,y,page_index,grid_width;
        ouse_navigation_grid(width-16,height-82,i,&x,&y,&page_index,&grid_width);
        if (page_index!=last_page) {
            page=panel(grid,page_index*grid_width,0,grid_width,height-82);
            lv_obj_add_flag(page,LV_OBJ_FLAG_SNAPPABLE); last_page=page_index;
        }
        // Native equivalent of archived launcher_app_button: the whole tile,
        // including its name, is clickable; icon and caption are passive children.
        lv_obj_t *tile=lv_button_create(page);
        lv_obj_remove_style_all(tile);
        lv_obj_set_pos(tile,x,y); lv_obj_set_size(tile,112,112);
        lv_obj_set_style_opa(tile,LV_OPA_70,LV_STATE_PRESSED);
        lv_obj_add_event_cb(tile,launch_app,LV_EVENT_CLICKED,core.icons[i].id);
        lv_obj_t *icon=panel(tile,10,0,92,92);
        lv_obj_remove_flag(icon,LV_OBJ_FLAG_CLICKABLE);
        ouse_theme_apply(icon,OUSE_STYLE_ICON,0);
        lv_obj_t *glyph=lv_label_create(icon);
        lv_label_set_text(glyph,core.icons[i].icon);
        lv_obj_set_style_text_font(glyph,&lv_font_montserrat_24,0);
        lv_obj_center(glyph);
        lv_obj_t *name=text(tile,core.icons[i].name,0,96,112);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    }
    ouse_shell_config_t shell = {.screen=core.screen,.touch=core.port.touch,.brightness=shell_brightness,
        .set_brightness=apply_brightness,.save_brightness=shell_save,.settings_state=shell_settings_state,.back=shell_back};
    ouse_shell_init(&shell);
    lv_screen_load(core.screen);
    core.started = true;
    port->unlock(port->context);
    return ESP_OK;
}

bool ouse_core_is_started(void) { return core.started; }
const char *ouse_core_version(void) { return OUSE_CORE_VERSION; }

typedef struct { ouse_ui_callback_t callback; void *context; } ui_call_t;
static void dispatch_ui(void *argument)
{
    ui_call_t call = *(ui_call_t *)argument;
    lv_free(argument);
    --core.pending_ui; // adapter's LVGL task already holds the UI lock
    call.callback(call.context);
}

esp_err_t ouse_core_post_ui(ouse_ui_callback_t callback, void *context)
{
    if (!callback) return ESP_ERR_INVALID_ARG;
    if (!core.started) return ESP_ERR_INVALID_STATE;
    if (!core.port.lock(core.port.context, 2000)) return ESP_ERR_TIMEOUT;
    esp_err_t error = ESP_ERR_NO_MEM;
    if (core.pending_ui < 8) {
        ui_call_t *call = lv_malloc(sizeof(*call));
        if (call) {
            *call = (ui_call_t){.callback = callback, .context = context};
            if (lv_async_call(dispatch_ui, call) == LV_RESULT_OK) {
                ++core.pending_ui; error = ESP_OK;
            } else lv_free(call);
        }
    }
    core.port.unlock(core.port.context);
    return error;
}
