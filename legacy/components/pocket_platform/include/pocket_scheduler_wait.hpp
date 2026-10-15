#pragma once
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace ouse {
// Scheduler idle polling needs a bounded delay, not a temporary pthread mutex
// and condition variable on every poll. The worker checks Boost interruption
// and io_context::stopped() again immediately after this delay returns.
inline void scheduler_poll_wait(uint32_t milliseconds)
{
    // Round upward and sleep at least one tick so a low tick rate never turns
    // this path into a priority-5/10 busy loop starving lower-priority tasks.
    const uint64_t ticks = (static_cast<uint64_t>(milliseconds) * configTICK_RATE_HZ + 999) / 1000;
    const TickType_t maximum = portMAX_DELAY - 1;
    vTaskDelay(static_cast<TickType_t>(ticks == 0 ? 1 : ticks > maximum ? maximum : ticks));
}
}
