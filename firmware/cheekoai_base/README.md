# Cheekoai Base Firmware

Cheekoai Base is the product firmware scaffold for turning the ESP32-S3 board
into a real app platform. It is intentionally compile-light: the runtime shape,
service boundaries, and lifecycle are present, while hardware-specific driver
adapters remain TODOs until the final board support package is selected.

This directory is separate from the public SDK examples. Product firmware owns
boot, provisioning, OTA, app orchestration, permissions, and hardware services.
Apps should depend on the stable SDK surface, not on these internal classes.

## Goals

- Boot into a deterministic launcher that can recover from setup, OTA, and app
  failures.
- Pair the device to a user or household account before enabling cloud features.
- Support Wi-Fi provisioning first, with BLE provisioning reserved behind a
  clear placeholder.
- Install signed OTA updates and keep enough state to roll back bad boots.
- Run Cheeko apps behind permission checks for audio, display, touch, network,
  storage, cloud, and OTA access.
- Keep display, touch, audio, and cloud integrations behind replaceable service
  interfaces.

## Layout

| Path | Purpose |
| --- | --- |
| `CMakeLists.txt` | ESP-IDF project entrypoint |
| `main/` | Firmware runtime source |
| `main/services/` | Hardware and network service boundaries |
| `docs/architecture.md` | Runtime architecture and state model |
| `docs/porting.md` | Where board-specific adapters should be connected |
| `test/README.md` | Hardware bring-up and verification checklist |

## Build Shape

When ESP-IDF is installed and hardware is connected:

```bash
cd firmware/cheekoai_base
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbmodemXXXX flash monitor
```

This scaffold also supports a local syntax pass without ESP-IDF:

```bash
g++ -std=c++17 -fsyntax-only main/*.cc main/services/*.cc -Imain
```

## Integration Contract

The product firmware should expose stable app/runtime concepts while treating
board support as an adapter layer:

- `BootLauncher` chooses setup, update, recovery, or app runtime mode.
- `PairingCode` creates short-lived human-readable pairing codes.
- `WifiProvisioning` stores and applies Wi-Fi credentials.
- `BleProvisioning` reserves the BLE transport boundary.
- `OtaManager` validates and installs signed update manifests.
- `AppRuntime` starts, ticks, and stops the active app under permissions.
- `DisplayService`, `TouchService`, `AudioService`, and `CloudService` hide
  hardware or backend details from the app host.

The TODOs in this tree are intentionally at adapter edges: NVS, esp_netif,
esp_https_ota, NimBLE, display panel driver, touch controller, codec/I2S, and
the Cheeko cloud client.

