// OusePocket learning adapter: consumes the published Waveshare BSP;
// Brookesia owns the LVGL adapter, so do NOT call bsp_display_start().
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "brookesia/hal_interface.hpp"
#include "brookesia/hal_interface/device.hpp"
#include "brookesia/hal_interface/interfaces/display/panel.hpp"
#include "brookesia/hal_interface/interfaces/display/touch.hpp"
#include "brookesia/hal_interface/interfaces/display/backlight.hpp"

using namespace esp_brookesia;
using namespace esp_brookesia::hal;
namespace {
constexpr const char *GROUP = "waveshare_lcd35";
esp_lcd_panel_handle_t panel = nullptr;
esp_lcd_panel_io_handle_t io = nullptr;
esp_lcd_touch_handle_t touch = nullptr;

class Panel final : public display::PanelIface {
public:
    Panel() : PanelIface({.h_res = 320, .v_res = 480,
          .pixel_format = PixelFormat::RGB565, .group_id = GROUP}) {
        done_ = xSemaphoreCreateBinary();
        draw_lock_ = xSemaphoreCreateMutex();
    }
    ~Panel() override {
        if (done_) vSemaphoreDelete(done_);
        if (draw_lock_) vSemaphoreDelete(draw_lock_);
    }
    bool attach() {
        if (!done_ || !draw_lock_) return false;
        esp_lcd_panel_io_callbacks_t callbacks{};
        callbacks.on_color_trans_done = transfer_done;
        return esp_lcd_panel_io_register_event_callbacks(io, &callbacks, this) == ESP_OK;
    }
    bool draw_bitmap(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2,
                     const uint8_t *data) override {
        return draw_bitmap_sync(x1, y1, x2, y2, data, 0);
    }
    bool draw_bitmap_sync(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2,
                         const uint8_t *data, uint32_t timeout_ms) override {
        if (!data || x2 <= x1 || y2 <= y1 || x2 > 320 || y2 > 480) return false;
        xSemaphoreTake(draw_lock_, portMAX_DELAY);
        // Drain the previous transfer before submitting another borrowed buffer.
        if (pending_) xSemaphoreTake(done_, portMAX_DELAY);
        pending_ = false;
        while (xSemaphoreTake(done_, 0) == pdTRUE) {}
        const bool submitted = esp_lcd_panel_draw_bitmap(panel, x1, y1, x2, y2, data) == ESP_OK;
        bool completed = submitted;
        if (submitted) {
            pending_ = true;
            if (timeout_ms > 0) {
                completed = xSemaphoreTake(done_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
                // Keep the caller's buffer safe even when the wait budget expires.
                if (!completed) xSemaphoreTake(done_, portMAX_DELAY);
                pending_ = false;
            }
        }
        xSemaphoreGive(draw_lock_);
        return completed;
    }
    bool get_driver_specific(DriverSpecific &out) override {
        out.io_handle = io;
        out.panel_handle = panel;
        out.bus_type = BusType::Generic;
        out.event_dispatcher = {this, add_listener, remove_listener};
        return true;
    }
private:
    struct Listener { EventCallback callback = nullptr; void *ctx = nullptr; uint32_t id = 0; };
    std::array<Listener, 4> listeners_{};
    portMUX_TYPE listener_lock_ = portMUX_INITIALIZER_UNLOCKED;
    uint32_t next_id_ = 1;
    SemaphoreHandle_t done_ = nullptr;
    SemaphoreHandle_t draw_lock_ = nullptr;
    bool pending_ = false; // Protected by draw_lock_.

    static bool add_listener(void *ctx, EventCallback callback, void *user, uint32_t *id) {
        auto &self = *static_cast<Panel *>(ctx);
        if (!callback || !id) return false;
        bool added = false;
        portENTER_CRITICAL(&self.listener_lock_);
        for (auto &slot : self.listeners_) {
            if (!slot.callback) {
                slot = {callback, user, self.next_id_++};
                *id = slot.id;
                added = true;
                break;
            }
        }
        portEXIT_CRITICAL(&self.listener_lock_);
        return added;
    }
    static bool remove_listener(void *ctx, uint32_t id) {
        auto &self = *static_cast<Panel *>(ctx);
        portENTER_CRITICAL(&self.listener_lock_);
        for (auto &slot : self.listeners_) if (slot.id == id) slot = {};
        portEXIT_CRITICAL(&self.listener_lock_);
        return true;
    }
    static bool transfer_done(esp_lcd_panel_io_handle_t,
                              esp_lcd_panel_io_event_data_t *, void *ctx) {
        auto &self = *static_cast<Panel *>(ctx);
        BaseType_t wake = pdFALSE;
        xSemaphoreGiveFromISR(self.done_, &wake);
        std::array<Listener, 4> snapshot;
        portENTER_CRITICAL_ISR(&self.listener_lock_);
        snapshot = self.listeners_;
        portEXIT_CRITICAL_ISR(&self.listener_lock_);
        bool yield = wake == pdTRUE;
        for (const auto &slot : snapshot) {
            if (slot.callback) yield = slot.callback(
                static_cast<uint8_t>(EventType::ColorTransDone), true, slot.ctx) || yield;
        }
        return yield;
    }
};

class Touch final : public display::TouchIface {
public:
    Touch() : TouchIface({.x_max = 320, .y_max = 480, .max_points = 1,
          .operation_mode = OperationMode::Polling, .group_id = GROUP}) {}
    bool read_points(std::vector<Point> &points) override {
        points.clear();
        if (esp_lcd_touch_read_data(touch) != ESP_OK) return false;
        uint16_t x = 0, y = 0, strength = 0;
        uint8_t count = 0;
        if (esp_lcd_touch_get_coordinates(touch, &x, &y, &strength, &count, 1) && count) {
            points.push_back({static_cast<int16_t>(x), static_cast<int16_t>(y), strength, 0});
        }
        return true;
    }
    bool register_interrupt_handler(InterruptHandler handler, void *) override {
        return handler == nullptr; // This learning adapter uses polling.
    }
};

class Backlight final : public display::BacklightIface {
public:
    Backlight() : BacklightIface({.group_id = GROUP}) {}
    bool set_brightness(uint8_t percent) override {
        const uint8_t value = std::clamp<uint8_t>(percent, 10, 100);
        if (bsp_display_brightness_set(on_.load() ? value : 0) != ESP_OK) return false;
        brightness_ = value;
        return true;
    }
    bool get_brightness(uint8_t &percent) override { percent = brightness_.load(); return true; }
    bool is_light_on_off_supported() override { return true; }
    bool set_light_on_off(bool on) override {
        if (bsp_display_brightness_set(on ? brightness_.load() : 0) != ESP_OK) return false;
        on_ = on;
        return true;
    }
    bool is_light_on() const override { return on_.load(); }
private:
    std::atomic<uint8_t> brightness_{70};
    std::atomic<bool> on_{true};
};

class WaveshareDisplay final : public Device {
public:
    WaveshareDisplay() : Device("OuseWaveshareDisplay") {}
    ~WaveshareDisplay() = default;
private:
    bool probe() override { return true; }
    // Keep physical handles for firmware lifetime; only one LVGL instance owns this board.
    bool deinit_on_zero_references() const override { return false; }
    std::vector<InterfaceSpec> get_interface_specs() const override {
        return {{display::PanelIface::NAME, "Ouse:Panel"},
                {display::TouchIface::NAME, "Ouse:Touch"},
                {display::BacklightIface::NAME, "Ouse:Backlight"}};
    }
    bool on_init() override {
        ESP_ERROR_CHECK(bsp_display_brightness_init());
        ESP_ERROR_CHECK(bsp_display_new(nullptr, &panel, &io));
        ESP_ERROR_CHECK(bsp_touch_new(nullptr, &touch));
        auto display_panel = std::make_shared<Panel>();
        if (!display_panel->attach()) return false;
        interfaces_.emplace("Ouse:Panel", display_panel);
        interfaces_.emplace("Ouse:Touch", std::make_shared<Touch>());
        interfaces_.emplace("Ouse:Backlight", std::make_shared<Backlight>());
        ESP_ERROR_CHECK(bsp_display_brightness_set(70));
        return true;
    }
    void on_deinit() override {
        // There is no runtime board shutdown/reinit in this minimal firmware.
        interfaces_.clear();
    }
};
}

// main is a static archive: CMake forces this symbol to retain the HAL registrar.
BROOKESIA_PLUGIN_REGISTER_WITH_SYMBOL(
    Device, WaveshareDisplay, std::string("OuseWaveshareDisplay"), ouse_display_plugin_symbol
);
