// Sources: ESP-IDF heap_debug, FreeRTOS per-core idle runtime, and partition APIs.
#include "pocket_resources.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_clk_tree.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_image_format.h"
#include "esp_littlefs.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

namespace ouse {
namespace {
namespace json = boost::json;
constexpr int64_t SAMPLE_US = 1000000;
constexpr int64_t LARGEST_BLOCK_US = 5000000;
constexpr uint32_t INTERNAL_CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
constexpr uint32_t PSRAM_CAPS = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
std::mutex snapshot_mutex;
json::object cached_snapshot;
int64_t sampled_at_us = 0;
int64_t largest_at_us = 0;
std::array<size_t, 2> largest_blocks{};

const char *reset_reason_name(esp_reset_reason_t reason) {
    switch (reason) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    case ESP_RST_USB: return "usb";
    case ESP_RST_JTAG: return "jtag";
    case ESP_RST_EFUSE: return "efuse";
    case ESP_RST_PWR_GLITCH: return "power_glitch";
    case ESP_RST_CPU_LOCKUP: return "cpu_lockup";
    default: return "unknown";
    }
}

const json::object &system_info() {
    static const json::object info = [] {
        esp_chip_info_t chip{};
        esp_chip_info(&chip);
        const auto *app = esp_app_get_description();
        const auto reason = esp_reset_reason();
        return json::object{
            {"chipModel", chip.model == CHIP_ESP32P4 ? "ESP32-P4" : CONFIG_IDF_TARGET},
            {"chipRevision", std::to_string(chip.revision / 100) + "." + std::to_string(chip.revision % 100)},
            {"cores", chip.cores}, {"resetReason", reset_reason_name(reason)},
            {"resetReasonCode", static_cast<int>(reason)},
            {"firmwareVersion", app->version}, {"idfVersion", app->idf_ver},
        };
    }();
    return info;
}

json::object heap_info(uint32_t caps, size_t largest, int64_t age_ms) {
    // Free/low-water counters are cheap. Only the cached largest block walks TLSF.
    const size_t total = heap_caps_get_total_size(caps);
    const size_t free = std::min(total, heap_caps_get_free_size(caps));
    return {{"total", total}, {"free", free}, {"used", total - free},
            {"minimumFree", heap_caps_get_minimum_free_size(caps)},
            {"largestFree", largest}, {"largestAgeMs", age_ms}};
}

json::object cpu_info(int64_t now_us) {
    json::object result{{"frequencyMHz", nullptr}, {"loadPercent", nullptr},
                        {"valid", false}, {"intervalMs", 0}};
    uint32_t frequency_hz = 0;
    if (esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,
                                   &frequency_hz) == ESP_OK) {
        result["frequencyMHz"] = frequency_hz / 1000000.0;
    }
    json::array cores;
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS && CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER && !CONFIG_FREERTOS_SMP && INCLUDE_xTaskGetIdleTaskHandle
    static std::array<configRUN_TIME_COUNTER_TYPE, portNUM_PROCESSORS> previous_idle{};
    static int64_t previous_at_us = 0;
    const int64_t interval_us = now_us - previous_at_us;
    // Discard the old baseline after returning from background or a long stall.
    const bool valid = previous_at_us != 0 && interval_us >= SAMPLE_US && interval_us <= LARGEST_BLOCK_US;
    double total_load = 0;
    for (int core = 0; core < portNUM_PROCESSORS; ++core) {
        const auto idle = ulTaskGetIdleRunTimeCounterForCore(core);
        json::object entry{{"id", core}, {"loadPercent", nullptr}};
        if (valid) {
            const configRUN_TIME_COUNTER_TYPE idle_delta = idle - previous_idle[core];
            const double fraction = static_cast<double>(idle_delta) / static_cast<double>(interval_us);
            // Counters are updated at task switches: window edges can jitter slightly.
            const double load = std::clamp(100.0 * (1.0 - fraction), 0.0, 100.0);
            entry["loadPercent"] = load;
            total_load += load;
        }
        previous_idle[core] = idle;
        cores.push_back(std::move(entry));
    }
    previous_at_us = now_us;
    if (valid) {
        result["valid"] = true;
        result["intervalMs"] = interval_us / 1000;
        result["loadPercent"] = total_load / portNUM_PROCESSORS;
    }
#else
    for (int core = 0; core < portNUM_PROCESSORS; ++core)
        cores.push_back(json::object{{"id", core}, {"loadPercent", nullptr}});
    result["error"] = "CPU runtime statistics unavailable in this firmware configuration";
#endif
    result["cores"] = std::move(cores);
    return result;
}

const json::object &flash_layout() {
    // Parse image headers once, without rehashing/verifying the entire firmware.
    static const json::object layout = [] {
        uint32_t flash_size = 0;
        json::object flash{{"totalBytes", nullptr}, {"configuredBytes", nullptr}};
        if (esp_flash_get_physical_size(nullptr, &flash_size) == ESP_OK) flash["totalBytes"] = flash_size;
        if (esp_flash_get_size(nullptr, &flash_size) == ESP_OK) flash["configuredBytes"] = flash_size;
        json::array partitions;
        auto iterator = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
        while (iterator) {
            const auto *part = esp_partition_get(iterator);
            const char *type = part->type == ESP_PARTITION_TYPE_APP ? "app" :
                               part->type == ESP_PARTITION_TYPE_DATA ? "data" : "other";
            partitions.push_back(json::object{{"label", part->label}, {"type", type},
                                              {"offset", part->address}, {"size", part->size}});
            iterator = esp_partition_next(iterator); // End-of-list releases the iterator.
        }
        flash["partitions"] = std::move(partitions);
        const auto *app = esp_app_get_description();
        json::object firmware{{"projectName", app->project_name}, {"version", app->version},
                              {"idfVersion", app->idf_ver}, {"partition", ""},
                              {"partitionBytes", 0}, {"imageBytes", nullptr}};
        if (const auto *running = esp_ota_get_running_partition()) {
            firmware["partition"] = running->label;
            firmware["partitionBytes"] = running->size;
            const esp_partition_pos_t position{.offset = running->address, .size = running->size};
            esp_image_metadata_t metadata{};
            if (esp_image_get_metadata(&position, &metadata) == ESP_OK && metadata.image_len <= running->size)
                firmware["imageBytes"] = metadata.image_len;
        }
        return json::object{{"flash", std::move(flash)}, {"firmware", std::move(firmware)}};
    }();
    return layout;
}
}

boost::json::object resource_snapshot() {
    const int64_t started_us = esp_timer_get_time();
    std::lock_guard lock(snapshot_mutex);
    const int64_t now_us = esp_timer_get_time();
    if (sampled_at_us && now_us - sampled_at_us < SAMPLE_US) return cached_snapshot;
    if (!largest_at_us || now_us - largest_at_us >= LARGEST_BLOCK_US) {
        largest_blocks[0] = heap_caps_get_largest_free_block(INTERNAL_CAPS);
        largest_blocks[1] = heap_caps_get_largest_free_block(PSRAM_CAPS);
        largest_at_us = now_us;
    }
    const auto age_ms = (now_us - largest_at_us) / 1000;
    cached_snapshot = {{"ok", true}, {"schemaVersion", 1}, {"capturedAtMs", now_us / 1000},
        {"uptimeMs", now_us / 1000}, {"cpu", cpu_info(now_us)},
        {"memory", json::object{{"internal", heap_info(INTERNAL_CAPS, largest_blocks[0], age_ms)},
                               {"psram", heap_info(PSRAM_CAPS, largest_blocks[1], age_ms)}}},
        {"system", system_info()}};
    cached_snapshot["sampleDurationUs"] = esp_timer_get_time() - started_us;
    sampled_at_us = now_us;
    return cached_snapshot;
}

boost::json::object resource_storage(bool sd_mounted, bool sd_reading) {
    const int64_t started_us = esp_timer_get_time();
    json::object result = flash_layout();
    size_t total = 0, used = 0;
    const bool internal_ok = esp_littlefs_info("littlefs_data", &total, &used) == ESP_OK;
    if (!internal_ok) total = used = 0;
    used = std::min(used, total);
    result["internal"] = json::object{{"available", internal_ok}, {"total", total},
                                       {"used", used}, {"free", total - used}};
    uint64_t sd_total = 0, sd_free = 0;
    // Read the currently mounted volume; never mount/format a card for monitoring.
    // f_getfree may scan FAT on its first call; defer while audio owns an SD file.
    const bool sd_busy = sd_mounted && sd_reading;
    const bool sd_ok = sd_mounted && !sd_busy && esp_vfs_fat_info("/sdcard", &sd_total, &sd_free) == ESP_OK;
    if (!sd_ok) sd_total = sd_free = 0;
    sd_free = std::min(sd_free, sd_total);
    result["sd"] = json::object{{"mounted", sd_mounted}, {"available", sd_ok}, {"busy", sd_busy},
                                 {"total", sd_total}, {"used", sd_total - sd_free}, {"free", sd_free}};
    result["ok"] = true;
    result["schemaVersion"] = 1;
    result["capturedAtMs"] = esp_timer_get_time() / 1000;
    result["sampleDurationUs"] = esp_timer_get_time() - started_us;
    return result;
}
}
