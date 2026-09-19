#include "boot_launcher.h"

namespace cheekoai {

BootLauncher::BootLauncher(DisplayService& display, WifiProvisioning& wifi,
                           BleProvisioning& ble, OtaManager& ota,
                           PairingCode& pairing, AppRuntime& runtime)
    : display_(display),
      wifi_(wifi),
      ble_(ble),
      ota_(ota),
      pairing_(pairing),
      runtime_(runtime) {}

void BootLauncher::Start() {
  display_.Initialize();
  display_.ShowBootSplash("Cheekoai");

  switch (SelectBootMode()) {
    case BootMode::kSetup:
      display_.ShowPairingCode(pairing_.Generate());
      wifi_.StartAccessPointProvisioning();
      ble_.StartProvisioningAdvertisement();
      break;
    case BootMode::kUpdate:
      ota_.InstallPendingUpdate();
      break;
    case BootMode::kRecovery:
      display_.ShowRecovery("Recovery mode");
      break;
    case BootMode::kAppRuntime:
      runtime_.Start();
      break;
  }
}

BootMode BootLauncher::SelectBootMode() const {
  if (ota_.HasPendingUpdate()) {
    return BootMode::kUpdate;
  }
  if (!wifi_.HasProvisionedNetwork()) {
    return BootMode::kSetup;
  }
  if (ota_.LastBootFailed()) {
    return BootMode::kRecovery;
  }
  return BootMode::kAppRuntime;
}

}  // namespace cheekoai

