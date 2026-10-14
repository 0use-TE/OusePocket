/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "brookesia/system_super/macro_configs.h"
#if !BROOKESIA_SYSTEM_SUPER_ENABLE_DEBUG_LOG
#   define BROOKESIA_LOG_DISABLE_DEBUG_TRACE 1
#endif
#include "private/utils.hpp"
#include "private/shell_app.hpp"
#include "brookesia/system_super/system.hpp"

namespace esp_brookesia::system::super {

std::expected<void, std::string> System::activate_app(core::AppId app_id)
{
    const auto previous = get_active_app();
    const bool pause_previous = previous && previous->app_id != shell_app_id_ && previous->app_id != app_id;
    if (pause_previous) {
        auto paused = pause_app(previous->app_id);
        if (!paused) return paused;
    }
    const auto target = get_app(app_id);
    auto result = target && target->state == core::AppState::Running
        ? resume_app(app_id) : start_app(app_id, core::System::AppStartOptions{});
    if (!result && pause_previous) {
        auto restored = resume_app(previous->app_id);
        if (!restored) BROOKESIA_LOGW("Failed to restore previous app: %1%", restored.error());
    }
    return result;
}

bool System::is_shell_active() const
{
    const auto active = get_active_app();
    if (active.has_value()) {
        return active->app_id == shell_app_id_;
    }
    const auto shell = get_app(shell_app_id_);
    return shell.has_value() && (shell->state == core::AppState::Running || shell->state == core::AppState::Paused);
}

std::expected<void, std::string> System::on_app_navigation_home(core::AppId app_id)
{
    const auto active = get_active_app();
    if (!active || active->app_id != app_id) return {}; // A paused app cannot navigate the foreground.
    return go_home();
}

std::expected<void, std::string> System::open_shell_page(ShellPage page)
{
    pending_shell_page_ = page;

    const auto active = get_active_app();
    if (active.has_value() && active->app_id != shell_app_id_) {
        auto paused = pause_app(active->app_id);
        if (!paused) return paused;
        std::vector<core::AppId> retained;
        for (const auto &app : list_apps()) {
            if (app.app_id != shell_app_id_ && app.state == core::AppState::Paused)
                retained.push_back(app.app_id);
        }
        while (retained.size() > 3) {
            auto victim = retained.front();
            if (victim == active->app_id) victim = retained[1];
            auto stopped = stop_app(victim);
            if (!stopped) return stopped;
            retained.erase(std::find(retained.begin(), retained.end(), victim));
        }
    }
    return restore_shell();
}

std::expected<void, std::string> System::open_app_launcher()
{
    return open_shell_page(ShellPage::AppLauncher);
}

std::expected<void, std::string> System::open_quick_control()
{
    return std::unexpected("Shell Quick Control page is no longer available");
}

std::expected<void, std::string> System::open_system_monitor()
{
    return std::unexpected("Shell System Monitor page is no longer available");
}

std::expected<void, std::string> System::open_notifications()
{
    return open_shell_page(ShellPage::AppLauncher);
}

std::expected<void, std::string> System::close_active_app()
{
    auto active = get_active_app();
    if (!active || active->app_id == shell_app_id_) {
        return shell_app_ ? shell_app_->set_foreground_app(std::nullopt) : std::expected<void, std::string> {};
    }
    pending_shell_page_ = ShellPage::AppLauncher;
    return stop_app(active->app_id);
}

std::expected<void, std::string> System::go_home()
{
    if (shell_app_) shell_app_->close_recent_tasks();
    return open_shell_page(ShellPage::AppLauncher);
}

std::expected<void, std::string> System::navigate_back()
{
    const auto active = get_active_app();
    if (!active || active->app_id == shell_app_id_) return go_home();
    auto result = send_app_action(active->app_id, "system.navigation.back");
    if (!result && result.error().find("navigation.home") != std::string::npos) return go_home();
    return result;
}

std::expected<void, std::string> System::open_recent_tasks()
{
    const auto active = get_active_app();
    recent_focus_app_id_ = active && active->app_id != shell_app_id_ ? active->app_id : core::INVALID_APP_ID;
    auto home = go_home();
    if (!home) return home;
    return shell_app_ ? shell_app_->show_recent_tasks(true) : std::expected<void, std::string>{};
}

std::expected<void, std::string> System::restore_shell()
{
    if (shell_app_id_ == core::INVALID_APP_ID) {
        return {};
    }
    auto shell = get_app(shell_app_id_);
    if (shell && (shell->state == core::AppState::Running || shell->state == core::AppState::Paused)) {
        auto resumed = resume_app(shell_app_id_);
        if (!resumed) return resumed;
        if (shell_app_) {
            auto page_result = shell_app_->show_page(pending_shell_page_);
            if (!page_result) {
                return page_result;
            }
            return shell_app_->set_foreground_app(std::nullopt);
        }
        return {};
    }

    auto start_result = start_app(shell_app_id_);
    if (!start_result) {
        return start_result;
    }
    if (pending_shell_page_ == ShellPage::AppLauncher || shell_app_ == nullptr) {
        return {};
    }
    return shell_app_->show_page(pending_shell_page_);
}

} // namespace esp_brookesia::system::super
