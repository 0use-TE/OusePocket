// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cmath>
#include <tuple>
#include "private/shell_impl.hpp"
#include "brookesia/service_helper/media/audio.hpp"
#include "pocket_platform.hpp"
#include "brookesia/service_helper/media/display.hpp"
#include "brookesia/hal_interface/interface.hpp"
#include "brookesia/hal_interface/interfaces/system/general.hpp"

namespace esp_brookesia::system::super {
namespace {
using Audio = service::helper::AudioPlayback;
using Display = service::helper::Display;
constexpr auto PANEL = "/overlay/quick_controls/sheet/panel";
constexpr int TIMEOUT = 1000;
}

void ShellApp::close_quick_controls()
{
    if (!context_ || !quick_controls_open_) return;
    if (quick_controls_animation_id_) context_->gui().stop_animation(quick_controls_animation_id_);
    quick_controls_animation_id_ = 0;
    quick_controls_dragging_ = quick_controls_closing_ = quick_controls_ready_ = false;
    quick_controls_open_ = false;
    status_peek_tracking_ = false;
    status_peek_visible_ = false;
    ++status_peek_generation_;
    cancel_status_peek_auto_hide_timer();
    (void)context_->gui().set_binding_value("/overlay/quick_controls", "hidden", "true");
    // Desktop retains its status bar; apps return to their immersive state.
    (void)set_system_ui_expanded(foreground_is_shell_);
    (void)refresh_system_ui_state_bindings();
}

void ShellApp::move_quick_controls(int32_t y)
{
    if (!context_) return;
    quick_controls_y_ = std::clamp<int32_t>(y, -owner_.get_environment().height_px, 0);
    (void)context_->gui().set_binding_value("/overlay/quick_controls/sheet", "y", std::to_string(quick_controls_y_));
}

void ShellApp::settle_quick_controls(bool open)
{
    if (!context_) return;
    quick_controls_dragging_ = quick_controls_closing_ = false;
    quick_controls_ready_ = false;
    (void)context_->gui().set_binding_value("/overlay/quick_controls/input_shield", "hidden", "false");
    const auto generation = ++status_peek_generation_;
    if (quick_controls_animation_id_) context_->gui().stop_animation(quick_controls_animation_id_);
    const auto target = open ? 0 : -owner_.get_environment().height_px;
    auto complete = [this, generation, open, target]() {
        if (!context_ || generation != status_peek_generation_) return;
        quick_controls_animation_id_ = 0;
        move_quick_controls(target);
        if (!open) { close_quick_controls(); return; }
        quick_controls_ready_ = true;
        (void)context_->gui().set_binding_value("/overlay/quick_controls/input_shield", "hidden", "true");
    };
    auto result = context_->gui().start_view_animation_with_result("/overlay/quick_controls/sheet",
        make_timed_animation(gui::AnimationProperty::Y, target, 180), complete);
    quick_controls_animation_id_ = result ? result->subscription_id : 0;
    if (!result) complete();
}

void ShellApp::refresh_quick_controls()
{
    if (!context_ || !quick_controls_open_) return;
    const bool zh = context_->gui().get_language() == "zh_CN";
    quick_controls_syncing_ = true;
    quick_output_id_.reset();
    for (const auto &output : service::Display::get_instance().get_outputs()) {
        if (output.backlight.has_value()) {
            quick_output_id_ = output.id;
            break;
        }
    }
    bool brightness_ready = false;
    if (quick_output_id_) {
        auto value = Display::call_function_sync<double>(Display::FunctionId::GetBacklightBrightness,
            static_cast<double>(*quick_output_id_), service::helper::Timeout(TIMEOUT));
        if (value) {
            quick_brightness_ = std::clamp(static_cast<int>(*value), 10, 100);
            brightness_ready = true;
        }
    }
    if (!quick_audio_binding_.is_valid() && Audio::is_available()) {
        quick_audio_binding_ = service::ServiceManager::get_instance().bind(Audio::get_name().data());
    }
    bool audio_ready = false;
    if (quick_audio_binding_.is_valid()) {
        auto volume = Audio::call_function_sync<double>(Audio::FunctionId::GetVolume,
            service::helper::Timeout(TIMEOUT));
        auto muted = Audio::call_function_sync<bool>(Audio::FunctionId::GetMute,
            service::helper::Timeout(TIMEOUT));
        if (volume && muted) {
            quick_volume_ = std::clamp(static_cast<int>(*volume), 0, 100);
            quick_muted_ = *muted;
            audio_ready = true;
        }
    }
    std::vector<gui::BindingValueUpdate> updates;
    auto text = [&](const char *node, std::string value) {
        add_binding_update(updates, std::string(PANEL) + "/" + node, "text", std::move(value));
    };
    text("title", zh ? "快捷设置" : "Quick controls");
    text("wifi/label", wifi_connected_ ? (zh ? "Wi-Fi 已连接" : "Wi-Fi connected") :
                                   (zh ? "Wi-Fi 未连接" : "Wi-Fi not connected"));
    text("brightness_label", (zh ? "亮度 " : "Brightness ") + std::to_string(quick_brightness_) + "%");
    text("volume_label", (zh ? "音量 " : "Volume ") + std::to_string(quick_volume_) + "%");
    text("mute/label", quick_muted_ ? (zh ? "取消静音" : "Unmute") : (zh ? "静音" : "Mute"));
    const bool dark = context_->gui().get_theme() == "dark";
    auto selected_theme = context_->gui().get_stored_theme_preference().value_or(context_->gui().get_theme());
    if (selected_theme != "light" && selected_theme != "dark") selected_theme = context_->gui().get_theme();
    const bool theme_pending = selected_theme != context_->gui().get_theme();
    text("theme/label", std::string(zh ? "主题" : "Theme") +
        (theme_pending ? (zh ? "\n待重启" : "\nPending restart") : ""));
    const auto suffix = dark ? ".dark" : ".light";
    for (const auto &[node, kind] : {std::pair{"wifi/icon", "wifi"}, std::pair{"mute/icon", "mute"},
            std::pair{"theme/icon", "theme"}, std::pair{"settings/icon", "settings"},
            std::pair{"brightness_icon", "sun"}, std::pair{"volume_icon", "volume"}}) {
        add_binding_update(updates, std::string(PANEL) + "/" + node, "src", std::string("quick.") + kind + suffix);
    }
    add_binding_update(updates, std::string(PANEL) + "/mute", "bg",
        quick_muted_ ? "${color.primary.soft}" : "${color.surface.raised}");
    add_binding_update(updates, std::string(PANEL) + "/wifi", "bg",
        wifi_connected_ ? "${color.primary.soft}" : "${color.surface.raised}");
    add_binding_update(updates, std::string(PANEL) + "/theme", "bg",
        selected_theme == "dark" ? "${color.primary.soft}" : "${color.surface.raised}");
    refresh_status_clock();
    text("feedback", (!brightness_ready || !audio_ready) ?
        (zh ? "部分功能不可用" : "Some controls unavailable") : "");
    for (const auto &[node, value, ready] : {
            std::tuple{"brightness", quick_brightness_, brightness_ready},
            std::tuple{"volume", quick_volume_, audio_ready}}) {
        add_binding_update(updates, std::string(PANEL) + "/" + node, "value", std::to_string(value));
        add_binding_update(updates, std::string(PANEL) + "/" + node, "disabled", ready ? "false" : "true");
    }
    add_binding_update(updates, std::string(PANEL) + "/mute", "disabled", audio_ready ? "false" : "true");
    auto result = context_->gui().set_binding_values(updates);
    quick_controls_syncing_ = false;
    if (!result) BROOKESIA_LOGW("Quick controls bindings failed: %1%", result.error());
}

void ShellApp::handle_quick_control_event(const gui::Event &event)
{
    if (!context_ || !quick_controls_open_ || !quick_controls_ready_ || quick_controls_syncing_) return;
    if (event.action == "ouse.quick.settings" || event.action == "ouse.quick.wifi") {
        close_quick_controls();
        for (const auto &app : owner_.list_apps()) {
            if (app.manifest.id == "brookesia.general.settings") {
                auto started = owner_.activate_app(app.app_id);
                if (!started) BROOKESIA_LOGW("Quick settings launch failed: %1%", started.error());
                break;
            }
        }
        return;
    }
    if (event.action == "ouse.quick.theme") {
        const bool zh = context_->gui().get_language() == "zh_CN";
        const auto active = context_->gui().get_theme();
        auto selected = context_->gui().get_stored_theme_preference().value_or(active);
        if (selected != "light" && selected != "dark") selected = active;
        const std::string target = selected == "dark" ? "light" : "dark";
        // Match Settings: persist preference only; existing documents keep
        // their active theme until restart. Both entries share the same key.
        auto saved = context_->gui().save_theme_preference(target);
        if (!saved) {
            (void)context_->gui().set_binding_value(std::string(PANEL) + "/feedback", "text",
                zh ? "主题保存失败" : "Failed to save theme");
            BROOKESIA_LOGW("Quick theme save failed: %1%", saved.error());
            return;
        }
        refresh_quick_controls();
        if (target == active) return; // Selecting the active theme cancels pending change.
        close_quick_controls(); // The system dialog must be above the sheet.
        auto prompt = owner_.show_system_message_dialog(core::MessageDialogOptions{
            .text = zh ? "主题已保存，重启后生效" : "Theme saved. Restart to apply.",
            .informative_text = zh ? "选择稍后可继续使用当前主题。" : "Later keeps the current theme until restart.",
            .icon = core::MessageDialogIcon::Question,
            .buttons = {
                {zh ? "立即重启" : "Restart now", core::MessageDialogButtonRole::Accept},
                {zh ? "稍后" : "Later", core::MessageDialogButtonRole::Reject},
            },
        }, [owner = &owner_, zh](const core::MessageDialogResult &answer) {
            if (answer.reason != core::MessageDialogCloseReason::Button || answer.button_index != 0) return;
            auto restart = hal::acquire_first_interface<hal::system::GeneralIface>();
            auto result = restart != nullptr ? restart->restart() :
                std::expected<void, std::string>(std::unexpected("Restart interface unavailable"));
            if (!result) {
                BROOKESIA_LOGW("Quick theme restart failed: %1%", result.error());
                (void)owner->show_system_message_dialog(core::MessageDialogOptions{
                    .text = (zh ? "重启失败：" : "Restart failed: ") + result.error(),
                    .informative_text = "",
                    .icon = core::MessageDialogIcon::Warning,
                    .buttons = {{zh ? "关闭" : "Close", core::MessageDialogButtonRole::Accept}},
                });
            }
        });
        if (!prompt) BROOKESIA_LOGW("Quick theme restart prompt failed: %1%", prompt.error());
        return;
    }
    std::expected<void, std::string> result;
    if (event.action == "ouse.quick.brightness" || event.action == "ouse.quick.volume") {
        const auto raw = event.get_double("value");
        if (!raw || !std::isfinite(*raw)) return;
        const bool brightness = event.action == "ouse.quick.brightness";
        const int value = std::clamp(static_cast<int>(*raw), brightness ? 10 : 0, 100);
        if (brightness && quick_output_id_) {
            if (value == quick_brightness_) return;
            result = Display::call_function_sync(Display::FunctionId::SetBacklightBrightness,
                static_cast<double>(*quick_output_id_), static_cast<double>(value), service::helper::Timeout(TIMEOUT));
            if (result) quick_brightness_ = value;
        } else if (!brightness && quick_audio_binding_.is_valid()) {
            if (value == quick_volume_) return;
            result = Audio::call_function_sync(Audio::FunctionId::SetVolume,
                static_cast<double>(value), service::helper::Timeout(TIMEOUT));
            if (result) quick_volume_ = value;
        } else return;
    } else if (event.action == "ouse.quick.mute" && quick_audio_binding_.is_valid()) {
        result = Audio::call_function_sync(Audio::FunctionId::SetMute, !quick_muted_, service::helper::Timeout(TIMEOUT));
        if (result) quick_muted_ = !quick_muted_;
    } else return;
    const bool zh = context_->gui().get_language() == "zh_CN";
    (void)context_->gui().set_binding_value(std::string(PANEL) + "/feedback", "text",
        result ? "" : (zh ? "操作失败" : "Operation failed"));
    // Update labels without rewriting the moving slider, which could disrupt dragging.
    (void)context_->gui().set_binding_value(std::string(PANEL) + "/brightness_label", "text",
        (zh ? "亮度 " : "Brightness ") + std::to_string(quick_brightness_) + "%");
    (void)context_->gui().set_binding_value(std::string(PANEL) + "/volume_label", "text",
        (zh ? "音量 " : "Volume ") + std::to_string(quick_volume_) + "%");
    (void)context_->gui().set_binding_value(std::string(PANEL) + "/mute/label", "text",
        quick_muted_ ? (zh ? "取消静音" : "Unmute") : (zh ? "静音" : "Mute"));
    (void)context_->gui().set_binding_value(std::string(PANEL) + "/mute", "bg",
        quick_muted_ ? "${color.primary.soft}" : "${color.surface.raised}");
    if (!result) BROOKESIA_LOGW("Quick control operation failed: %1%", result.error());
}
} // namespace esp_brookesia::system::super
