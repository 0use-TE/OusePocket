// Product service: removable storage and read-only PMIC telemetry.
#include "pocket_platform.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <vector>
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_littlefs.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"
#include "nvs.h"
#include "brookesia/service_manager/service/manager.hpp"
#include "brookesia/service_manager/service/base.hpp"
namespace ouse {
namespace {
namespace fs = std::filesystem;
using namespace esp_brookesia::service;
std::mutex files_mutex, power_mutex;
sdmmc_card_t *card = nullptr;
sd_pwr_ctrl_handle_t sd_power = nullptr;
i2c_master_dev_handle_t pmic = nullptr;
boost::json::object battery{{"valid", false}};
std::atomic<int> timeout_seconds{60};
std::atomic<int64_t> last_touch{0};
std::atomic<bool> asleep{false};
std::atomic<uint8_t> screen_brightness{70};
std::atomic<bool> screen_enabled{false};
bool consume_touch = false;
std::atomic<bool> copying{false};
std::atomic<uint64_t> copy_bytes{0}, copy_total{0};
std::string copy_error;
boost::json::object fail(std::string error) { return {{"ok", false}, {"error", std::move(error)}}; }
std::string str(const boost::json::object &o, const char *key) {
    auto v = o.if_contains(key); return v && v->is_string() ? std::string(v->as_string()) : "";
}
bool read_reg(uint8_t address, uint8_t *data, size_t count = 1) {
    return pmic && i2c_master_transmit_receive(pmic, &address, 1, data, count, 100) == ESP_OK;
}
void sample_battery() {
    uint8_t s1 = 0, s2 = 0, percent = 255, volts[2]{};
    boost::json::object next{{"valid", false}};
    if (read_reg(0, &s1) && read_reg(1, &s2)) {
        const bool present = (s1 & 8) != 0;
        next = {{"valid", true}, {"present", present}, {"external", (s1 & 32) != 0},
            {"charging", present && (s2 & 7) < 4}, {"full", present && (s2 & 7) == 4}};
        if (present && read_reg(0xA4, &percent) && percent <= 100) next["percent"] = percent;
        // Preserve PMIC defaults: do not enable ADC or change supply/charger registers.
        if (present && read_reg(0x34, volts, 2)) {
            int mv = ((volts[0] & 63) << 8) | volts[1];
            if (mv >= 2500 && mv <= 4500) next["millivolts"] = mv;
        }
    }
    std::lock_guard lock(power_mutex); battery = std::move(next);
}
void unmount_sd() {
    if (card) esp_vfs_fat_sdcard_unmount("/sdcard", card);
    card = nullptr;
}
bool mount_sd() {
    if (card && sdmmc_get_status(card) == ESP_OK) return true;
    unmount_sd();
    if (!sd_power) {
        sd_pwr_ctrl_ldo_config_t config{.ldo_chan_id = 4};
        if (sd_pwr_ctrl_new_on_chip_ldo(&config, &sd_power) != ESP_OK) return false;
    }
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    host.pwr_ctrl_handle = sd_power;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.cd = SDMMC_SLOT_NO_CD;
    slot.wp = SDMMC_SLOT_NO_WP;
    esp_vfs_fat_sdmmc_mount_config_t config{};
    config.format_if_mount_failed = false;
    config.max_files = 5;
    config.allocation_unit_size = 16 * 1024;
    return esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &config, &card) == ESP_OK;
}
std::optional<fs::path> resolve(std::string path) {
    fs::path p(path);
    if (!p.is_absolute() || path.find('\\') != std::string::npos || path.find('\0') != std::string::npos) return {};
    for (const auto &part : p) if (part == ".." || part == ".") return {};
    p = p.lexically_normal();
    const auto s = p.string();
    if (s == "/littlefs/user" || s.starts_with("/littlefs/user/")) return p;
    if (s == "/sdcard" || s.starts_with("/sdcard/")) {
        if (!card || sdmmc_get_status(card) != ESP_OK) { unmount_sd(); return {}; }
        return p;
    }
    return {};
}
bool is_root(const fs::path &p) { return p == "/littlefs/user" || p == "/sdcard"; }
struct CopyJob { fs::path from, to; };
void copy_run(void *arg) {
    auto job = std::unique_ptr<CopyJob>(static_cast<CopyJob *>(arg));
    std::string error;
    {
        std::lock_guard lock(files_mutex);
        auto from = resolve(job->from.string()), to = resolve(job->to.string());
        FILE *input = from ? fopen(from->c_str(), "rb") : nullptr;
        FILE *output = input && to ? fopen(to->c_str(), "wb") : nullptr;
        if (!input || !output) error = "Cannot open copy source/destination";
        std::array<char, 8192> buffer{};
        while (input && output && error.empty()) {
            auto n = fread(buffer.data(), 1, buffer.size(), input);
            if (!n) { if (ferror(input)) error = "Read failed (SD removed?)"; break; }
            if (fwrite(buffer.data(), 1, n, output) != n) { error = "Write failed (full/removed?)"; break; }
            copy_bytes += n;
            vTaskDelay(1);
        }
        if (input) fclose(input);
        if (output && fclose(output) != 0) error = "Cannot flush copied file";
        if (!error.empty() && to) remove(to->c_str());
        copy_error = std::move(error);
    }
    copying = false;
}
void copy_worker(void *arg) {
    copy_run(arg); // Destroy C++ locals before deleting the FreeRTOS task.
    vTaskDelete(nullptr);
}
void monitor(void *) {
    for (;;) {
        sample_battery();
        if (files_mutex.try_lock()) {
            if (card && sdmmc_get_status(card) != ESP_OK) unmount_sd();
            files_mutex.unlock();
        }
        for (int i = 0; i < 50; ++i) {
            const auto timeout = timeout_seconds.load();
            if (timeout > 0 && screen_enabled && !asleep && esp_timer_get_time() - last_touch > timeout * 1000000LL) {
                asleep = true;
                bsp_display_backlight_off();
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}
class PocketService final : public ServiceBase {
public:
    PocketService() : ServiceBase({.name="Pocket", .description="OusePocket storage and power", .version="1.0.0",
        .scheduler_type=SchedulerType::Secondary}) {}
private:
    std::vector<FunctionSchema> get_function_schemas() override {
        return {{.name="Request", .parameters={{"Request", "Operation object", FunctionValueType::Object}},
            .default_timeout_ms=10000, .return_value=FunctionReturnSchema{FunctionValueType::Object}}};
    }
    FunctionHandlerMap get_function_handlers() override {
        return {[this](FunctionParameterMap &&args) -> FunctionResult {
            auto request = std::get_if<boost::json::object>(&args.at("Request"));
            if (!request) return {.success=false, .error_message="Request must be object"};
            return {.success=true, .data=pocket_request(*request)};
        }};
    }
};
}
boost::json::object pocket_power_state() {
    std::lock_guard lock(power_mutex);
    auto result = battery;
    result["timeout"] = timeout_seconds.load();
    result["asleep"] = asleep.load();
    return result;
}
bool set_screen_timeout(int seconds) {
    if (seconds != 0 && seconds != 30 && seconds != 60 && seconds != 120 && seconds != 300) return false;
    nvs_handle_t h;
    if (nvs_open("ouse.power", NVS_READWRITE, &h) != ESP_OK) return false;
    auto error = nvs_set_i32(h, "timeout", seconds);
    if (error == ESP_OK) error = nvs_commit(h);
    nvs_close(h);
    if (error != ESP_OK) return false;
    timeout_seconds = seconds; last_touch = esp_timer_get_time();
    return true;
}
bool pocket_touch(bool pressed) {
    if (pressed) {
        last_touch = esp_timer_get_time();
        if (asleep.exchange(false)) { consume_touch = true; bsp_display_brightness_set(screen_brightness); }
    }
    bool consumed = consume_touch;
    if (!pressed) consume_touch = false;
    return consumed;
}
bool pocket_backlight(uint8_t brightness, bool enabled) {
    screen_brightness = brightness; screen_enabled = enabled;
    return bsp_display_brightness_set(enabled && !asleep ? brightness : 0) == ESP_OK;
}
void start_pocket_platform() {
    static bool started = false;
    if (started) return;
    started = true;
    nvs_handle_t h;
    if (nvs_open("ouse.power", NVS_READONLY, &h) == ESP_OK) {
        int32_t t = 60;
        if (nvs_get_i32(h, "timeout", &t) == ESP_OK && (t == 0 || t == 30 || t == 60 || t == 120 || t == 300)) timeout_seconds = t;
        nvs_close(h);
    }
    bsp_i2c_init();
    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7; config.device_address = 0x34; config.scl_speed_hz = 100000;
    if (i2c_master_probe(bsp_i2c_get_handle(), 0x34, 100) == ESP_OK)
        i2c_master_bus_add_device(bsp_i2c_get_handle(), &config, &pmic);
    sample_battery();
    last_touch = esp_timer_get_time();
    std::error_code ec; fs::create_directories("/littlefs/user", ec);
    esp_brookesia::service::ServiceManager::get_instance().add_service(std::make_shared<PocketService>());
    if (xTaskCreate(monitor, "pocket_power", 4096, nullptr, 3, nullptr) != pdPASS)
        timeout_seconds = 0; // No monitoring task: leave screen on.
}
boost::json::object pocket_request(const boost::json::object &r) {
    const auto op = str(r, "Op");
    if (op == "Power") return pocket_power_state();
    if (op == "CopyStatus") {
        boost::json::object s{{"busy", copying.load()}, {"bytes", copy_bytes.load()}, {"total", copy_total.load()}};
        if (!copying) { std::lock_guard lock(files_mutex); s["error"] = copy_error; }
        return s;
    }
    if (copying) return fail("Copy in progress");
    std::lock_guard lock(files_mutex);
    if (op == "Volumes") {
        size_t total = 0, used = 0; esp_littlefs_info("littlefs_data", &total, &used);
        boost::json::object result{{"ok", true}, {"internalTotal", total}, {"internalUsed", used}, {"sd", card != nullptr}};
        uint64_t sd_total = 0, sd_free = 0;
        if (card && esp_vfs_fat_info("/sdcard", &sd_total, &sd_free) == ESP_OK) {
            result["sdTotal"] = sd_total; result["sdFree"] = sd_free;
        }
        return result;
    }
    if (op == "Mount") return {{"ok", mount_sd()}, {"sd", card != nullptr}};
    if (op == "Eject") { unmount_sd(); return {{"ok", true}}; }
    auto path = resolve(str(r, "Path"));
    if (!path) return fail("Path unavailable (SD missing or protected directory)");
    std::error_code ec;
    if (op == "List") {
        std::vector<boost::json::object> rows;
        for (fs::directory_iterator it(*path, ec), end; !ec && it != end; it.increment(ec)) {
            bool dir = it->is_directory(ec); if (ec) break;
            uintmax_t size = dir ? 0 : it->file_size(ec); if (ec) break;
            rows.push_back({{"name", it->path().filename().string()}, {"directory", dir}, {"size", size}});
            if (rows.size() >= 512) break;
        }
        if (ec) return fail(ec.message());
        std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
            bool ad = a.at("directory").as_bool(), bd = b.at("directory").as_bool();
            return ad != bd ? ad > bd : a.at("name").as_string() < b.at("name").as_string();
        });
        boost::json::array entries; for (auto &row : rows) entries.push_back(std::move(row));
        return {{"ok", true}, {"entries", std::move(entries)}, {"limited", rows.size() == 512}};
    }
    if (op == "Mkdir") fs::create_directory(*path, ec);
    else if (op == "Remove") {
        if (is_root(*path)) return fail("Cannot delete volume root");
        fs::remove(*path, ec); // Empty directories only, never recursive deletion.
    } else if (op == "Rename" || op == "Copy") {
        auto to = resolve(str(r, "To"));
        if (!to || is_root(*path) || is_root(*to) || *path == *to) return fail("Invalid destination");
        if (fs::exists(*to, ec)) return fail("Destination already exists");
        if (op == "Rename") fs::rename(*path, *to, ec);
        else {
            if (!fs::is_regular_file(*path, ec)) return fail("Copy supports regular files only");
            copy_total = fs::file_size(*path, ec); if (ec) return fail(ec.message());
            copying = true; copy_bytes = 0; copy_error.clear();
            auto job = new CopyJob{*path, *to};
            if (xTaskCreate(copy_worker, "pocket_copy", 12288, job, 3, nullptr) != pdPASS) {
                delete job; copying = false; return fail("Cannot start copy worker");
            }
        }
    } else return fail("Unknown operation");
    return ec ? fail(ec.message()) : boost::json::object{{"ok", true}};
}
}
