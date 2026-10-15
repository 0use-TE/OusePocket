#pragma once
#include "boost/json.hpp"

namespace ouse {
// Read-only telemetry, sampled only on request. No extra task or timer.
boost::json::object resource_snapshot();
// Caller holds the platform's files_mutex so SD eject/copy cannot race this query.
boost::json::object resource_storage(bool sd_mounted, bool sd_reading);
}
