#pragma once

#include <cstdint>

namespace cheekoai {

// Milliseconds elapsed since boot. Wraps after ~49.7 days.
uint32_t PlatformMillis();

// Blocks the calling task for approximately delay_ms milliseconds.
void PlatformDelayMs(uint32_t delay_ms);

}  // namespace cheekoai
