/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/shell_impl.hpp"
#include "private/navigation_gesture.hpp"

namespace esp_brookesia::system::super {

std::expected<void, std::string> ShellApp::mount_background(core::AppContext &context)
{
    (void)context;
    background_mounted_ = true;
    return {};
}


void ShellApp::unmount_background()
{
    if (context_ == nullptr || !background_mounted_) {
        return;
    }
    background_mounted_ = false;
}


std::expected<void, std::string> ShellApp::mount_overlay(core::AppContext &context)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();
    (void)context;
    overlay_mounted_ = true;
    system_ui_expanded_ = true;
    foreground_is_shell_ = true;

    if (overlay_action_connections_.empty()) {
        auto subscribe_mask_action = [this, &context](std::string_view action) -> std::expected<void, std::string> {
            auto connection = context.gui().subscribe_action(action, [this](const gui::Event &event)
            {
                handle_quick_control_event(event);
            });
            if (!connection.connected())
            {
                return std::unexpected("Failed to subscribe Shell system UI mask action");
            }
            overlay_action_connections_.push_back(std::move(connection));
            return {};
        };
        if (auto result = subscribe_mask_action(SUPER_ACTION_SYSTEM_UI_MASK_CLICK); !result) {
            return result;
        }
        if (auto result = subscribe_mask_action(SUPER_ACTION_SYSTEM_UI_MASK_GESTURE); !result) {
            return result;
        }
    }

    for (const auto action : {"ouse.quick.settings", "ouse.quick.wifi", "ouse.quick.theme", "ouse.quick.brightness", "ouse.quick.volume", "ouse.quick.mute"}) {
        auto connection = context.gui().subscribe_action(action, [this](const gui::Event &event) {
            handle_quick_control_event(event);
        });
        if (!connection.connected()) return std::unexpected("Failed to subscribe quick control action");
        overlay_action_connections_.push_back(std::move(connection));
    }

    refresh_status_clock();
    (void)context.timer().start_periodic("ouse.battery.poll", 5000);
    for (const auto action : {"ouse.recents.native"}) {
        auto connection = context.gui().subscribe_action(action,
            [this](const gui::Event &event) { handle_recent_action(event); });
        if (!connection.connected()) return std::unexpected("Failed to subscribe recent task action");
        overlay_action_connections_.push_back(std::move(connection));
    }
    schedule_status_clock_timer();
    refresh_wifi_status();
    subscribe_sntp_events();
    (void)configure_display_gesture();
    apply_debug_config(get_debug_config_snapshot());
    return refresh_system_ui_bindings();
}


void ShellApp::unmount_overlay()
{
    close_recent_tasks();
    if (owner_.desktop_backend_) owner_.desktop_backend_->hide_back_hint();
    if (context_ && quick_controls_animation_id_) context_->gui().stop_animation(quick_controls_animation_id_);
    quick_controls_animation_id_ = 0;
    if (context_ && launcher_page_animation_id_) context_->gui().stop_animation(launcher_page_animation_id_);
    launcher_page_animation_id_ = 0;
    ++launcher_page_generation_;
    quick_controls_open_ = false;
    ++status_peek_generation_;
    quick_audio_binding_.release();
    stop_debug_runtime();
    disconnect_overlay_actions();
    release_display_service_binding();
    release_sntp_service_binding();
    release_wifi_service_binding();
    cancel_gesture_exit_hold_timer();
    cancel_status_peek_auto_hide_timer();
    stop_status_clock_timer();
    if (context_ == nullptr) {
        return;
    }
    if (status_bar_animation_id_ != 0) {
        context_->gui().stop_animation(status_bar_animation_id_);
        status_bar_animation_id_ = 0;
    }
    if (gesture_indicator_animation_id_ != 0) {
        context_->gui().stop_animation(gesture_indicator_animation_id_);
        gesture_indicator_animation_id_ = 0;
    }
    if (gesture_indicator_x_animation_id_ != 0) {
        context_->gui().stop_animation(gesture_indicator_x_animation_id_);
        gesture_indicator_x_animation_id_ = 0;
    }
    loading_owners_.clear();
    launch_overlay_active_ = false;
    overlay_mounted_ = false;
}


bool ShellApp::ensure_display_service_binding()
{
    if (display_service_binding_.is_valid()) {
        if (DisplayHelper::is_running()) {
            return true;
        }
        disconnect_display_gesture();
        display_service_binding_.release();
    }
    if (!DisplayHelper::is_available()) {
        BROOKESIA_LOGW("Display service is not available for Shell touch gestures");
        return false;
    }

    display_service_binding_ = service::ServiceManager::get_instance().bind(DisplayHelper::get_name().data());
    if (!display_service_binding_.is_valid()) {
        BROOKESIA_LOGW("Failed to bind Display service for Shell touch gestures");
        return false;
    }
    if (!DisplayHelper::is_running()) {
        BROOKESIA_LOGW("Display service is not running for Shell touch gestures");
        display_service_binding_.release();
        return false;
    }
    return true;
}


void ShellApp::release_display_service_binding()
{
    disconnect_display_gesture();
    display_service_binding_.release();
}


bool ShellApp::configure_display_gesture()
{
    disconnect_display_gesture();
    if (!ensure_display_service_binding()) {
        return false;
    }

    auto gui_output_names = get_gui_display_output_names();
    auto outputs = DisplayService::get_instance().get_outputs();
    bool configured = false;
    for (const auto &output : outputs) {
        if (!output.touch.has_value()) {
            continue;
        }
        auto touch_snapshot = DisplayService::get_instance().get_touch_snapshot(output.name);
        if (!touch_snapshot) {
            continue;
        }
        if (!gui_output_names.empty() &&
                std::find(gui_output_names.begin(), gui_output_names.end(), output.name) == gui_output_names.end()) {
            continue;
        }

        DisplayService::TouchGestureConfig config;
        config.enabled = true;
        config.detect_period_ms = 20;
        config.direction_lock_enabled = true;
        config.release_debounce_ms = 40;
        config.threshold.horizontal_edge = get_gesture_horizontal_edge_px(output.width);
        config.threshold.vertical_edge = get_gesture_vertical_edge_px(output.height);
        auto result = DisplayService::get_instance().set_touch_gesture_config(output.id, config);
        if (!result) {
            BROOKESIA_LOGW(
                "Failed to configure Shell bottom gesture for Display output %1%: %2%",
                output.name, result.error()
            );
            continue;
        }
        configured = true;

        auto gesture_callback = [this](const std::string &, const DisplayService::TouchGestureInfo & info) {
            handle_display_touch_gesture(info);
        };
        auto connection = DisplayService::get_instance().connect_touch_gesture(output.name, gesture_callback);
        if (!connection.connected()) {
            BROOKESIA_LOGW("Failed to subscribe Display touch gesture for Shell output %1%", output.name);
            continue;
        }
        display_gesture_connections_.push_back(std::move(connection));
    }

    if (!configured) {
        BROOKESIA_LOGW("No touch-capable Display output is available for Shell bottom gesture");
        release_display_service_binding();
        return false;
    }

    if (display_gesture_connections_.empty()) {
        BROOKESIA_LOGW("Failed to subscribe Display touch gesture events for Shell");
        release_display_service_binding();
        return false;
    }
    if (owner_.desktop_backend_) {
        const auto environment = owner_.get_environment();
        owner_.desktop_backend_->configure_navigation_edges(navigation::side_width(environment.density),
            std::max(32, static_cast<int>(32 * environment.density)));
    }
    return true;
}


void ShellApp::disconnect_display_gesture()
{
    for (const auto &connection : display_gesture_connections_) {
        connection.disconnect();
    }
    display_gesture_connections_.clear();
    reset_gesture_indicator();
}


void ShellApp::handle_display_touch_gesture(const DisplayService::TouchGestureInfo &info)
{
    if (context_ == nullptr) {
        return;
    }
    if (launch_overlay_active_) {
        edge_back_tracking_ = false;
        if (owner_.desktop_backend_) owner_.desktop_backend_->hide_back_hint();
        return;
    }
    const auto environment = owner_.get_environment();
    if (info.event_type == DisplayService::TouchGestureEventType::Press)
        edge_back_tracking_ = (!foreground_is_shell_ || quick_controls_open_ || recent_tasks_open_) && navigation::side_origin(info.start_x, info.start_y,
            environment.width_px, environment.height_px, environment.density);
    const bool edge_back = info.event_type == DisplayService::TouchGestureEventType::Release && edge_back_tracking_ &&
        navigation::back_swipe(info.start_x, info.start_y, info.stop_x, info.stop_y,
            environment.width_px, environment.height_px, environment.density);
    if (edge_back_tracking_ && owner_.desktop_backend_) {
        if (info.event_type == DisplayService::TouchGestureEventType::Pressing) {
            const int inward = info.start_x < environment.width_px / 2 ?
                info.stop_x - info.start_x : info.start_x - info.stop_x;
            owner_.desktop_backend_->show_back_hint(info.start_x, info.start_y,
                std::abs(info.stop_y - info.start_y) <= std::abs(inward) ? inward : 0,
                std::max(40, static_cast<int>(48 * environment.density)), context_->gui().get_theme() == "dark");
        } else if (info.event_type == DisplayService::TouchGestureEventType::Release) {
            owner_.desktop_backend_->hide_back_hint();
        }
    }
    if (info.event_type == DisplayService::TouchGestureEventType::Release) edge_back_tracking_ = false;
    if (edge_back && keyboard_mounted_) { finish_keyboard(false); return; }
    if (edge_back && message_dialog_mounted_) { finish_message_dialog(-1, core::MessageDialogCloseReason::Closed); return; }
    if (message_dialog_mounted_ || message_dialog_closing_) {
        if (gesture_tracking_ || gesture_exit_armed_ || gesture_exit_triggered_ || status_peek_tracking_) {
            reset_gesture_indicator();
            status_peek_tracking_ = false;
            (void)refresh_system_ui_state_bindings();
        }
        return;
    }

    if (quick_controls_open_) {
        if (quick_controls_dragging_) {
            if (info.event_type == DisplayService::TouchGestureEventType::Pressing) {
                const auto delta = info.stop_y - info.start_y;
                move_quick_controls(quick_controls_closing_ ? delta : -environment.height_px + delta);
            } else if (info.event_type == DisplayService::TouchGestureEventType::Release) {
                const auto delta = info.stop_y - info.start_y;
                const int threshold = std::max<int32_t>(40, environment.height_px / 5);
                settle_quick_controls(quick_controls_closing_ ? delta > -threshold : delta >= threshold);
            }
            return;
        }
        if (!quick_controls_ready_) return;
        if (edge_back) { settle_quick_controls(false); return; }
        // Only the bottom handle starts dismissal. A slider owns its whole drag.
        if (info.event_type == DisplayService::TouchGestureEventType::Press &&
            info.start_y >= environment.height_px - static_cast<int32_t>(32 * environment.density)) {
            quick_controls_dragging_ = quick_controls_closing_ = true;
            quick_controls_ready_ = false;
            (void)context_->gui().set_binding_value("/overlay/quick_controls/input_shield", "hidden", "false");
        }
        return;
    }
    if (recent_tasks_open_) {
        if (recent_tasks_ignore_release_) {
            if (info.event_type == DisplayService::TouchGestureEventType::Release) recent_tasks_ignore_release_ = false;
            return;
        }
        if (edge_back) { close_recent_tasks(); return; }
        if (info.event_type == DisplayService::TouchGestureEventType::Release &&
            info.start_y > owner_.get_environment().height_px - 30 &&
            info.direction == DisplayService::TouchGestureDirection::Up) close_recent_tasks();
        return;
    }
    if (edge_back) {
        auto result = owner_.navigate_back();
        BROOKESIA_LOGI("Edge back: start(%1%,%2%), stop(%3%,%4%), success(%5%)",
            info.start_x, info.start_y, info.stop_x, info.stop_y, result.has_value());
        if (!result) BROOKESIA_LOGW("Edge back failed: %1%", result.error());
        return;
    }
    const bool started_from_bottom = has_gesture_area(info.start_area, SUPER_TOUCH_GESTURE_AREA_BOTTOM_EDGE);
    const bool started_from_top = has_gesture_area(info.start_area, SUPER_TOUCH_GESTURE_AREA_TOP_EDGE);
    if (foreground_is_shell_ && !started_from_top && !started_from_bottom) {
        const int dx = info.stop_x - info.start_x;
        const int dy = info.stop_y - info.start_y;
        if (info.event_type == DisplayService::TouchGestureEventType::Press) {
            launcher_ignore_click_ = launcher_page_animation_id_ != 0;
            launcher_swiping_ = false;
        } else if (info.event_type == DisplayService::TouchGestureEventType::Pressing) {
            // Even on a single page, dragging across an icon is not a click.
            if (std::max(std::abs(dx), std::abs(dy)) > 12 * environment.density) launcher_ignore_click_ = true;
            if (launcher_pages_ > 1 && !launcher_page_animation_id_ &&
                (launcher_swiping_ || (std::abs(dx) > 12 * environment.density && std::abs(dx) > std::abs(dy)))) {
                launcher_swiping_ = launcher_ignore_click_ = true;
                move_launcher_page(std::clamp<int32_t>(-launcher_page_ * launcher_page_width_ + dx,
                    -(launcher_pages_ - 1) * launcher_page_width_, 0));
            }
        } else if (info.event_type == DisplayService::TouchGestureEventType::Release && launcher_swiping_) {
            launcher_swiping_ = false;
            const int threshold = std::max<int32_t>(24, launcher_page_width_ / 5);
            settle_launcher_page(launcher_page_ + (dx < -threshold ? 1 : dx > threshold ? -1 : 0));
        }
        return;
    }
    if (info.event_type == DisplayService::TouchGestureEventType::Press) {
        if (started_from_top && can_start_status_peek()) {
            ++status_peek_generation_;
            status_peek_tracking_ = true;
            (void)refresh_system_ui_state_bindings();
            if (!started_from_bottom) {
                return;
            }
        }
        if (!started_from_bottom) {
            return;
        }
        reset_status_peek_session(false, true, "before bottom gesture");
        ++gesture_generation_;
        gesture_tracking_ = true;
        gesture_exit_armed_ = false;
        gesture_exit_triggered_ = false;
        (void)refresh_system_ui_state_bindings();
        cancel_gesture_exit_hold_timer();
        switch_display_to_gui_for_system_ui();
        if (gesture_indicator_max_width_ <= 0) {
            gesture_indicator_max_width_ = get_fallback_gesture_indicator_width(owner_.get_environment());
        }
        update_gesture_indicator_width(std::max<int32_t>(gesture_indicator_max_width_, 1));
        return;
    }

    if (status_peek_tracking_) {
        if (gesture_tracking_ || gesture_exit_armed_ || gesture_exit_triggered_) {
            status_peek_tracking_ = false;
            (void)refresh_system_ui_state_bindings();
            return;
        }
        if (info.event_type == DisplayService::TouchGestureEventType::Release) {
            status_peek_tracking_ = false;
            show_status_peek();
            (void)refresh_system_ui_state_bindings();
            return;
        }
        if (info.event_type != DisplayService::TouchGestureEventType::Pressing || !started_from_top) {
            status_peek_tracking_ = false;
            (void)refresh_system_ui_state_bindings();
            return;
        }
        const auto downward_delta = std::max(0, info.stop_y - info.start_y);
        const auto peek_distance = std::max<int32_t>(get_status_peek_distance_px(owner_.get_environment()), 1);
        if (downward_delta >= peek_distance &&
                (info.direction == DisplayService::TouchGestureDirection::Down ||
                 info.direction == DisplayService::TouchGestureDirection::None)) {
            trigger_status_peek();
            if (quick_controls_open_) move_quick_controls(-environment.height_px + downward_delta);
        }
        return;
    }

    if (!gesture_tracking_) {
        return;
    }

    if (info.event_type == DisplayService::TouchGestureEventType::Release) {
        if (!gesture_exit_triggered_ && info.start_y - info.stop_y >= navigation::home_distance(environment.height_px)) {
            cancel_gesture_exit_hold_timer();
            reset_gesture_indicator();
            auto result = owner_.go_home();
            BROOKESIA_LOGI("Bottom swipe Home: success(%1%)", result.has_value());
            if (!result) BROOKESIA_LOGW("Home failed: %1%", result.error());
            return;
        }
        if (!gesture_exit_triggered_) {
            cancel_gesture_exit_hold_timer();
            animate_gesture_indicator_rebound();
        }
        gesture_tracking_ = false;
        (void)refresh_system_ui_state_bindings();
        return;
    }

    if (info.event_type != DisplayService::TouchGestureEventType::Pressing) {
        return;
    }
    if (!started_from_bottom) {
        animate_gesture_indicator_rebound();
        gesture_tracking_ = false;
        (void)refresh_system_ui_state_bindings();
        return;
    }

    const auto upward_delta = std::max(0, info.start_y - info.stop_y);
    if (upward_delta <= 0 && info.direction != DisplayService::TouchGestureDirection::Up) {
        update_gesture_indicator_width(std::max<int32_t>(gesture_indicator_max_width_, 1));
        return;
    }

    const auto exit_distance = std::max<int32_t>(get_gesture_exit_distance_px(owner_.get_environment()), 1);
    const auto max_width = std::max<int32_t>(gesture_indicator_max_width_, 1);
    const auto progress = std::clamp(static_cast<float>(upward_delta) / static_cast<float>(exit_distance), 0.0F, 1.0F);
    const auto width = static_cast<int32_t>(static_cast<float>(max_width) * (1.0F - progress));
    if (progress < 1.0F) {
        gesture_exit_armed_ = false;
        cancel_gesture_exit_hold_timer();
        (void)refresh_system_ui_state_bindings();
        update_gesture_indicator_width(std::max<int32_t>(width, 1));
        return;
    }

    update_gesture_indicator_width(1);
    std::vector<gui::BindingValueUpdate> updates;
    add_gesture_indicator_binding_updates(
        updates,
        true,
        std::max<int32_t>(gesture_indicator_max_width_, 1),
        1
    );
    auto result = context_->gui().set_binding_values(updates);
    if (!result) {
        BROOKESIA_LOGW("Failed to hide Shell gesture indicator: %1%", result.error());
    }
    if (gesture_exit_armed_ || gesture_exit_hold_timer_id_ != core::INVALID_TIMER_ID) {
        return;
    }
    gesture_exit_armed_ = true;
    (void)refresh_system_ui_state_bindings();
    auto timer = context_->timer().start_delayed(SUPER_GESTURE_EXIT_HOLD_TIMER_NAME, SUPER_GESTURE_EXIT_HOLD_MS);
    if (!timer) {
        BROOKESIA_LOGW("Failed to start Shell gesture exit hold timer: %1%", timer.error());
        animate_gesture_indicator_rebound();
        return;
    }
    gesture_exit_hold_timer_id_ = *timer;
}


void ShellApp::reset_gesture_indicator()
{
    ++gesture_generation_;
    gesture_tracking_ = false;
    gesture_exit_armed_ = false;
    gesture_exit_triggered_ = false;
    cancel_gesture_exit_hold_timer();
    if (context_ == nullptr) {
        gesture_indicator_max_width_ = 0;
        return;
    }
    if (gesture_indicator_animation_id_ != 0) {
        context_->gui().stop_animation(gesture_indicator_animation_id_);
        gesture_indicator_animation_id_ = 0;
    }
    if (gesture_indicator_x_animation_id_ != 0) {
        context_->gui().stop_animation(gesture_indicator_x_animation_id_);
        gesture_indicator_x_animation_id_ = 0;
    }
    if (gesture_indicator_max_width_ <= 0) {
        gesture_indicator_max_width_ = get_fallback_gesture_indicator_width(owner_.get_environment());
    }
    const auto max_width = std::max<int32_t>(gesture_indicator_max_width_, 1);
    std::vector<gui::BindingValueUpdate> updates;
    add_gesture_indicator_binding_updates(updates, true, max_width, max_width);
    auto result = context_->gui().set_binding_values(updates);
    if (!result) {
        BROOKESIA_LOGW("Failed to reset Shell gesture indicator: %1%", result.error());
    }
    (void)refresh_system_ui_state_bindings();
}


void ShellApp::cancel_gesture_exit_hold_timer()
{
    if (context_ != nullptr && gesture_exit_hold_timer_id_ != core::INVALID_TIMER_ID) {
        (void)context_->timer().stop(gesture_exit_hold_timer_id_);
    }
    gesture_exit_hold_timer_id_ = core::INVALID_TIMER_ID;
}


void ShellApp::cancel_status_peek_auto_hide_timer()
{
    if (context_ != nullptr && status_peek_auto_hide_timer_id_ != core::INVALID_TIMER_ID) {
        (void)context_->timer().stop(status_peek_auto_hide_timer_id_);
    }
    status_peek_auto_hide_timer_id_ = core::INVALID_TIMER_ID;
}


bool ShellApp::can_start_status_peek() const
{
    return context_ != nullptr && !quick_controls_open_ && !keyboard_mounted_ && !launch_overlay_active_ && !message_dialog_mounted_ &&
           !message_dialog_closing_ && !gesture_tracking_ && !gesture_exit_armed_ && !gesture_exit_triggered_;
}

void ShellApp::show_status_peek()
{
    if (!context_ || foreground_is_shell_ || quick_controls_open_) return;
    status_peek_visible_ = true;
    auto expanded = set_system_ui_expanded(true);
    if (!expanded) { status_peek_visible_ = false; return; }
    cancel_status_peek_auto_hide_timer();
    auto timer = context_->timer().start_delayed(SUPER_STATUS_PEEK_AUTO_HIDE_TIMER_NAME, SUPER_STATUS_PEEK_HOLD_MS);
    if (timer) status_peek_auto_hide_timer_id_ = *timer;
    else collapse_status_peek(true);
}


void ShellApp::trigger_status_peek()
{
    if (!can_start_status_peek()) {
        return;
    }
    BROOKESIA_LOGI(
        "Shell top status peek triggered: foreground_is_shell(%1%), tracking(%2%), visible(%3%)",
        foreground_is_shell_, status_peek_tracking_, status_peek_visible_
    );
    status_peek_tracking_ = false;
    status_peek_visible_ = true;
    ++status_peek_generation_;
    cancel_status_peek_auto_hide_timer();

    auto expand_result = set_system_ui_expanded(true);
    if (!expand_result) {
        BROOKESIA_LOGW("Failed to show Shell status peek: %1%", expand_result.error());
        status_peek_visible_ = false;
        return;
    }

    quick_controls_open_ = true;
    quick_controls_dragging_ = true;
    quick_controls_closing_ = quick_controls_ready_ = false;
    (void)context_->gui().set_binding_value("/overlay/quick_controls/input_shield", "hidden", "false");
    move_quick_controls(-owner_.get_environment().height_px);
    (void)context_->gui().set_binding_value("/overlay/quick_controls", "hidden", "false");
    refresh_quick_controls();
    (void)refresh_system_ui_state_bindings();

}


void ShellApp::collapse_status_peek(bool restore_display)
{
    ++status_peek_generation_;
    status_peek_tracking_ = false;
    status_peek_visible_ = false;
    quick_controls_open_ = false;
    if (context_) (void)context_->gui().set_binding_value("/overlay/quick_controls", "hidden", "true");
    cancel_status_peek_auto_hide_timer();
    if (context_ == nullptr) {
        return;
    }

    if (status_bar_animation_id_ != 0) {
        context_->gui().stop_animation(status_bar_animation_id_);
        status_bar_animation_id_ = 0;
    }
    system_ui_expanded_ = false;
    auto state_result = refresh_system_ui_state_bindings();
    if (!state_result) {
        BROOKESIA_LOGW("Failed to update Shell status peek mask: %1%", state_result.error());
    }
    auto result = refresh_system_ui_position_bindings();
    if (!result) {
        BROOKESIA_LOGW("Failed to collapse Shell status peek: %1%", result.error());
    }
    if (restore_display) {
        restore_display_after_system_ui();
    }
}


void ShellApp::reset_status_peek_session(bool restore_display, bool collapse_immediately, const char *reason)
{
    const bool should_reset = status_peek_tracking_ || status_peek_visible_ ||
                              status_peek_auto_hide_timer_id_ != core::INVALID_TIMER_ID ||
                              (!foreground_is_shell_ && system_ui_expanded_) ||
                              !display_source_restore_records_.empty();
    if (!should_reset) {
        return;
    }

    BROOKESIA_LOGI(
        "Reset Shell status peek: reason(%1%), foreground_is_shell(%2%), tracking(%3%), visible(%4%), "
        "expanded(%5%), restore_display(%6%), immediate(%7%)",
        reason != nullptr ? reason : "unknown",
        foreground_is_shell_,
        status_peek_tracking_,
        status_peek_visible_,
        system_ui_expanded_,
        restore_display,
        collapse_immediately
    );

    ++status_peek_generation_;
    status_peek_tracking_ = false;
    status_peek_visible_ = false;
    quick_controls_open_ = false;
    if (context_) (void)context_->gui().set_binding_value("/overlay/quick_controls", "hidden", "true");
    cancel_status_peek_auto_hide_timer();

    if (context_ == nullptr) {
        if (restore_display) {
            BROOKESIA_LOGI("Restore display source while resetting Shell status peek without GUI context");
            restore_display_after_system_ui();
        }
        return;
    }

    if (collapse_immediately) {
        if (status_bar_animation_id_ != 0) {
            context_->gui().stop_animation(status_bar_animation_id_);
            status_bar_animation_id_ = 0;
        }
        system_ui_expanded_ = foreground_is_shell_;
        auto result = refresh_system_ui_position_bindings();
        if (!result) {
            BROOKESIA_LOGW("Failed to immediately reset Shell status peek: %1%", result.error());
        }
        if (restore_display) {
            BROOKESIA_LOGI("Restore display source while immediately resetting Shell status peek");
            restore_display_after_system_ui();
        }
        return;
    }

    if (!system_ui_expanded_) {
        if (restore_display) {
            BROOKESIA_LOGI("Restore display source after Shell status peek reset without collapse animation");
            restore_display_after_system_ui();
        }
        return;
    }

    auto collapse_result = set_system_ui_expanded(false);
    if (!collapse_result) {
        BROOKESIA_LOGW("Failed to animate Shell status peek reset: %1%", collapse_result.error());
        if (restore_display) {
            restore_display_after_system_ui();
        }
    }
}


void ShellApp::update_gesture_indicator_width(int32_t width)
{
    if (context_ == nullptr) {
        return;
    }
    if (gesture_indicator_animation_id_ != 0) {
        context_->gui().stop_animation(gesture_indicator_animation_id_);
        gesture_indicator_animation_id_ = 0;
    }
    if (gesture_indicator_x_animation_id_ != 0) {
        context_->gui().stop_animation(gesture_indicator_x_animation_id_);
        gesture_indicator_x_animation_id_ = 0;
    }
    if (gesture_indicator_max_width_ <= 0) {
        gesture_indicator_max_width_ = get_fallback_gesture_indicator_width(owner_.get_environment());
    }
    const auto clamped_width = std::clamp<int32_t>(width, 1, std::max<int32_t>(gesture_indicator_max_width_, 1));
    std::vector<gui::BindingValueUpdate> updates;
    add_gesture_indicator_binding_updates(
        updates,
        false,
        std::max<int32_t>(gesture_indicator_max_width_, 1),
        clamped_width
    );
    auto result = context_->gui().set_binding_values(updates);
    if (!result) {
        BROOKESIA_LOGW("Failed to update Shell gesture indicator: %1%", result.error());
    }
}


void ShellApp::animate_gesture_indicator_rebound()
{
    if (context_ == nullptr) {
        restore_display_after_system_ui();
        return;
    }
    if (gesture_indicator_animation_id_ != 0) {
        context_->gui().stop_animation(gesture_indicator_animation_id_);
        gesture_indicator_animation_id_ = 0;
    }
    if (gesture_indicator_x_animation_id_ != 0) {
        context_->gui().stop_animation(gesture_indicator_x_animation_id_);
        gesture_indicator_x_animation_id_ = 0;
    }
    if (gesture_indicator_max_width_ <= 0) {
        gesture_indicator_max_width_ = get_fallback_gesture_indicator_width(owner_.get_environment());
    }

    std::vector<gui::BindingValueUpdate> updates;
    add_binding_update(updates, SUPER_GESTURE_INDICATOR_PATH, "gesture_indicator_hidden", "false");
    auto binding_result = context_->gui().set_binding_values(updates);
    if (!binding_result) {
        BROOKESIA_LOGW("Failed to show Shell gesture indicator before rebound: %1%", binding_result.error());
        reset_gesture_indicator();
        restore_display_after_system_ui();
        return;
    }

    const auto generation = ++gesture_generation_;
    auto x_animation = make_timed_animation(gui::AnimationProperty::X, 0, SUPER_GESTURE_REBOUND_MS);
    auto x_animation_result = context_->gui().start_view_animation_with_result(
                                  SUPER_GESTURE_INDICATOR_BAR_PATH,
                                  x_animation,
    [this, generation]() {
        if (context_ == nullptr || generation != gesture_generation_) {
            return;
        }
        gesture_indicator_x_animation_id_ = 0;
    }
                              );
    if (!x_animation_result) {
        BROOKESIA_LOGW("Failed to start Shell gesture indicator x rebound: %1%", x_animation_result.error());
    } else {
        gesture_indicator_x_animation_id_ = x_animation_result->subscription_id;
    }

    auto animation = make_timed_animation(
                         gui::AnimationProperty::Width,
                         std::max<int32_t>(gesture_indicator_max_width_, 1),
                         SUPER_GESTURE_REBOUND_MS
                     );
    auto animation_result = context_->gui().start_view_animation_with_result(
                                SUPER_GESTURE_INDICATOR_BAR_PATH,
                                animation,
    [this, generation]() {
        if (context_ == nullptr || generation != gesture_generation_) {
            return;
        }
        gesture_indicator_animation_id_ = 0;
        gesture_indicator_x_animation_id_ = 0;
        reset_gesture_indicator();
        restore_display_after_system_ui();
    }
                            );
    if (!animation_result) {
        BROOKESIA_LOGW("Failed to start Shell gesture indicator rebound: %1%", animation_result.error());
        reset_gesture_indicator();
        restore_display_after_system_ui();
        return;
    }
    gesture_indicator_animation_id_ = animation_result->subscription_id;
}


void ShellApp::trigger_gesture_exit()
{
    if (context_ == nullptr || gesture_exit_triggered_) {
        return;
    }
    gesture_exit_triggered_ = true;
    gesture_tracking_ = false;
    gesture_exit_armed_ = false;
    status_peek_tracking_ = false;
    status_peek_visible_ = false;
    quick_controls_open_ = false;
    if (context_) (void)context_->gui().set_binding_value("/overlay/quick_controls", "hidden", "true");
    cancel_status_peek_auto_hide_timer();
    if (gesture_indicator_animation_id_ != 0) {
        context_->gui().stop_animation(gesture_indicator_animation_id_);
        gesture_indicator_animation_id_ = 0;
    }
    if (gesture_indicator_x_animation_id_ != 0) {
        context_->gui().stop_animation(gesture_indicator_x_animation_id_);
        gesture_indicator_x_animation_id_ = 0;
    }
    if (gesture_indicator_max_width_ <= 0) {
        gesture_indicator_max_width_ = get_fallback_gesture_indicator_width(owner_.get_environment());
    }
    const auto max_width = std::max<int32_t>(gesture_indicator_max_width_, 1);
    std::vector<gui::BindingValueUpdate> updates;
    add_gesture_indicator_binding_updates(updates, true, max_width, max_width);
    auto binding_result = context_->gui().set_binding_values(updates);
    if (!binding_result) {
        BROOKESIA_LOGW("Failed to hide Shell gesture indicator after exit trigger: %1%", binding_result.error());
    }
    auto result = owner_.open_recent_tasks();
    if (!result) {
        BROOKESIA_LOGW("Failed to close active app from Shell bottom gesture: %1%", result.error());
        gesture_exit_triggered_ = false;
        (void)refresh_system_ui_state_bindings();
        restore_display_after_system_ui();
        return;
    }
    clear_display_source_restore_records();
}


}
