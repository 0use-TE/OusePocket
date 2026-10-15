// ES8311 output adapter for the published Waveshare BSP. No Board Manager profile.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>
#include <cctype>
#include "esp_audio_simple_dec.h"
#include "esp_mp3_dec.h"
#include "impl/esp_wav_dec.h"
#include "pocket_media.hpp"
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
        if (config.bits!=16 || config.channels<1 || config.channels>2 ||
            config.sample_rate<8000 || config.sample_rate>48000) return false;
        if (opened_ && config.bits==config_.bits && config.channels==config_.channels &&
            config.sample_rate==config_.sample_rate) return true;
        if (opened_) {esp_codec_dev_close(handle_);opened_=false;}
        esp_codec_dev_sample_info_t info{};
        info.bits_per_sample = config.bits;
        info.channel = config.channels;
        info.sample_rate = config.sample_rate;
        if (esp_codec_dev_open(handle_, &info) != ESP_CODEC_DEV_OK) return false;
        opened_ = true;
        config_=config;
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
    Config config_{};
};

// Native decoding/output stays in firmware; the independently installed JS app
// supplies UI, playlist and controls. Only local WAV/MP3 and the test tone.
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
        if (!opened_) return false;
        std::string path=url;
        if(path.starts_with("file://")) path.erase(0,7);
        std::string suffix=path.substr(path.find_last_of('.')==std::string::npos?path.size():path.find_last_of('.'));
        std::transform(suffix.begin(),suffix.end(),suffix.begin(),[](unsigned char c){return std::tolower(c);});
        if(url!="tone://test" && suffix!=".mp3" && suffix!=".wav") return false;
        stop();
        std::unique_ptr<ouse::MediaReader> file;
        if(url!="tone://test") {
            file=ouse::open_media_file(path);
            if(!file) {ouse::set_media_state(path,"Idle","Cannot open music (SD missing or path protected)");return false;}
        }
        path_=path; file_=std::move(file); type_=suffix==".wav"?ESP_AUDIO_SIMPLE_DEC_TYPE_WAV:ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
        cancelled_ = false;
        paused_ = false;
        running_ = true;
        ouse::set_media_state(path_,"Playing");
        if (callback_) callback_(hal::audio::PlayState::Playing);
        if (xTaskCreate(worker, "ouse_audio", 8192, this, 4, nullptr) != pdPASS) {
            running_ = false;
            file_.reset();
            ouse::set_media_state(path_,"Idle","Cannot start audio worker");
            if (callback_) callback_(hal::audio::PlayState::Idle);
            return false;
        }
        join_needed_ = true;
        return true;
    }
    bool pause() override {
        if (!running_) return false;
        paused_ = true;
        ouse::set_media_state(path_,"Paused");
        if (callback_) callback_(hal::audio::PlayState::Paused);
        return true;
    }
    bool resume() override {
        if (!running_) return false;
        paused_ = false;
        ouse::set_media_state(path_,"Playing");
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
        self->worker_run(); // Close decoder/file and destroy C++ locals first.
        self->running_=false;
        if(self->callback_) self->callback_(hal::audio::PlayState::Idle);
        xSemaphoreGive(self->done_);
        vTaskDelete(nullptr);
    }
    bool wait_playing() {
        while(paused_ && !cancelled_) vTaskDelay(pdMS_TO_TICKS(10));
        return !cancelled_;
    }
    void worker_run() {
        std::string error;
        if(file_) error=decode_file(); else error=play_tone();
        file_.reset(); // The lease is released before publishing Idle.
        ouse::set_media_state(path_,"Idle",error);
        if(!error.empty()) ESP_LOGE("OuseAudio","Playback: %s",error.c_str());
    }
    std::string decode_file() {
        static bool registered=false;
        if(!registered) {
            if(esp_mp3_dec_register()!=ESP_AUDIO_ERR_OK || esp_wav_dec_register()!=ESP_AUDIO_ERR_OK)
                return "Cannot register WAV/MP3 decoder";
            registered=true;
        }
        esp_audio_simple_dec_cfg_t config{};config.dec_type=type_;
        esp_audio_simple_dec_handle_t decoder=nullptr;
        if(esp_audio_simple_dec_open(&config,&decoder)!=ESP_AUDIO_ERR_OK) return "Cannot open decoder";
        struct Guard {esp_audio_simple_dec_handle_t handle;~Guard(){esp_audio_simple_dec_close(handle);}} guard{decoder};
        std::vector<uint8_t> input(4096),output(8192);
        uint64_t bytes=0,microseconds=0;bool emitted=false;
        while(wait_playing()) {
            const auto count=file_->read(input.data(),input.size());
            if(file_->failed()) return "Read failed (SD removed?)";
            esp_audio_simple_dec_raw_t raw{};raw.buffer=input.data();raw.len=count;raw.eos=count<input.size();
            unsigned stalled=0;
            do {
                if(!wait_playing()) return {};
                esp_audio_simple_dec_out_t frame{};frame.buffer=output.data();frame.len=output.size();
                const auto result=esp_audio_simple_dec_process(decoder,&raw,&frame);
                if(result==ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                    if(frame.needed_size<=output.size() || frame.needed_size>64*1024) return "Invalid decoder buffer size";
                    output.resize(frame.needed_size);continue;
                }
                if(result!=ESP_AUDIO_ERR_OK && result!=ESP_AUDIO_ERR_DATA_LACK)
                    return "Invalid or unsupported audio file (use MP3 or 16-bit PCM WAV)";
                if(raw.consumed>raw.len || frame.decoded_size>output.size()) return "Invalid decoder frame bounds";
                if(frame.decoded_size) {
                    esp_audio_simple_dec_info_t info{};
                    if(esp_audio_simple_dec_get_info(decoder,&info)!=ESP_AUDIO_ERR_OK ||
                       !codec_->open({info.bits_per_sample,info.channel,info.sample_rate}))
                        return "Unsupported output format (16-bit, 8–48kHz, mono/stereo required)";
                    const unsigned frame_bytes=info.channel*2;
                    for(size_t offset=0;offset<frame.decoded_size;) {
                        if(!wait_playing()) return {};
                        const auto length=std::min<size_t>(4096,frame.decoded_size-offset);
                        if(length%frame_bytes || !codec_->write_data(output.data()+offset,length)) return "PCM output failed";
                        offset+=length;microseconds+=static_cast<uint64_t>(length)*1000000/(frame_bytes*info.sample_rate);
                    }
                    emitted=true;
                }
                bytes+=raw.consumed;raw.buffer+=raw.consumed;raw.len-=raw.consumed;
                ouse::set_media_progress(microseconds/1000,bytes);
                if(!raw.consumed && !frame.decoded_size) {
                    if(!raw.len) break;
                    return "Decoder made no progress";
                }
                if(!raw.consumed && ++stalled>16) return "Decoder stalled";
                if(raw.consumed) stalled=0;
            } while(raw.len);
            if(raw.eos) break;
            vTaskDelay(1);
        }
        return cancelled_ || emitted ? "" : "Audio file contains no playable samples";
    }
    std::string play_tone() {
        if(!codec_->open({16,1,16000})) return "Cannot configure test tone output";
        std::array<int16_t, 256> samples{};
        constexpr size_t count = 16000 / 2; // 440 Hz, half a second, faded ends.
        for (size_t offset = 0; offset < count && wait_playing(); offset += samples.size()) {
            const size_t length = std::min(samples.size(), count - offset);
            for (size_t i = 0; i < length; ++i) {
                const float position = static_cast<float>(offset + i);
                const float envelope = std::min({1.0F, position / 320.0F,
                    static_cast<float>(count - offset - i) / 320.0F});
                samples[i] = static_cast<int16_t>(4000.0F * envelope *
                    std::sin(6.2831853F * 440.0F * position / 16000.0F));
            }
            if (!codec_->write_data(reinterpret_cast<const uint8_t *>(samples.data()), length * 2)) {
                ESP_LOGE("OuseAudio", "PCM output failed");
                return "Test tone PCM output failed";
            }
        }
        return {};
    }
    std::shared_ptr<Codec> codec_;
    SemaphoreHandle_t done_ = nullptr;
    EventCallback callback_;
    std::atomic<bool> opened_{false}, running_{false}, cancelled_{false}, paused_{false};
    bool join_needed_ = false;
    std::string path_;
    std::unique_ptr<ouse::MediaReader> file_;
    esp_audio_simple_dec_type_t type_=ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
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
