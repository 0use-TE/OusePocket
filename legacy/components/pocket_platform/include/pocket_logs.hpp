#pragma once
#include "boost/json.hpp"
namespace ouse {
void initialize_log_capture();
void set_console_output_suppressed(bool suppressed);
void start_log_writer();
boost::json::object log_request(const boost::json::object &request);
}
