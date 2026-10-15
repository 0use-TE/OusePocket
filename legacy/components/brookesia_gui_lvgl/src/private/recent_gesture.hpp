#pragma once
#include <algorithm>
#include <cmath>
namespace esp_brookesia::gui::lvgl::recent {
enum class Axis { None, Horizontal, Vertical };
enum class Action { Open, Close, Previous, Next, Rebound };
inline Axis choose_axis(int dx, int dy, int step) {
    if (std::max(std::abs(dx), std::abs(dy)) < step) return Axis::None;
    return std::abs(dy) > std::abs(dx) * 1.73F ? Axis::Vertical : Axis::Horizontal;
}
inline Action release(Axis axis, int dx, int dy, int threshold) {
    if (axis == Axis::None) return Action::Open;
    if (axis == Axis::Horizontal) {
        if (dx <= -threshold) return Action::Next;
        if (dx >= threshold) return Action::Previous;
        return Action::Rebound;
    }
    return dy <= -threshold ? Action::Close : Action::Rebound;
}
}
