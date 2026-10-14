// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cmath>
#include <tuple>
#include "private/shell_impl.hpp"
#include "brookesia/service_helper/media/audio.hpp"
#include "brookesia/service_helper/media/display.hpp"

namespace esp_brookesia::system::super {
namespace {
using Audio = service::helper::AudioPlayback;
using Display = service::helper::Display;
constexpr auto PANEL = "/overlay/quick_controls/panel";
constexpr int TIMEOUT = 1000;
}

void ShellApp::close_quick_controls()
{
    if (!context_ || !quick_controls_open_) return;
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
    text("wifi", wifi_connected_ ? (zh ? "Wi-Fi 已连接" : "Wi-Fi connected") :
                                   (zh ? "Wi-Fi 未连接" : "Wi-Fi not connected"));
    text("brightness_label", (zh ? "亮度 " : "Brightness ") + std::to_string(quick_brightness_) + "%");
    text("volume_label", (zh ? "音量 " : "Volume ") + std::to_string(quick_volume_) + "%");
    text("mute/label", quick_muted_ ? (zh ? "取消静音" : "Unmute") : (zh ? "静音" : "Mute"));
    text("close/label", zh ? "关闭" : "Close");
    add_binding_update(updates, "/overlay/quick_controls/handle", "text",
        zh ? "向上滑动" : "Swipe up to close");
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
    if (!context_ || !quick_controls_open_ || quick_controls_syncing_) return;
    if (event.action == "ouse.quick.close" || event.action == SUPER_ACTION_SYSTEM_UI_MASK_CLICK) {
        close_quick_controls();
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
    if (!result) BROOKESIA_LOGW("Quick control operation failed: %1%", result.error());
}
} // namespace esp_brookesia::system::super
