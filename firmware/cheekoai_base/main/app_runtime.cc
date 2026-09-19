#include "app_runtime.h"

namespace cheekoai {

AppRuntime::AppRuntime(DisplayService& display, TouchService& touch,
                       AudioService& audio, CloudService& cloud,
                       PermissionStore& permissions)
    : display_(display),
      touch_(touch),
      audio_(audio),
      cloud_(cloud),
      permissions_(permissions) {}

void AppRuntime::Start() {
  running_ = true;
  permissions_.Grant(active_app_, Permission::kDisplay);
  permissions_.Grant(active_app_, Permission::kTouch);
  display_.ShowLauncher();
  touch_.Start();
  audio_.Initialize();
  cloud_.ConnectIfPermitted(permissions_, active_app_);
}

void AppRuntime::Tick(uint32_t uptime_ms) {
  if (!running_) {
    return;
  }
  touch_.Poll();
  cloud_.Pump();
  (void)uptime_ms;
}

void AppRuntime::Stop() {
  running_ = false;
  audio_.StopAll();
  touch_.Stop();
}

}  // namespace cheekoai

