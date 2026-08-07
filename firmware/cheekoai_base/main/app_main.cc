#include "app_runtime.h"
#include "ble_provisioning.h"
#include "boot_launcher.h"
#include "ota_manager.h"
#include "pairing_code.h"
#include "permissions.h"
#include "platform.h"
#include "services/audio_service.h"
#include "services/cloud_service.h"
#include "services/display_service.h"
#include "services/touch_service.h"
#include "wifi_provisioning.h"

extern "C" void app_main() {
  // Services must outlive app_main: on ESP-IDF, background tasks and callbacks
  // keep referencing them after app_main returns, so everything is a
  // function-local static instead of a stack local.
  static cheekoai::DisplayService display;
  static cheekoai::TouchService touch;
  static cheekoai::AudioService audio;
  static cheekoai::CloudService cloud;
  static cheekoai::WifiProvisioning wifi;
  static cheekoai::BleProvisioning ble;
  static cheekoai::OtaManager ota;
  static cheekoai::PairingCode pairing;
  static cheekoai::PermissionStore permissions;
  static cheekoai::AppRuntime runtime(display, touch, audio, cloud, permissions);

  static cheekoai::BootLauncher launcher(display, wifi, ble, ota, pairing,
                                         runtime);
  launcher.Start();

  if (launcher.mode() == cheekoai::BootMode::kAppRuntime) {
    constexpr uint32_t kTickIntervalMs = 16;
    while (true) {
      runtime.Tick(cheekoai::PlatformMillis());
      cheekoai::PlatformDelayMs(kTickIntervalMs);
    }
  }
}
