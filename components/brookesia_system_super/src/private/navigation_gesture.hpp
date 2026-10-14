#pragma once
#include <algorithm>
#include <cmath>

namespace esp_brookesia::system::super::navigation {
inline int side_width(float density) { return std::clamp(static_cast<int>(32 * density), 20, 40); }
inline bool side_origin(int x, int y, int width, int height, float density)
{
    const int vertical_margin = std::max(32, static_cast<int>(32 * density));
    return y > vertical_margin && y < height - vertical_margin &&
        (x <= side_width(density) || x >= width - side_width(density));
}
inline bool back_swipe(int x, int y, int stop_x, int stop_y, int width, int height, float density)
{
    if (!side_origin(x, y, width, height, density)) return false;
    const int dx = stop_x - x, dy = stop_y - y;
    const int distance = std::max(40, static_cast<int>(48 * density));
    return std::abs(dy) <= std::abs(dx) &&
        ((x <= side_width(density) && dx >= distance) ||
         (x >= width - side_width(density) && dx <= -distance));
}
inline int home_distance(int height) { return std::clamp(height / 8, 48, 96); }
}
