#include "wireless_startup.hpp"
#include <atomic>
#include <memory>
#include <string>
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "brookesia/app_settings/wifi_preferences.hpp"
#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/service_helper/network/wifi.hpp"
#include "brookesia/service_manager/service/manager.hpp"
#include "brookesia/service_sntp.hpp"
#include "brookesia/service_wifi.hpp"

#if CONFIG_BROOKESIA_SERVICE_WIFI_ENABLE_AUTO_REGISTER || CONFIG_BROOKESIA_SERVICE_SNTP_ENABLE_AUTO_REGISTER
#error "Deferred wireless requires manual Wi-Fi/SNTP registration; run tools/enable-device-config.py"
#endif

namespace ouse {
namespace {
constexpr const char *TAG = "OuseWireless";
std::atomic<bool> startup_requested{false};

void initialize_wireless()
{
    struct PreferenceRestoreCompletion {
        ~PreferenceRestoreCompletion()
        {
            esp_brookesia::app::settings::finish_wifi_preference_restore();
        }
    } restore_completion;
    ESP_LOGI(TAG, "Desktop ready; initializing Hosted after scheduler startup");
    // Hosted 2.12.3 retains its own ESP_ERROR_CHECK/assert failure behavior.
    // Do not retry a partially initialized transport or deinit it blindly.
    const auto result = esp_hosted_init();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Hosted initialization failed: %s", esp_err_to_name(result));
        return;
    }

    auto &manager = esp_brookesia::service::ServiceManager::get_instance();
    using esp_brookesia::service::ServiceBase;
    using Wifi = esp_brookesia::service::wifi::Wifi;
    using WifiHelper = esp_brookesia::service::helper::Wifi;
    using SNTP = esp_brookesia::service::SNTP;
    // Match the official singleton registrar's non-owning shared_ptr policy.
    if (!manager.add_service(std::shared_ptr<ServiceBase>(&Wifi::get_instance(), [](ServiceBase *) {}))) {
        ESP_LOGE(TAG, "Wi-Fi service registration failed");
        return;
    }
    static auto wifi_binding = manager.bind(WifiHelper::get_name().data());
    if (!wifi_binding.is_valid()) {
        ESP_LOGE(TAG, "Wi-Fi service startup failed");
        return;
    }

    // Use the same stored switch as Settings. Binding alone leaves Wi-Fi idle;
    // Start restores the service's existing saved-AP auto-connect behavior.
    auto preferred = esp_brookesia::app::settings::read_wifi_enabled_preference(2000);
    if (!preferred) {
        ESP_LOGW(TAG, "Wi-Fi preference unavailable; keeping Wi-Fi off: %s", preferred.error().c_str());
    } else if (*preferred) {
        const auto action = std::string(BROOKESIA_DESCRIBE_TO_STR(WifiHelper::GeneralAction::Start));
        auto started = WifiHelper::call_function_sync(
            WifiHelper::FunctionId::TriggerGeneralAction, action,
            esp_brookesia::service::helper::Timeout(2000));
        if (!started) ESP_LOGW(TAG, "Wi-Fi start request failed: %s", started.error().c_str());
    }

    if (!manager.add_service(std::shared_ptr<ServiceBase>(&SNTP::get_instance(), [](ServiceBase *) {}))) {
        ESP_LOGW(TAG, "SNTP service registration failed");
        return;
    }
    // Binding starts SNTP's existing state machine and loads its persisted
    // timezone/servers. It waits for connectivity without blocking the desktop.
    static auto sntp_binding = manager.bind(SNTP::get_instance().get_attributes().name);
    if (!sntp_binding.is_valid()) {
        ESP_LOGW(TAG, "SNTP service startup failed");
        return;
    }
    ESP_LOGI(TAG, "Deferred Wi-Fi and SNTP services ready");
}

void wireless_task(void *)
{
    // Let C++ locals leave scope before deleting this temporary internal stack.
    initialize_wireless();
    vTaskDelete(nullptr);
}
}

bool start_deferred_wireless()
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) return false;
    bool expected = false;
    if (!startup_requested.compare_exchange_strong(expected, true)) return true;
    esp_brookesia::app::settings::begin_wifi_preference_restore();
    // The official constructor used an internal startup stack. Keep driver
    // initialization on internal RAM too; this stack is released on completion.
    if (xTaskCreate(wireless_task, "ouse_wireless", 16 * 1024, nullptr, 4, nullptr) != pdPASS) {
        esp_brookesia::app::settings::finish_wifi_preference_restore();
        startup_requested.store(false);
        ESP_LOGE(TAG, "Cannot create deferred wireless startup task");
        return false;
    }
    return true;
}
}
