#include "ouse_theme.h"
// SPDX-License-Identifier: MIT
#include "counter_app.h"
#include <string.h>

static struct {
    lv_obj_t *value, *elapsed;
    lv_timer_t *timer;
    uint32_t count, opened_at;
} counter;

static lv_obj_t *label(lv_obj_t *root, const char *text, int y, int width)
{
    lv_obj_t *object = lv_label_create(root);
    lv_label_set_text(object, text);
    lv_obj_set_pos(object, 20, y);
    lv_obj_set_width(object, width - 40);
    lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
    return object;
}

static void exit_counter(lv_event_t *event) { (void)event; (void)ouse_app_background(); }
static void update_value(void) { lv_label_set_text_fmt(counter.value, "%lu", (unsigned long)counter.count); }
static void increment(lv_event_t *event) { (void)event; if (counter.count < UINT32_MAX) ++counter.count; update_value(); }
static void reset(lv_event_t *event) { (void)event; counter.count = 0; update_value(); }
static void elapsed(lv_timer_t *timer)
{
    (void)timer;
    lv_label_set_text_fmt(counter.elapsed, "本次运行: %lu s", (unsigned long)(lv_tick_elaps(counter.opened_at) / 1000));
}
static void button(lv_obj_t *root, const char *text, int y, int width, lv_event_cb_t callback)
{
    lv_obj_t *object = lv_button_create(root);
    lv_obj_set_pos(object, 24, y);
    lv_obj_set_size(object, width - 48, 48);
    ouse_theme_apply(object, OUSE_STYLE_BUTTON, 0);
    ouse_theme_apply(object, OUSE_STYLE_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(object, 14, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t *caption = lv_label_create(object);
    lv_label_set_text(caption, text); lv_obj_center(caption);
}

static esp_err_t open_counter(lv_obj_t *root, void *context)
{
    (void)context;
    const int width = lv_obj_get_width(root), height = lv_obj_get_height(root);
    label(root, "计数器", 20, width);

    counter.value = label(root, "0", 96, width);
    lv_obj_set_style_text_font(counter.value, &lv_font_montserrat_24, 0);
    counter.elapsed = label(root, "本次运行: 0 s", 140, width);
    button(root, "+1", 186, width, increment);
    button(root, "清零", 246, width, reset);
    button(root, "回到桌面", height - 72, width, exit_counter);
    counter.opened_at = lv_tick_get();
    counter.timer = lv_timer_create(elapsed, 1000, NULL);
    return counter.timer ? ESP_OK : ESP_ERR_NO_MEM;
}

static void close_counter(void *context)
{
    (void)context;
    if (counter.timer) lv_timer_delete(counter.timer);
    memset(&counter, 0, sizeof(counter));
}

static void pause_counter(void *context) { (void)context; if (counter.timer) lv_timer_pause(counter.timer); }
static void resume_counter(void *context) { (void)context; if (counter.timer) lv_timer_resume(counter.timer); }

const ouse_app_definition_t ouse_counter_app = {
    .id = "ouse.counter", .name = "计数器", .icon = LV_SYMBOL_PLUS,
    .open = open_counter, .close = close_counter, .pause = pause_counter, .resume = resume_counter,
};
