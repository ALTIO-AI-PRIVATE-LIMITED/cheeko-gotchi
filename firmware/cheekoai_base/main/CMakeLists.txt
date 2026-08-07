idf_component_register(
  SRCS
    "app_main.cc"
    "app_runtime.cc"
    "ble_provisioning.cc"
    "boot_launcher.cc"
    "ota_manager.cc"
    "pairing_code.cc"
    "permissions.cc"
    "platform.cc"
    "wifi_provisioning.cc"
    "services/audio_service.cc"
    "services/cloud_service.cc"
    "services/display_service.cc"
    "services/touch_service.cc"
  INCLUDE_DIRS "."
)

