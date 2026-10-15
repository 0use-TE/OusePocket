#pragma once

namespace ouse {
// Called once after the desktop has started. Hosted initialization runs on a
// temporary internal-RAM task stack, outside GUI/service scheduler callbacks.
bool start_deferred_wireless();
}
