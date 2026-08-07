#include "boot_launcher.h"

#include "platform.h"

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

  mode_ = SelectBootMode();
  switch (mode_) {
    case BootMode::kSetup: {
      // TODO(idf): Entropy must come from esp_random(); the boot clock is a
      // host-only placeholder with no security value.
      const uint32_t now_ms = PlatformMillis();
      const uint32_t entropy = now_ms * 2654435761u + 0x9e3779b9u;
      display_.ShowPairingCode(pairing_.Generate(entropy, now_ms / 1000));
      wifi_.StartAccessPointProvisioning();
      ble_.StartProvisioningAdvertisement();
      break;
    }
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

