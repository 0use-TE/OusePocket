#pragma once
#include "private/types.hpp"
#include "private/recent_gesture.hpp"
#include <map>
namespace esp_brookesia::gui::lvgl {
class DesktopImpl {
public:
    explicit DesktopImpl(BackendImpl &backend): backend_(backend) {}
    ~DesktopImpl();
    std::expected<void, std::string> capture(uint32_t id, const std::vector<std::string> &paths, bool dark);
    void release(uint32_t id);
    void clear();
    std::expected<void, std::string> show(const std::vector<RecentTask> &apps, uint32_t focus,
        bool dark, bool chinese, uint64_t generation, bool ignore_release);
    void hide();
    void back_hint(int x, int y, int distance, int threshold, bool dark);
    void hide_back_hint();
    void navigation_edges(int side, int margin);
private:
    struct Snapshot {
        lv_draw_buf_t *buffer = nullptr;
        ~Snapshot() { if (buffer) lv_draw_buf_destroy(buffer); }
    };
    struct Card {
        DesktopImpl *desktop = nullptr;
        uint32_t id = 0;
        lv_obj_t *slot = nullptr;
        lv_obj_t *drag = nullptr;
        std::shared_ptr<Snapshot> snapshot;
        std::shared_ptr<BinaryImageSource> icon;
    };
    static void card_event(lv_event_t *event);
    static void button_event(lv_event_t *event);
    void emit(std::string_view operation, uint32_t id = 0);
    void focus(size_t index, bool animate);
    BackendImpl &backend_;
    std::map<uint32_t, std::shared_ptr<Snapshot>> snapshots_;
    std::vector<std::unique_ptr<Card>> cards_;
    BackendHandle event_handle_;
    lv_obj_t *body_ = nullptr;
    lv_obj_t *table_ = nullptr;
    lv_obj_t *hint_ = nullptr;
    lv_obj_t *hint_line_ = nullptr;
    lv_obj_t *left_edge_ = nullptr;
    lv_obj_t *right_edge_ = nullptr;
    lv_point_precise_t arrow_[3] {};
    lv_point_t press_ {};
    recent::Axis axis_ = recent::Axis::None;
    size_t focus_ = 0;
    int step_ = 5;
    int threshold_ = 30;
    uint64_t generation_ = 0;
    bool ignore_release_ = false;
};
}
