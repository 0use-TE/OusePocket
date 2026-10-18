// SPDX-License-Identifier: MIT
#include "ouse_ime.h"
#include "ouse_theme.h"
#include "ouse_ui.h"
#include <stdlib.h>
#include <string.h>

#define SCROLL_PATH_MAX 24
#define VIEW_PROPS 4

typedef struct input_binding {
    struct input_binding *next;
    lv_obj_t *textarea, *viewport, *reveal;
    ouse_ime_avoid_t mode;
    ouse_ime_type_t type;
    bool scroll_on_focus;
} input_binding_t;

typedef struct {
    lv_obj_t *object;
    int32_t x, y;
} scroll_snapshot_t;

static struct {
    input_binding_t *bindings, *active;
    lv_obj_t *keyboard;
    lv_timer_t *timer;
    scroll_snapshot_t scroll[SCROLL_PATH_MAX];
    unsigned scroll_count;
    lv_style_value_t original[VIEW_PROPS];
    bool local[VIEW_PROPS], changed[VIEW_PROPS], updating;
    int original_height, original_translate;
    uint32_t ui_generation;
    int bottom_inset;
} ime;

static const lv_style_prop_t viewport_props[VIEW_PROPS] = {
    LV_STYLE_HEIGHT, LV_STYLE_MIN_HEIGHT, LV_STYLE_MAX_HEIGHT, LV_STYLE_TRANSLATE_Y
};

static void input_event(lv_event_t *event);
static void viewport_deleted(lv_event_t *event);
static void scroll_deleted(lv_event_t *event);
static void fit_input(void);

static bool inside(lv_obj_t *object, lv_obj_t *root)
{
    for (; object; object=lv_obj_get_parent(object)) if (object==root) return true;
    return false;
}

static bool input_available(lv_obj_t *object)
{
    // A field outside the current scroll clip must still be focusable: bringing
    // that field into the visible viewport is the keyboard avoidance's job.
    if (lv_obj_has_state(object,LV_STATE_DISABLED)) return false;
    for (lv_obj_t *parent=object; parent; parent=lv_obj_get_parent(parent))
        if (lv_obj_has_flag(parent,LV_OBJ_FLAG_HIDDEN)) return false;
    return lv_obj_get_screen(object)==lv_screen_active();
}

static input_binding_t *find(lv_obj_t *textarea)
{
    for (input_binding_t *entry=ime.bindings; entry; entry=entry->next)
        if (entry->textarea==textarea) return entry;
    return NULL;
}
static void cursor_style(lv_obj_t *textarea)
{
    lv_obj_set_style_bg_opa(textarea,LV_OPA_TRANSP,LV_PART_CURSOR);
    lv_obj_set_style_border_width(textarea,0,LV_PART_CURSOR);
    lv_obj_set_style_border_side(textarea,LV_BORDER_SIDE_LEFT,LV_PART_CURSOR);
    lv_obj_set_style_pad_left(textarea,-ouse_ui_dp(2),LV_PART_CURSOR);
    lv_obj_set_style_border_opa(textarea,LV_OPA_COVER,LV_PART_CURSOR);
    lv_obj_set_style_border_color(textarea,lv_color_hex(ouse_theme_colors().accent),LV_PART_CURSOR);
    lv_obj_set_style_border_color(textarea,lv_color_hex(ouse_theme_colors().accent),LV_PART_CURSOR|LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(textarea,ouse_ui_dp(2),LV_PART_CURSOR|LV_STATE_FOCUSED);
    lv_obj_set_style_anim_duration(textarea,500,LV_PART_CURSOR);
}

static void hide_session(lv_obj_t *deleting_root)
{
    input_binding_t *entry=ime.active;
    if (!entry) return;
    // Clear ownership before LVGL changes focus/style; callbacks may re-enter.
    ime.active=NULL;
    lv_obj_remove_state(entry->textarea,LV_STATE_FOCUSED);
    if (entry->type==OUSE_IME_PASSWORD) {
        lv_textarea_set_password_mode(entry->textarea,true);
        if (entry->reveal) lv_label_set_text(lv_obj_get_child(entry->reveal,0),LV_SYMBOL_EYE_OPEN);
    }
    if (ime.timer) lv_timer_pause(ime.timer);
    if (ime.keyboard) {
        lv_keyboard_set_textarea(ime.keyboard,NULL);
        lv_obj_add_flag(ime.keyboard,LV_OBJ_FLAG_HIDDEN);
    }
    bool restore_view=!deleting_root || !inside(entry->viewport,deleting_root);
    if (restore_view) {
        for (unsigned i=0; i<VIEW_PROPS; ++i) if (ime.changed[i]) {
            if (ime.local[i]) lv_obj_set_local_style_prop(entry->viewport,viewport_props[i],ime.original[i],0);
            else lv_obj_remove_local_style_prop(entry->viewport,viewport_props[i],0);
        }
        lv_obj_update_layout(entry->viewport);
    }
    // Geometry is restored before scroll positions, including nested containers.
    for (unsigned i=0; i<ime.scroll_count; ++i) {
        scroll_snapshot_t *saved=&ime.scroll[i];
        if (!saved->object) continue;
        lv_obj_remove_event_cb_with_user_data(saved->object,scroll_deleted,saved);
        if (!deleting_root || !inside(saved->object,deleting_root))
            lv_obj_scroll_to(saved->object,saved->x,saved->y,LV_ANIM_OFF);
        saved->object=NULL;
    }
    ime.scroll_count=0;
    memset(ime.changed,0,sizeof(ime.changed));
}

void ouse_ime_hide(void) { hide_session(NULL); }
bool ouse_ime_active(void) { return ime.active!=NULL; }
bool ouse_ime_focused(const lv_obj_t *textarea) { return ime.active && ime.active->textarea==textarea; }
bool ouse_ime_blur(lv_obj_t *textarea)
{
    if (!ouse_ime_focused(textarea)) return false;
    ouse_ime_hide();
    return true;
}

void ouse_ime_hide_root(lv_obj_t *root)
{
    if (root && ime.active && inside(ime.active->textarea,root)) ouse_ime_hide();
}

static void unlink_binding(input_binding_t *entry, bool textarea_deleting, bool viewport_deleting)
{
    if (ime.active==entry)
        hide_session(viewport_deleting ? entry->viewport : textarea_deleting ? entry->textarea : NULL);
    input_binding_t **link=&ime.bindings;
    while (*link && *link!=entry) link=&(*link)->next;
    if (*link) *link=entry->next;
    if (!textarea_deleting) {
        if (entry->reveal) lv_obj_delete(entry->reveal);
        lv_obj_remove_event_cb_with_user_data(entry->textarea,input_event,entry);
        if (entry->scroll_on_focus) lv_obj_add_flag(entry->textarea,LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    }
    if (!viewport_deleting) lv_obj_remove_event_cb_with_user_data(entry->viewport,viewport_deleted,entry);
    free(entry);
}

void ouse_ime_detach(lv_obj_t *textarea)
{
    input_binding_t *entry=find(textarea);
    if (entry) unlink_binding(entry,false,false);
}

static void viewport_deleted(lv_event_t *event)
{
    input_binding_t *entry=lv_event_get_user_data(event);
    if (lv_event_get_target_obj(event)==entry->viewport) unlink_binding(entry,false,true);
}

static void scroll_deleted(lv_event_t *event)
{
    scroll_snapshot_t *saved=lv_event_get_user_data(event);
    if (lv_event_get_target_obj(event)!=saved->object) return;
    saved->object=NULL;
    if (ime.active) hide_session(lv_event_get_target_obj(event));
}

static void keyboard_event(lv_event_t *event)
{
    if (lv_event_get_code(event)==LV_EVENT_DELETE) {
        if (ime.active) hide_session(lv_event_get_target_obj(event));
        ime.keyboard=NULL;
    }
    // LVGL forwards READY/CANCEL exactly once to its bound textarea. Handle them
    // on that textarea instead of forwarding here: a submit handler is allowed
    // to focus another input immediately, without submitting that next input.
}

static void measure_keyboard(void)
{
    if (!ime.keyboard) return;
    lv_display_t *display=lv_obj_get_display(ime.keyboard);
    int height=lv_display_get_vertical_resolution(display);
    int keyboard_height=ouse_ui_dp(192), limit=height*45/100;
    if (keyboard_height>limit) keyboard_height=limit;
    lv_obj_set_size(ime.keyboard,LV_PCT(100),keyboard_height);
    lv_obj_align(ime.keyboard,LV_ALIGN_BOTTOM_MID,0,-ime.bottom_inset);
}

void ouse_ime_set_bottom_inset(int pixels)
{
    if (pixels<0 || ime.bottom_inset==pixels) return;
    ime.bottom_inset=pixels;
    measure_keyboard();
    if (ime.active) fit_input();
}

static void refresh(lv_timer_t *timer)
{
    (void)timer;
    if (!ime.active) return;
    if (!input_available(ime.active->textarea)) { ouse_ime_hide(); return; }
    if (ime.ui_generation!=ouse_ui_generation()) {
        ime.ui_generation=ouse_ui_generation();
        measure_keyboard();
        fit_input();
    }
    if(lv_color_to_u32(lv_obj_get_style_border_color(ime.active->textarea,LV_PART_CURSOR))!=lv_color_to_u32(lv_color_hex(ouse_theme_colors().accent)))
        cursor_style(ime.active->textarea);
}

static bool ensure_keyboard(lv_obj_t *screen)
{
    if (!ime.keyboard) {
        ime.keyboard=lv_keyboard_create(screen);
        if (!ime.keyboard) return false;
        lv_obj_remove_flag(ime.keyboard,LV_OBJ_FLAG_SCROLLABLE);
        lv_keyboard_set_popovers(ime.keyboard,false);
        ouse_ui_use_font(ime.keyboard,16);
        ouse_theme_apply(ime.keyboard,OUSE_STYLE_APP,0);
        ouse_theme_apply(ime.keyboard,OUSE_STYLE_PANEL,LV_PART_ITEMS);
        ouse_theme_apply(ime.keyboard,OUSE_STYLE_PRESSED,LV_PART_ITEMS|LV_STATE_CHECKED);
        ouse_theme_apply(ime.keyboard,OUSE_STYLE_PRESSED,LV_PART_ITEMS|LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(ime.keyboard,ouse_ui_dp(5),0);
        lv_obj_set_style_pad_row(ime.keyboard,ouse_ui_dp(5),0);
        lv_obj_set_style_pad_column(ime.keyboard,ouse_ui_dp(3),0);
        lv_obj_set_style_border_width(ime.keyboard,0,0);
        lv_obj_set_style_border_width(ime.keyboard,0,LV_PART_ITEMS);
        lv_obj_set_style_radius(ime.keyboard,ouse_ui_dp(6),LV_PART_ITEMS);
        lv_obj_set_style_shadow_width(ime.keyboard,0,LV_PART_ITEMS);
        lv_obj_add_event_cb(ime.keyboard,keyboard_event,LV_EVENT_ALL,NULL);
        lv_obj_add_flag(ime.keyboard,LV_OBJ_FLAG_HIDDEN);
    } else if (lv_obj_get_parent(ime.keyboard)!=screen) lv_obj_set_parent(ime.keyboard,screen);
    if (!ime.timer) {
        ime.timer=lv_timer_create(refresh,100,NULL);
        if (!ime.timer) return false;
        lv_timer_pause(ime.timer);
    }
    measure_keyboard();
    return true;
}

static void change_viewport(unsigned index, int32_t value)
{
    ime.changed[index]=true;
    lv_style_value_t style={.num=value};
    lv_obj_set_local_style_prop(ime.active->viewport,viewport_props[index],style,0);
}

static void fit_input(void)
{
    if (!ime.active || ime.updating) return;
    input_binding_t *entry=ime.active;
    if (entry->mode==OUSE_IME_OVERLAY) return;
    ime.updating=true;
    lv_obj_update_layout(ime.keyboard);
    lv_area_t keyboard, viewport, input;
    lv_obj_get_coords(ime.keyboard,&keyboard);
    lv_obj_get_coords(entry->viewport,&viewport);
    if (entry->mode==OUSE_IME_RESIZE) {
        int height=keyboard.y1-viewport.y1;
        if (height>ime.original_height) height=ime.original_height;
        if (height<ouse_ui_dp(44)) height=ouse_ui_dp(44);
        change_viewport(0,height);
        change_viewport(1,0);
        change_viewport(2,height);
        lv_obj_update_layout(entry->viewport);
        lv_obj_scroll_to_view_recursive(entry->textarea,LV_ANIM_OFF);
    } else {
        // Recalculate from the unshifted view for focus changes and metric updates.
        change_viewport(3,ime.original_translate);
        lv_obj_update_layout(entry->viewport);
        lv_obj_scroll_to_view_recursive(entry->textarea,LV_ANIM_OFF);
        lv_obj_get_coords(entry->textarea,&input);
        int overlap=input.y2+ouse_ui_dp(8)-keyboard.y1;
        if (overlap>0) change_viewport(3,ime.original_translate-overlap);
        lv_obj_update_layout(entry->viewport);
    }
    ime.updating=false;
}

void ouse_ime_focus(lv_obj_t *textarea)
{
    input_binding_t *entry=find(textarea);
    if (!entry || !input_available(textarea)) return;
    if (ime.active==entry) { lv_obj_add_state(textarea,LV_STATE_FOCUSED);fit_input(); return; }
    ouse_ime_hide();
    if (!ensure_keyboard(lv_obj_get_screen(textarea))) return;
    lv_obj_update_layout(entry->viewport);
    ime.original_height=lv_obj_get_height(entry->viewport);
    ime.original_translate=lv_obj_get_style_translate_y(entry->viewport,0);
    for (unsigned i=0; i<VIEW_PROPS; ++i) {
        ime.local[i]=lv_obj_get_local_style_prop(entry->viewport,viewport_props[i],&ime.original[i],0)==LV_STYLE_RES_FOUND;
        ime.changed[i]=false;
    }
    // Capture scroll positions before automatic focus avoidance changes them.
    for (lv_obj_t *parent=lv_obj_get_parent(textarea); parent && ime.scroll_count<SCROLL_PATH_MAX;
         parent=lv_obj_get_parent(parent)) {
        scroll_snapshot_t *saved=&ime.scroll[ime.scroll_count++];
        *saved=(scroll_snapshot_t){parent,lv_obj_get_scroll_x(parent),lv_obj_get_scroll_y(parent)};
        lv_obj_add_event_cb(parent,scroll_deleted,LV_EVENT_DELETE,saved);
    }
    ime.active=entry;
    cursor_style(textarea);
    lv_obj_add_state(textarea,LV_STATE_FOCUSED);
    ime.ui_generation=ouse_ui_generation();
    lv_keyboard_set_mode(ime.keyboard,entry->type==OUSE_IME_NUMBER ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(ime.keyboard,textarea);
    lv_obj_remove_flag(ime.keyboard,LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ime.keyboard);
    fit_input();
    lv_timer_resume(ime.timer);
}

static void input_event(lv_event_t *event)
{
    input_binding_t *entry=lv_event_get_user_data(event);
    if (lv_event_get_target_obj(event)!=entry->textarea) return;
    lv_event_code_t code=lv_event_get_code(event);
    if (code==LV_EVENT_DELETE) { unlink_binding(entry,true,false); return; }
    if (code==LV_EVENT_FOCUSED || code==LV_EVENT_DEFOCUSED) {
        lv_indev_t *indev=lv_event_get_indev(event);
        if (indev && lv_indev_get_type(indev)==LV_INDEV_TYPE_POINTER) {
            // Pointer focus changes happen on press, before LVGL knows whether
            // this contact will become a tap, scroll or system edge gesture.
            // Open on CLICKED instead; let Back consume the still-open IME on
            // gesture completion rather than navigating the app after an early
            // pointer defocus. Explicit/group focus remains immediate.
            if (code==LV_EVENT_DEFOCUSED && ime.active==entry)
                lv_obj_add_state(entry->textarea,LV_STATE_FOCUSED);
            return;
        }
    }
    if (code==LV_EVENT_FOCUSED || code==LV_EVENT_CLICKED) ouse_ime_focus(entry->textarea);
    else if (ime.active==entry) {
        if (code==LV_EVENT_DEFOCUSED || code==LV_EVENT_READY || code==LV_EVENT_CANCEL) ouse_ime_hide();
        else if (code==LV_EVENT_VALUE_CHANGED || code==LV_EVENT_SIZE_CHANGED) fit_input();
    }
}

static void reveal_password(lv_event_t *event)
{
    input_binding_t *entry=lv_event_get_user_data(event);
    ouse_ime_focus(entry->textarea);
    bool hidden=lv_textarea_get_password_mode(entry->textarea);
    lv_textarea_set_password_mode(entry->textarea,!hidden);
    lv_label_set_text(lv_obj_get_child(entry->reveal,0),hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

esp_err_t ouse_ime_attach(lv_obj_t *textarea, lv_obj_t *viewport,
                        ouse_ime_avoid_t mode, ouse_ime_type_t type)
{
    if (!textarea || !viewport || textarea==viewport || !lv_obj_check_type(textarea,&lv_textarea_class) ||
        !inside(textarea,viewport) || (unsigned)mode>OUSE_IME_OVERLAY ||
        (unsigned)type>OUSE_IME_PASSWORD) return ESP_ERR_INVALID_ARG;
    input_binding_t *entry=find(textarea);
    if (entry) {
        ouse_ime_detach(textarea);
    }
    entry=calloc(1,sizeof(*entry));
    if (!entry) return ESP_ERR_NO_MEM;
    *entry=(input_binding_t){.next=ime.bindings,.textarea=textarea,.viewport=viewport,.mode=mode,.type=type,
        .scroll_on_focus=lv_obj_has_flag(textarea,LV_OBJ_FLAG_SCROLL_ON_FOCUS)};
    ime.bindings=entry;
    // The system snapshots before doing its own avoidance. LVGL's earlier
    // class-level animated focus scroll would otherwise race that snapshot.
    lv_obj_remove_flag(textarea,LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    cursor_style(textarea);
    lv_textarea_set_password_mode(textarea,type==OUSE_IME_PASSWORD);
    if (type==OUSE_IME_PASSWORD) {
        entry->reveal=lv_button_create(textarea);
        lv_obj_remove_style_all(entry->reveal);
        lv_obj_set_size(entry->reveal,ouse_ui_dp(36),LV_PCT(100));
        lv_obj_add_flag(entry->reveal,LV_OBJ_FLAG_FLOATING|LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_remove_flag(entry->reveal,LV_OBJ_FLAG_CLICK_FOCUSABLE|LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(entry->reveal,LV_ALIGN_RIGHT_MID,0,0);
        lv_obj_set_style_pad_right(textarea,ouse_ui_dp(40),0);
        lv_obj_t *icon=lv_label_create(entry->reveal);
        lv_obj_set_style_text_font(icon,&lv_font_montserrat_16,0);
        lv_label_set_text(icon,LV_SYMBOL_EYE_OPEN);
        lv_obj_center(icon);
        lv_obj_add_event_cb(entry->reveal,reveal_password,LV_EVENT_CLICKED,entry);
    }
    lv_obj_add_event_cb(textarea,input_event,LV_EVENT_ALL,entry);
    lv_obj_add_event_cb(viewport,viewport_deleted,LV_EVENT_DELETE,entry);
    return ESP_OK;
}

void ouse_ime_get_state(ouse_ime_state_t *state)
{
    if (!state) return;
    memset(state,0,sizeof(*state));
    for (input_binding_t *entry=ime.bindings; entry; entry=entry->next) ++state->attachments;
    if (!ime.active) return;
    state->visible=true;
    state->mode=ime.active->mode;
    state->keyboard_type=ime.active->type;
    state->keyboard_height=lv_obj_get_height(ime.keyboard);
    state->viewport_height=lv_obj_get_height(ime.active->viewport);
    lv_obj_get_coords(ime.active->textarea,&state->input_bounds);
    lv_obj_get_coords(ime.active->viewport,&state->viewport_bounds);
    lv_obj_get_coords(ime.keyboard,&state->keyboard_bounds);
}
