#include <memory>
#include <expected>
#include <string>
#include <vector>
#include "boost/json/array.hpp"
#include "brookesia/lib_utils/describe_helpers.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "brookesia/gui_lvgl.hpp"
#include "brookesia/system_super.hpp"
#include "brookesia/service_helper/media/display.hpp"
#include "brookesia/service_manager/service/manager.hpp"
#include "ouse_preferences.hpp"

using namespace esp_brookesia;
using DisplayHelper = service::helper::Display;

class PocketSystem final : public system::super::System {
public:
    explicit PocketSystem(double output_id) : output_id_(output_id) {}

    std::expected<void, std::string> reveal_display()
    {
        if (display_revealed_) return {};
        auto &source = gui::lvgl::DisplaySource::get_instance();
        if (esp_lv_adapter_refresh_now(source.display()) != ESP_OK) {
            return std::unexpected("Initial UI refresh failed; keeping backlight off");
        }
        // Let the final asynchronous SPI transfer settle before turning on the light.
        vTaskDelay(pdMS_TO_TICKS(100));
        auto result = DisplayHelper::call_function_sync(
            DisplayHelper::FunctionId::SetBacklightOnOff, output_id_, true,
            service::helper::Timeout(1000));
        if (!result) return result;
        display_revealed_ = true;
        return {};
    }

    void darken_display()
    {
        auto result = DisplayHelper::call_function_sync(
            DisplayHelper::FunctionId::SetBacklightOnOff, output_id_, false,
            service::helper::Timeout(1000));
        if (!result) ESP_LOGE("Ouse", "Backlight off: %s", result.error().c_str());
        display_revealed_ = false;
    }

protected:
    std::expected<void, std::string> on_init() override
    {
#if BROOKESIA_SYSTEM_SUPER_ENABLE_STARTUP_OVERLAY
        // Core mounts and processes the startup overlay before invoking on_init().
        // Reveal that valid frame while Super initializes fonts, themes and apps.
        auto result = reveal_display();
        if (!result) return result;
        ESP_LOGI("Ouse", "Startup overlay visible; preparing desktop");
#endif
        return system::super::System::on_init();
    }

private:
    double output_id_;
    bool display_revealed_ = false;
};

static void log_memory(const char *phase)
{
    constexpr uint32_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    ESP_LOGI("Ouse", "Memory %s: internal free=%u largest=%u; PSRAM free=%u",
        phase,
        static_cast<unsigned>(heap_caps_get_free_size(internal_caps)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(internal_caps)),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

static void allocation_failed(size_t size, uint32_t caps, const char *function)
{
    // Do not allocate or use libc logging locks while reporting an allocation failure.
    esp_rom_printf("Ouse ALLOC FAILED: function=%s size=%u caps=0x%08lx\n",
        function, static_cast<unsigned>(size), static_cast<unsigned long>(caps));
}

#include "pocket_platform.hpp"
static void initialize_system()
{
    // Platform is started below after Core mounts LittleFS.
    log_memory("before services");
    auto &manager = service::ServiceManager::get_instance();
    if (!manager.init() || !manager.start()) {
        ESP_LOGE("Ouse", "ServiceManager startup failed");
        return;
    }

    // Keep the binding alive for the lifetime of the system.
    static auto display_binding = manager.bind(DisplayHelper::get_name().data());
    if (!display_binding.is_valid()) {
        ESP_LOGE("Ouse", "Display service unavailable");
        return;
    }
    auto &source = gui::lvgl::DisplaySource::get_instance();
    if (!source.start({})) {
        ESP_LOGE("Ouse", "LVGL display source startup failed");
        return;
    }
    auto active = DisplayHelper::call_function_sync(
        DisplayHelper::FunctionId::SetActiveSourceRole,
        std::string(), std::string(gui::lvgl::DISPLAY_SOURCE_ROLE),
        service::helper::Timeout(1000));
    if (!active) {
        ESP_LOGE("Ouse", "Activate source: %s", active.error().c_str());
        return;
    }

    auto outputs_json = DisplayHelper::call_function_sync<boost::json::array>(
        DisplayHelper::FunctionId::GetOutputs, service::helper::Timeout(1000));
    std::vector<DisplayHelper::OutputInfo> outputs;
    if (!outputs_json || !BROOKESIA_DESCRIBE_FROM_JSON(
            boost::json::value(outputs_json.value()), outputs) || outputs.empty()) {
        ESP_LOGE("Ouse", "No display output");
        return;
    }
    const double output_id = static_cast<double>(outputs.front().id);
    auto backlight = DisplayHelper::call_function_sync(
        DisplayHelper::FunctionId::SetBacklightOnOff, output_id, false,
        service::helper::Timeout(1000));
    if (!backlight) {
        ESP_LOGE("Ouse", "Backlight setup: %s", backlight.error().c_str());
        return;
    }
    auto gesture = DisplayHelper::TouchGestureConfig{};
    gesture.enabled = true;
    auto gesture_result = DisplayHelper::call_function_sync(
        DisplayHelper::FunctionId::SetTouchGestureConfig, output_id,
        BROOKESIA_DESCRIBE_TO_JSON(gesture).as_object(), service::helper::Timeout(1000));
    if (!gesture_result) {
        ESP_LOGE("Ouse", "Gesture setup: %s", gesture_result.error().c_str());
        return;
    }

    log_memory("before system init");
    static auto shell = std::make_unique<PocketSystem>(output_id);
    system::super::System::Config config;
    config.core_config.gui_backend = std::make_unique<gui::lvgl::Backend>();
    config.desktop_backend = static_cast<gui::lvgl::Backend *>(config.core_config.gui_backend.get());
    config.core_config.startup_overlay.min_present_ms = 600;
    config.core_config.storage.default_install_target = system::core::StorageInstallTarget::Internal;
    const int density_percent = ouse::load_density_percent();
    config.core_config.environment = {
        .width_px = 320, .height_px = 480,
        .density = static_cast<float>(density_percent) / 100.0F, .font_scale = 1.0F,
    };
    ESP_LOGI("Ouse", "UI scale: %d%% (restart applies saved preference)", density_percent);
    auto initialized = shell->init(std::move(config));
    if (!initialized) {
        shell->darken_display();
        ESP_LOGE("Ouse", "System init: %s", initialized.error().c_str());
        return;
    }
    log_memory("before system start");
    auto storage_preference = shell->set_default_install_storage(system::core::StorageInstallTarget::Internal);
    if (!storage_preference) ESP_LOGW("Ouse", "Internal install preference: %s", storage_preference.error().c_str());
    ouse::start_pocket_platform();
    auto started = shell->start();
    if (!started) {
        shell->darken_display();
        ESP_LOGE("Ouse", "System start: %s", started.error().c_str());
        return;
    }
    // Fallback for builds with startup overlays disabled.
    auto revealed = shell->reveal_display();
    if (!revealed) {
        ESP_LOGE("Ouse", "Backlight reveal: %s", revealed.error().c_str());
        return;
    }
    ESP_LOGI("Ouse", "READY: System Super + Settings + BPK installer");
}

static void setup(void *)
{
    // Return through initialize_system() so C++ locals are destroyed before task deletion.
    initialize_system();
    log_memory("after setup");
    vTaskDeleteWithCaps(nullptr);
}

extern "C" void app_main()
{
    // GUI/resource loading needs more stack than the default app_main task.
    if (heap_caps_register_failed_alloc_callback(allocation_failed) != ESP_OK) {
        ESP_LOGW("Ouse", "Allocation failure diagnostics could not be registered");
    }
    if (xTaskCreateWithCaps(setup, "ouse_setup", 48 * 1024, nullptr, 5, nullptr,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE("Ouse", "Cannot allocate setup task");
    }
}
