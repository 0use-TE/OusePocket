#pragma once
#include <cstdint>
#include "nvs.h"

namespace ouse {
inline constexpr const char *PREFERENCE_NAMESPACE = "ouse.device";
inline constexpr const char *DENSITY_KEY = "DensityPct";
inline bool valid_density(int value) { return value == 75 || value == 100 || value == 125; }

// HAL Storage initializes NVS before either the shell or Settings uses this header.
inline int load_density_percent()
{
    nvs_handle_t handle;
    if (nvs_open(PREFERENCE_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return 100;
    int32_t value = 100;
    auto result = nvs_get_i32(handle, DENSITY_KEY, &value);
    nvs_close(handle);
    return result == ESP_OK && valid_density(value) ? value : 100;
}

inline esp_err_t save_density_percent(int value)
{
    if (!valid_density(value)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    auto result = nvs_open(PREFERENCE_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    result = nvs_set_i32(handle, DENSITY_KEY, value);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result;
}
} // namespace ouse
