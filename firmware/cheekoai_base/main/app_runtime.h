#pragma once

#include "permissions.h"
#include "services/audio_service.h"
#include "services/cloud_service.h"
#include "services/display_service.h"
#include "services/touch_service.h"

namespace cheekoai {

class AppRuntime {
 public:
  AppRuntime(DisplayService& display, TouchService& touch, AudioService& audio,
             CloudService& cloud, PermissionStore& permissions);

  void Start();
  void Tick(uint32_t uptime_ms);
  void Stop();

 private:
  DisplayService& display_;
  TouchService& touch_;
  AudioService& audio_;
  CloudService& cloud_;
  PermissionStore& permissions_;
  AppIdentity active_app_{"launcher", "cheekoai"};
  bool running_ = false;
};

}  // namespace cheekoai

