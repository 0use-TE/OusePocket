// ES8311 output adapter for the published Waveshare BSP. No Board Manager profile.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "brookesia/hal_interface/device.hpp"
#include "brookesia/hal_interface/interfaces/audio/codec_player.hpp"
#include "brookesia/hal_interface/interfaces/audio/processor.hpp"

using namespace esp_brookesia;
namespace {
class Codec final : public hal::audio::CodecPlayerIface {
public:
    bool initialize() {
        handle_ = bsp_audio_codec_speaker_init();
        return handle_ != nullptr;
    }
    ~Codec() override {
        close();
        if (handle_) esp_codec_dev_delete(handle_);
    }
    bool open(const Config &config) override {
        std::lock_guard lock(mutex_);
        if (!handle_) return false;
        if (opened_) return true;
        esp_codec_dev_sample_info_t info{};
        info.bits_per_sample = config.bits;
        info.channel = config.channels;
        info.sample_rate = config.sample_rate;
        if (esp_codec_dev_open(handle_, &info) != ESP_CODEC_DEV_OK) return false;
        opened_ = true;
        if (esp_codec_dev_set_out_vol(handle_, volume_) != ESP_CODEC_DEV_OK) {
            esp_codec_dev_close(handle_);
            opened_ = false;
            return false;
        }
        return true;
    }
    void close() override {
        std::lock_guard lock(mutex_);
        if (handle_ && opened_) esp_codec_dev_close(handle_);
        opened_ = false;
    }
    bool set_volume(uint8_t value) override {
        std::lock_guard lock(mutex_);
        volume_ = std::min<uint8_t>(value, 100);
        return handle_ && (!opened_ ||
            esp_codec_dev_set_out_vol(handle_, volume_) == ESP_CODEC_DEV_OK);
    }
    bool write_data(const uint8_t *data, size_t size) override {
        std::lock_guard lock(mutex_);
        return opened_ && data && size <= 4096 &&
            esp_codec_dev_write(handle_, const_cast<uint8_t *>(data), static_cast<int>(size)) == ESP_CODEC_DEV_OK;
    }
    // The codec owns PA activation. AudioPlayback implements mute through volume=0.
    bool is_pa_on_off_supported() override { return false; }
    bool set_pa_on_off(bool) override { return false; }
    bool is_pa_on() const override { return false; }
private:
    esp_codec_dev_handle_t handle_ = nullptr;
    std::mutex mutex_;
    bool opened_ = false;
    uint8_t volume_ = 40;
};

// Real PCM output for the Settings test sound. Media decoding is a later feature.
class Playback final : public hal::audio::PlaybackIface {
public:
    explicit Playback(std::shared_ptr<Codec> codec) : codec_(std::move(codec)) {
        done_ = xSemaphoreCreateBinary();
    }
    ~Playback() override { close(); if (done_) vSemaphoreDelete(done_); }
    bool open(EventCallback callback) override {
        if (!done_ || !codec_->open({16, 1, 16000})) return false;
        callback_ = std::move(callback);
        opened_ = true;
        return true;
    }
    void close() override {
        stop();
        opened_ = false;
        callback_ = {};
        codec_->close();
    }
    bool play(const std::string &url) override {
        if (!opened_ || url != "tone://test") {
            ESP_LOGW("OuseAudio", "Only tone://test is supported by this output baseline");
            return false;
        }
        stop();
        cancelled_ = false;
        paused_ = false;
        running_ = true;
        if (callback_) callback_(hal::audio::PlayState::Playing);
        if (xTaskCreate(worker, "ouse_tone", 4096, this, 4, nullptr) != pdPASS) {
            running_ = false;
            if (callback_) callback_(hal::audio::PlayState::Idle);
            return false;
        }
        join_needed_ = true;
        return true;
    }
    bool pause() override {
        if (!running_) return false;
        paused_ = true;
        if (callback_) callback_(hal::audio::PlayState::Paused);
        return true;
    }
    bool resume() override {
        if (!running_) return false;
        paused_ = false;
        if (callback_) callback_(hal::audio::PlayState::Playing);
        return true;
    }
    bool stop() override {
        cancelled_ = true;
        if (join_needed_) {
            xSemaphoreTake(done_, portMAX_DELAY);
            join_needed_ = false;
        }
        return true;
    }
    bool is_opened() const override { return opened_; }
private:
    static void worker(void *arg) {
        auto self = static_cast<Playback *>(arg);
        std::array<int16_t, 256> samples{};
        constexpr size_t count = 16000 / 2; // 440 Hz, half a second, faded ends.
        for (size_t offset = 0; offset < count && !self->cancelled_; offset += samples.size()) {
            while (self->paused_ && !self->cancelled_) vTaskDelay(pdMS_TO_TICKS(10));
            if (self->cancelled_) break;
            const size_t length = std::min(samples.size(), count - offset);
            for (size_t i = 0; i < length; ++i) {
                const float position = static_cast<float>(offset + i);
                const float envelope = std::min({1.0F, position / 320.0F,
                    static_cast<float>(count - offset - i) / 320.0F});
                samples[i] = static_cast<int16_t>(4000.0F * envelope *
                    std::sin(6.2831853F * 440.0F * position / 16000.0F));
            }
            if (!self->codec_->write_data(reinterpret_cast<const uint8_t *>(samples.data()), length * 2)) {
                ESP_LOGE("OuseAudio", "PCM output failed");
                break;
            }
        }
        self->running_ = false;
        if (self->callback_) self->callback_(hal::audio::PlayState::Idle);
        xSemaphoreGive(self->done_);
        vTaskDelete(nullptr);
    }
    std::shared_ptr<Codec> codec_;
    SemaphoreHandle_t done_ = nullptr;
    EventCallback callback_;
    std::atomic<bool> opened_{false}, running_{false}, cancelled_{false}, paused_{false};
    bool join_needed_ = false;
};

class AudioDevice final : public hal::Device {
public:
    AudioDevice() : Device("OuseWaveshareAudio") {}
protected:
    bool probe() override { return true; }
    bool deinit_on_zero_references() const override { return false; }
    std::vector<hal::InterfaceSpec> get_interface_specs() const override {
        return {{hal::audio::CodecPlayerIface::NAME, "Ouse:CodecPlayer"},
                {hal::audio::PlaybackIface::NAME, "Audio:Playback"}};
    }
    bool on_init() override {
        auto codec = std::make_shared<Codec>();
        if (!codec->initialize()) return false;
        interfaces_["Ouse:CodecPlayer"] = codec;
        interfaces_["Audio:Playback"] = std::make_shared<Playback>(codec);
        ESP_LOGI("OuseAudio", "ES8311 output HAL initialized");
        return true;
    }
    void on_deinit() override { interfaces_.clear(); }
};
BROOKESIA_PLUGIN_REGISTER_WITH_SYMBOL(hal::Device, AudioDevice,
    std::string("OuseWaveshareAudio"), ouse_audio_plugin_symbol);
} // namespace
