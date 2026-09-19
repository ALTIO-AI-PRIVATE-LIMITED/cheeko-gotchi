#include "services/touch_service.h"

namespace cheekoai {

void TouchService::Start() {
  // TODO(board): Initialize touch controller and interrupt line.
}

void TouchService::Poll() {
  // TODO(board): Read touch state and translate to app events.
}

void TouchService::Stop() {
  // TODO(board): Disable touch interrupts before deep sleep or recovery.
}

}  // namespace cheekoai

