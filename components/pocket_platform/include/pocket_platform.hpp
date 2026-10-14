#pragma once
#include "boost/json.hpp"
namespace ouse {
void start_pocket_platform();
boost::json::object pocket_request(const boost::json::object &request);
boost::json::object pocket_power_state();
bool set_screen_timeout(int seconds);
// Called for each physical sample. Consumes the whole wake-up touch sequence.
bool pocket_touch(bool pressed);
bool pocket_backlight(uint8_t brightness, bool enabled);
}
