#pragma once

#include <atomic>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "brookesia/service_helper/system/storage.hpp"

namespace esp_brookesia::app::settings {

inline constexpr std::string_view WIFI_PREFERENCE_STORAGE_NAMESPACE = "app.settings";
inline constexpr std::string_view WIFI_PREFERENCE_STORAGE_KEY = "WifiEnabled";

// Product startup holds UI actions until the initial saved switch is applied.
// Default to ready for applications that do not defer wireless startup.
inline std::atomic<bool> wifi_preference_restore_pending{false};

inline void begin_wifi_preference_restore()
{
    wifi_preference_restore_pending.store(true, std::memory_order_release);
}

inline void finish_wifi_preference_restore()
{
    wifi_preference_restore_pending.store(false, std::memory_order_release);
}

inline bool is_wifi_preference_restore_pending()
{
    return wifi_preference_restore_pending.load(std::memory_order_acquire);
}

// Read the same saved switch for Settings and the deferred wireless startup.
// A missing value remains an error so each caller can explicitly default to off.
inline std::expected<bool, std::string> read_wifi_enabled_preference(uint32_t timeout_ms)
{
    using Storage = service::helper::Storage;
    auto nspace = Storage::make_kv_namespace({WIFI_PREFERENCE_STORAGE_NAMESPACE}, ".", timeout_ms);
    if (!nspace) {
        return std::unexpected(nspace.error());
    }
    auto key = Storage::make_kv_key({WIFI_PREFERENCE_STORAGE_KEY}, ".", timeout_ms);
    if (!key) {
        return std::unexpected(key.error());
    }
    return Storage::get_key_value<bool>(nspace->name, key->name, timeout_ms);
}

} // namespace esp_brookesia::app::settings
