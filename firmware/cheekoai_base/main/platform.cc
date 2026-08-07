#include "platform.h"

namespace cheekoai {

namespace {

// Host-stub monotonic clock. PlatformDelayMs advances it so host-side loops
// still observe forward-moving time without any OS dependency.
uint32_t g_stub_uptime_ms = 0;

}  // namespace

uint32_t PlatformMillis() {
  // TODO(idf): Return esp_timer_get_time() / 1000 once ESP-IDF is wired in.
  return g_stub_uptime_ms;
}

void PlatformDelayMs(uint32_t delay_ms) {
  // TODO(idf): Call vTaskDelay(pdMS_TO_TICKS(delay_ms)) once FreeRTOS is
  // wired in. The host stub only advances the stub clock.
  g_stub_uptime_ms += delay_ms;
}

}  // namespace cheekoai
