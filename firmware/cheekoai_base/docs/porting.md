# Porting Notes

Use this checklist when connecting the scaffold to real ESP32-S3 hardware.

## Board Support

- Confirm the exact ESP32-S3 module, flash size, PSRAM, LCD panel, touch
  controller, codec, microphone, speaker amp, buttons, battery gauge, and
  charger.
- Keep pin assignments in board metadata or a board support component.
- Avoid exposing board-specific pins to apps.

## ESP-IDF Adapters

| File | Adapter work |
| --- | --- |
| `wifi_provisioning.cc` | Add NVS-backed credentials, softAP provisioning endpoint, STA connect/retry |
| `ble_provisioning.cc` | Add BLE advertisement and provisioning characteristic |
| `ota_manager.cc` | Add manifest fetch, signature verification, HTTPS OTA, rollback marks |
| `services/display_service.cc` | Add LCD init, backlight, renderer, rotation, safe boot UI |
| `services/touch_service.cc` | Add touch driver, calibration, gesture/event translation |
| `services/audio_service.cc` | Add codec/I2S setup, mic capture, speaker playback |
| `services/cloud_service.cc` | Add device auth, cloud transport, app message routing |

## Security Defaults

- Pairing codes must be short-lived and bound to a device nonce.
- OTA manifests must be signed before any image URL is trusted.
- Cloud sessions must use device identity and TLS certificate validation.
- App permissions must default closed for mic, speaker, network, cloud, storage,
  and OTA.
- Recovery mode must avoid collecting microphone audio or cloud data unless the
  user explicitly starts diagnostics.

