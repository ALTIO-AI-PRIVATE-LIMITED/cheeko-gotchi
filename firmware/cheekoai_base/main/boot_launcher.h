#pragma once

#include "app_runtime.h"
#include "ble_provisioning.h"
#include "ota_manager.h"
#include "pairing_code.h"
#include "services/display_service.h"
#include "wifi_provisioning.h"

namespace cheekoai {

enum class BootMode {
  kSetup,
  kAppRuntime,
  kUpdate,
  kRecovery,
};

class BootLauncher {
 public:
  BootLauncher(DisplayService& display, WifiProvisioning& wifi,
               BleProvisioning& ble, OtaManager& ota, PairingCode& pairing,
               AppRuntime& runtime);

  void Start();
  BootMode SelectBootMode() const;

  // Mode chosen by the most recent Start(). Valid after Start() returns.
  BootMode mode() const { return mode_; }

 private:
  DisplayService& display_;
  WifiProvisioning& wifi_;
  BleProvisioning& ble_;
  OtaManager& ota_;
  PairingCode& pairing_;
  AppRuntime& runtime_;
  BootMode mode_ = BootMode::kSetup;
};

}  // namespace cheekoai

