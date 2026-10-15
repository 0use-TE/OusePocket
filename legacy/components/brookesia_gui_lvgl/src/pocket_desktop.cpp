// Screenshot lifecycle and card navigation follow the bundled Waveshare
// Brookesia Phone example (Apache-2.0). App pages remain alive while paused.
#include "private/pocket_desktop.hpp"
#include "brookesia/lib_utils/function_guard.hpp"

namespace esp_brookesia::gui::lvgl {
namespace {
struct Lock { Lock() { lock_thread(); } ~Lock() { unlock_thread(); } };
bool valid(lv_obj_t *object) { return object && lv_obj_is_valid(object); }
lv_obj_t *box(lv_obj_t *parent, int w, int h) {
    auto *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_size(object, w, h);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}
void transparent(lv_obj_t *object) { lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0); }
}

DesktopImpl::~DesktopImpl() {
    hide();
    if (valid(hint_)) lv_obj_delete(hint_);
    if (valid(left_edge_)) lv_obj_delete(left_edge_);
    if (valid(right_edge_)) lv_obj_delete(right_edge_);
    clear();
}

std::expected<void, std::string> DesktopImpl::capture(uint32_t id,
    const std::vector<std::string> &paths, bool dark)
{
#if LV_USE_SNAPSHOT
    auto *display = backend_.default_display();
    if (!display) return std::unexpected("Snapshot display unavailable");
    auto *screen = lv_display_get_screen_active(display);
    struct Saved { lv_obj_t *object; lv_obj_t *parent; int index; int x; int y; int raw_x; int raw_y; int layer; };
    std::vector<Saved> saved;
    // Paths can occur in more than one app. Only the visible, mounted roots
    // belonging to the foreground presentation may be captured.
    for (const auto &[handle, record] : backend_.records) {
        if (!record.is_top_level_screen || !valid(record.object) || record.hidden ||
            lv_obj_has_flag(record.object, LV_OBJ_FLAG_HIDDEN) || !backend_.mounted_targets.contains(handle)) continue;
        if (std::find(paths.begin(), paths.end(), backend_.build_absolute_path(record)) == paths.end()) continue;
        lv_area_t area {};
        lv_obj_get_coords(record.object, &area);
        saved.push_back({record.object, lv_obj_get_parent(record.object), lv_obj_get_index(record.object),
            area.x1, area.y1, lv_obj_get_style_x(record.object, LV_PART_MAIN), lv_obj_get_style_y(record.object, LV_PART_MAIN),
            lv_obj_get_parent(record.object) == lv_display_get_layer_top(display) ? 1 : 0});
    }
    if (saved.empty()) return std::unexpected("No visible app screen to snapshot");
    auto *surface = box(screen, lv_display_get_horizontal_resolution(display), lv_display_get_vertical_resolution(display));
    lv_obj_set_style_bg_color(surface, lv_color_hex(dark ? 0x1a1a1a : 0xf6f7f9), 0);
    lv_obj_set_style_bg_opa(surface, LV_OPA_COVER, 0);
    // Settings has a content screen and a separate header on the Top layer.
    // Compose both into one isolated surface, excluding Shell/keyboard/dialog.
    std::sort(saved.begin(), saved.end(), [](const Saved &a, const Saved &b) {
        return a.layer != b.layer ? a.layer < b.layer : a.index < b.index;
    });
    for (const auto &entry : saved) {
        lv_obj_set_parent(entry.object, surface);
        lv_obj_set_pos(entry.object, entry.x, entry.y);
    }
    lib_utils::FunctionGuard restore([&] {
        for (const auto &entry : saved) {
            lv_obj_set_parent(entry.object, entry.parent);
            lv_obj_set_pos(entry.object, entry.raw_x, entry.raw_y);
        }
        std::sort(saved.begin(), saved.end(), [](const Saved &a, const Saved &b) { return a.index < b.index; });
        for (const auto &entry : saved) lv_obj_move_to_index(entry.object, entry.index);
        lv_obj_delete(surface);
    });
    lv_obj_update_layout(surface);
    auto image = snapshots_.contains(id) ? snapshots_.at(id) : std::make_shared<Snapshot>();
    const auto width = lv_obj_get_width(surface), height = lv_obj_get_height(surface);
    if (image->buffer && (image->buffer->header.w != static_cast<uint32_t>(width) ||
        image->buffer->header.h != static_cast<uint32_t>(height))) {
        lv_draw_buf_destroy(image->buffer); image->buffer = nullptr;
    }
    if (!image->buffer) image->buffer = lv_snapshot_create_draw_buf(surface, LV_COLOR_FORMAT_RGB565);
    if (!image->buffer || lv_snapshot_take_to_draw_buf(surface, LV_COLOR_FORMAT_RGB565, image->buffer) != LV_RESULT_OK) {
        return std::unexpected("App screenshot allocation/render failed");
    }
    snapshots_[id] = std::move(image);
    return {};
#else
    (void)id; (void)paths; (void)dark;
    return std::unexpected("LV_USE_SNAPSHOT is disabled");
#endif
}

void DesktopImpl::release(uint32_t id) { snapshots_.erase(id); }
void DesktopImpl::clear() { snapshots_.clear(); }
void DesktopImpl::hide() {
    // A vertically dragged card temporarily lives directly under the host.
    for (const auto &card : cards_) {
        if (card->release_timer) lv_timer_delete(card->release_timer);
        if (valid(card->drag) && lv_obj_get_parent(card->drag) != card->slot) lv_obj_delete(card->drag);
    }
    if (valid(body_)) lv_obj_delete(body_);
    body_ = nullptr; table_ = nullptr; cards_.clear();
    ignore_release_ = false;
}

void DesktopImpl::emit(std::string_view operation, uint32_t id) {
    if (!backend_.event_sink) return;
    // Runtime forwards this to Shell's app-input worker. Never call app stop or
    // resume synchronously inside an LVGL event callback while holding its lock.
    backend_.event_sink(BackendEvent{.handle = event_handle_, .type = EventType::Clicked,
        .action = "ouse.recents.native", .payload = {{"operation", operation}, {"app_id", id}, {"generation", generation_}}});
}
void DesktopImpl::focus(size_t index, bool animate) {
    if (cards_.empty()) return;
    focus_ = std::min(index, cards_.size() - 1);
    // Like Phone, temporarily allow programmatic scrolling while keeping
    // pointer scrolling disabled so vertical card dismissal stays independent.
    lv_obj_add_flag(table_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_scroll_to_view(cards_[focus_]->slot, animate ? LV_ANIM_ON : LV_ANIM_OFF);
    lv_obj_remove_flag(table_, LV_OBJ_FLAG_SCROLLABLE);
}

std::expected<void, std::string> DesktopImpl::show(const std::vector<RecentTask> &apps, uint32_t selected,
    bool dark, bool chinese, uint64_t generation, bool ignore_release)
{
    hide(); hide_back_hint();
    auto *root = backend_.find_record_by_absolute_path("/overlay/recents");
    auto *heading = backend_.find_record_by_absolute_path("/overlay/recents/title");
    if (!root || !heading || !valid(root->object)) return std::unexpected("Recent task host unavailable");
    event_handle_ = root->handle;
    generation_ = generation;
    ignore_release_ = ignore_release;
    if (ignore_release) {
        // Showing a new widget under the finger can synthesize another Pressed
        // while the opening swipe is still held. Consume that entire gesture.
        for (auto *input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input))
            if (lv_indev_get_type(input) == LV_INDEV_TYPE_POINTER && lv_indev_get_state(input) == LV_INDEV_STATE_PRESSED)
                lv_indev_wait_release(input);
    }
    auto *display = lv_obj_get_display(root->object);
    const int width = lv_display_get_horizontal_resolution(display), height = lv_display_get_vertical_resolution(display);
    const int card_w = width * 180 / 320, preview_h = height * 270 / 480, title_h = height * 32 / 480;
    threshold_ = std::max(24, height * 30 / 480); step_ = std::max(5, height * 5 / 480);
    const auto foreground = lv_color_hex(dark ? 0xffffff : 0x20242c);
    const auto background = lv_color_hex(dark ? 0x1a1a1a : 0xf6f7f9);
    lv_obj_set_style_bg_color(root->object, background, 0);
    lv_obj_set_style_bg_opa(root->object, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(heading->object, foreground, 0);
    body_ = box(root->object, width, height - 40);
    // The GUI document's host uses flex, but this native surface has fixed
    // screen coordinates. Do not let the host rearrange it as a flex item.
    lv_obj_add_flag(body_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(body_, 0, 40);
    lv_obj_set_style_text_font(body_, lv_obj_get_style_text_font(heading->object, LV_PART_MAIN), 0);
    lv_obj_set_style_text_color(body_, foreground, 0);
    transparent(body_);
    table_ = box(body_, width, height - 115);
    transparent(table_);
    lv_obj_set_flex_flow(table_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(table_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(table_, width * 40 / 320, 0);
    lv_obj_set_style_pad_left(table_, (width - card_w) / 2, 0);
    lv_obj_set_style_pad_right(table_, (width - card_w) / 2, 0);
    lv_obj_set_scrollbar_mode(table_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_x(table_, LV_SCROLL_SNAP_CENTER);
    lv_obj_remove_flag(table_, LV_OBJ_FLAG_SCROLLABLE); // Phone uses manual gesture navigation.
    for (const auto &app : apps) {
        auto card = std::make_unique<Card>(); card->desktop = this; card->id = app.app_id;
        card->slot = box(table_, card_w, preview_h + title_h);
        transparent(card->slot); lv_obj_add_flag(card->slot, LV_OBJ_FLAG_SNAPPABLE);
        card->drag = box(card->slot, card_w, preview_h + title_h); transparent(card->drag);
        // Keep explicit x/y when promoted to the document's flex-column host.
        // Otherwise flex puts the preview below body_, outside the viewport.
        lv_obj_add_flag(card->drag, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_add_flag(card->drag, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(card->drag, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(card->drag, card_event, LV_EVENT_ALL, card.get());
        auto *title = box(card->drag, card_w, title_h); transparent(title);
        lv_obj_remove_flag(title, LV_OBJ_FLAG_CLICKABLE);
        auto *name = lv_label_create(title); lv_label_set_text(name, app.name.c_str());
        lv_obj_set_width(name, card_w - 30); lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 30, 0); lv_obj_remove_flag(name, LV_OBJ_FLAG_CLICKABLE);
        if (!app.icon_path.empty()) {
            auto loaded = load_image_source(app.icon_path);
            if (loaded) {
                card->icon = *loaded;
                auto *icon = lv_image_create(title); lv_image_set_src(icon, &card->icon->descriptor);
                const int size = std::max(18, title_h - 8);
                const auto source_size = std::max(card->icon->descriptor.header.w, card->icon->descriptor.header.h);
                if (source_size) lv_image_set_scale(icon, size * 256 / source_size);
                lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0); lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
            }
        }
        auto *preview = box(card->drag, card_w, preview_h);
        lv_obj_set_pos(preview, 0, title_h);
        lv_obj_set_style_bg_color(preview, lv_color_hex(dark ? 0x30343a : 0xffffff), 0);
        lv_obj_set_style_bg_opa(preview, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(preview, 20, 0); lv_obj_set_style_clip_corner(preview, true, 0);
        lv_obj_remove_flag(preview, LV_OBJ_FLAG_CLICKABLE);
        auto snapshot = snapshots_.find(app.app_id);
        if (snapshot != snapshots_.end() && snapshot->second->buffer) {
            card->snapshot = snapshot->second;
            auto *image = lv_image_create(preview); lv_image_set_src(image, card->snapshot->buffer);
            lv_image_set_scale(image, std::min(card_w * 256 / width, preview_h * 256 / height));
            lv_obj_center(image); lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
        } else {
            auto *label = lv_label_create(preview);
            lv_label_set_text(label, chinese ? "预览不可用" : "Preview unavailable"); lv_obj_center(label);
            lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
        }
        if (app.app_id == selected) focus_ = cards_.size();
        cards_.push_back(std::move(card));
    }
    if (cards_.empty()) {
        auto *label = lv_label_create(table_); lv_label_set_text(label, chinese ? "暂无最近任务" : "No recent apps");
        lv_obj_set_width(label, width); lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_left(table_, 0, 0); lv_obj_set_style_pad_right(table_, 0, 0);
    }
    auto *close_all = lv_button_create(body_);
    lv_obj_set_size(close_all, width * 180 / 320, 42); lv_obj_align(close_all, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_set_style_bg_color(close_all, background, 0); lv_obj_set_style_bg_opa(close_all, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(close_all, 1, 0); lv_obj_set_style_radius(close_all, 24, 0);
    lv_obj_set_style_border_color(close_all, lv_color_hex(dark ? 0x74777d : 0xaeb4bd), 0);
    lv_obj_set_style_shadow_width(close_all, 0, 0); lv_obj_set_style_text_color(close_all, foreground, 0);
    auto *label = lv_label_create(close_all); lv_label_set_text(label, chinese ? "全部关闭" : "Close all"); lv_obj_center(label);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(close_all, button_event, LV_EVENT_ALL, this);
    lv_obj_update_layout(body_); focus(focus_, false);
    return {};
}

void DesktopImpl::button_event(lv_event_t *event) {
    auto *desktop = static_cast<DesktopImpl *>(lv_event_get_user_data(event));
    if (!desktop) return;
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) desktop->ignore_release_ = false;
    else if (lv_event_get_code(event) == LV_EVENT_CLICKED && !desktop->ignore_release_) desktop->emit("close_all");
}
void DesktopImpl::card_event(lv_event_t *event) {
    auto *card = static_cast<Card *>(lv_event_get_user_data(event));
    if (!card) return;
    auto &desktop = *card->desktop;
    const auto code = lv_event_get_code(event);
    auto *input = lv_indev_active(); if (!input) return;
    lv_point_t point {}; lv_indev_get_point(input, &point);
    if (code == LV_EVENT_PRESSED) {
        if (card->closing) return;
        if (card->release_timer) {
            // A brief empty touch sample is not the end of the finger's drag.
            // Preserve its original anchor when contact returns before confirmation.
            lv_timer_delete(card->release_timer); card->release_timer = nullptr;
            return;
        }
        card->input = input;
        desktop.ignore_release_ = false; desktop.press_ = point; desktop.axis_ = recent::Axis::None;
    } else if (code == LV_EVENT_PRESSING && !desktop.ignore_release_ && !card->closing) {
        const int dx = point.x - desktop.press_.x, dy = point.y - desktop.press_.y;
        if (desktop.axis_ == recent::Axis::None) desktop.axis_ = recent::choose_axis(dx, dy, desktop.step_);
        if (desktop.axis_ == recent::Axis::Vertical) {
            // Dismissal distance is a release decision, not a movement limit.
            // Draw directly under the full-screen host to escape slot/table
            // clipping, retaining the horizontal slot for the other cards.
            if (lv_obj_get_parent(card->drag) == card->slot) {
                auto *host = lv_obj_get_parent(desktop.body_);
                lv_area_t origin {}, host_area {};
                lv_obj_get_coords(card->slot, &origin); lv_obj_get_coords(host, &host_area);
                card->drag_origin_y = origin.y1 - host_area.y1;
                lv_obj_set_parent(card->drag, host);
                lv_obj_set_pos(card->drag, origin.x1 - host_area.x1, card->drag_origin_y);
            }
            lv_obj_set_y(card->drag, card->drag_origin_y + dy);
        }
    } else if (code == LV_EVENT_RELEASED) {
        if (card->closing || card->release_timer) return;
        if (desktop.ignore_release_) { desktop.ignore_release_ = false; return; }
        const auto action = recent::release(desktop.axis_, point.x - desktop.press_.x, point.y - desktop.press_.y, desktop.threshold_);
        if (action != recent::Action::Close) {
            lv_obj_set_parent(card->drag, card->slot);
            lv_obj_set_pos(card->drag, 0, 0);
        }
        switch (action) {
        case recent::Action::Open: desktop.emit("open", card->id); break;
        case recent::Action::Close:
            // Do not stop an app on a single Released sample. Confirm continuous
            // release first; a renewed press cancels this pending dismissal.
            card->release_timer = lv_timer_create(confirm_card_release, 80, card);
            break;
        case recent::Action::Next: desktop.focus(desktop.focus_ + 1, true); break;
        case recent::Action::Previous: desktop.focus(desktop.focus_ ? desktop.focus_ - 1 : 0, true); break;
        case recent::Action::Rebound: break;
        }
    } else if (code == LV_EVENT_PRESS_LOST) {
        if (card->closing || card->release_timer) return;
        lv_obj_set_parent(card->drag, card->slot);
        lv_obj_set_pos(card->drag, 0, 0);
    }
}

void DesktopImpl::confirm_card_release(lv_timer_t *timer) {
    auto *card = static_cast<Card *>(lv_timer_get_user_data(timer));
    card->release_timer = nullptr;
    lv_timer_delete(timer);
    if (!valid(card->drag) || !card->input || lv_indev_get_state(card->input) != LV_INDEV_STATE_RELEASED) return;
    card->closing = true;
    const int height = lv_display_get_vertical_resolution(lv_obj_get_display(card->drag));
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, card->drag);
    lv_anim_set_user_data(&animation, card);
    lv_anim_set_exec_cb(&animation, [](void *object, int32_t y) { lv_obj_set_y(static_cast<lv_obj_t *>(object), y); });
    lv_anim_set_values(&animation, lv_obj_get_y(card->drag), -lv_obj_get_height(card->drag) - height / 10);
    lv_anim_set_duration(&animation, 180);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&animation, complete_card_close);
    if (!lv_anim_start(&animation)) card->desktop->emit("close", card->id);
}

void DesktopImpl::complete_card_close(lv_anim_t *animation) {
    auto *card = static_cast<Card *>(lv_anim_get_user_data(animation));
    // This event still travels through SystemGui to the app-input worker.
    card->desktop->emit("close", card->id);
}

void DesktopImpl::navigation_edges(int side, int margin) {
    auto *display = backend_.default_display(); if (!display) return;
    auto *layer = lv_display_get_layer_sys(display);
    const int width = lv_display_get_horizontal_resolution(display), height = lv_display_get_vertical_resolution(display);
    if (!valid(left_edge_)) left_edge_ = box(layer, side, height - 2 * margin);
    if (!valid(right_edge_)) right_edge_ = box(layer, side, height - 2 * margin);
    for (auto *edge : {left_edge_, right_edge_}) {
        transparent(edge); lv_obj_add_flag(edge, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(edge, side, height - 2 * margin);
    }
    lv_obj_set_pos(left_edge_, 0, margin); lv_obj_set_pos(right_edge_, width - side, margin);
    lv_obj_update_layout(layer);
}
void DesktopImpl::back_hint(int x, int y, int distance, int threshold, bool dark) {
    if (distance <= 3) { hide_back_hint(); return; }
    auto *display = backend_.default_display(); if (!display) return;
    const int width = lv_display_get_horizontal_resolution(display), height = lv_display_get_vertical_resolution(display);
    if (!valid(hint_)) {
        hint_ = box(lv_display_get_layer_sys(display), 44, 44);
        lv_obj_remove_flag(hint_, LV_OBJ_FLAG_CLICKABLE);
        hint_line_ = lv_line_create(hint_); lv_obj_remove_flag(hint_line_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_line_width(hint_line_, 3, 0); lv_obj_set_style_line_rounded(hint_line_, true, 0);
    }
    const bool left = x < width / 2;
    const bool armed = distance >= threshold;
    const int offset = std::clamp(distance / 3, 0, 22);
    lv_obj_remove_flag(hint_, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(hint_);
    lv_obj_set_pos(hint_, left ? -10 + offset : width - 34 - offset, std::clamp(y - 22, 32, height - 76));
    lv_obj_set_style_radius(hint_, 22, 0); lv_obj_set_style_bg_opa(hint_, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(hint_, lv_color_hex(armed ? 0x3478da : dark ? 0x44484e : 0xe1e4e9), 0);
    lv_obj_set_style_line_color(hint_line_, lv_color_hex(armed || dark ? 0xffffff : 0x20242c), 0);
    // Both edges perform Back; the chevron points inward from the origin edge.
    arrow_[0] = {static_cast<lv_value_precise_t>(left ? 7 : 0), 0};
    arrow_[1] = {static_cast<lv_value_precise_t>(left ? 0 : 7), 8};
    arrow_[2] = {static_cast<lv_value_precise_t>(left ? 7 : 0), 16};
    lv_line_set_points(hint_line_, arrow_, 3); lv_obj_center(hint_line_);
}
void DesktopImpl::hide_back_hint() { if (valid(hint_)) lv_obj_add_flag(hint_, LV_OBJ_FLAG_HIDDEN); }

std::expected<void, std::string> Backend::capture_app_snapshot(uint32_t id, const std::vector<std::string> &paths, bool dark) {
    Lock lock; if (!desktop_) desktop_ = std::make_unique<DesktopImpl>(*impl_); return desktop_->capture(id, paths, dark);
}
void Backend::release_app_snapshot(uint32_t id) { Lock lock; if (desktop_) desktop_->release(id); }
void Backend::clear_app_snapshots() { Lock lock; if (desktop_) { desktop_->hide(); desktop_->clear(); } }
std::expected<void, std::string> Backend::show_recent_tasks(const std::vector<RecentTask> &apps, uint32_t focus,
    bool dark, bool chinese, uint64_t generation, bool ignore_release) {
    Lock lock; if (!desktop_) desktop_ = std::make_unique<DesktopImpl>(*impl_);
    return desktop_->show(apps, focus, dark, chinese, generation, ignore_release);
}
void Backend::hide_recent_tasks() { Lock lock; if (desktop_) desktop_->hide(); }
void Backend::show_back_hint(int x, int y, int distance, int threshold, bool dark) {
    Lock lock; if (!desktop_) desktop_ = std::make_unique<DesktopImpl>(*impl_); desktop_->back_hint(x, y, distance, threshold, dark);
}
void Backend::hide_back_hint() { Lock lock; if (desktop_) desktop_->hide_back_hint(); }
void Backend::configure_navigation_edges(int side, int margin) {
    Lock lock; if (!desktop_) desktop_ = std::make_unique<DesktopImpl>(*impl_); desktop_->navigation_edges(side, margin);
}
}
