#include "app_runtime.h"
#include "ble_provisioning.h"
#include "boot_launcher.h"
#include "ota_manager.h"
#include "pairing_code.h"
#include "permissions.h"
#include "services/audio_service.h"
#include "services/cloud_service.h"
#include "services/display_service.h"
#include "services/touch_service.h"
#include "wifi_provisioning.h"

extern "C" void app_main() {
  cheekoai::DisplayService display;
  cheekoai::TouchService touch;
  cheekoai::AudioService audio;
  cheekoai::CloudService cloud;
  cheekoai::WifiProvisioning wifi;
  cheekoai::BleProvisioning ble;
  cheekoai::OtaManager ota;
  cheekoai::PairingCode pairing;
  cheekoai::PermissionStore permissions;
  cheekoai::AppRuntime runtime(display, touch, audio, cloud, permissions);

  cheekoai::BootLauncher launcher(display, wifi, ble, ota, pairing, runtime);
  launcher.Start();
}

