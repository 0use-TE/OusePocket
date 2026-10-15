#pragma once
#include "boost/json.hpp"
namespace ouse {
void initialize_log_capture();
void start_log_writer();
boost::json::object log_request(const boost::json::object &request);
}
