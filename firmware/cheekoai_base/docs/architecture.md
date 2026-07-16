# Cheekoai Base Architecture

The base firmware is organized around a small boot launcher and replaceable
service boundaries. The first product milestone is not to hard-code every
driver. It is to make the product responsibilities explicit so hardware,
mobile, cloud, and app runtime work can land independently.

## Boot State Model

1. Initialize display enough to show status.
2. Check OTA rollback and pending update state.
3. Check whether Wi-Fi or account pairing is complete.
4. Enter one of four modes:
   - `Setup`: show pairing code, start Wi-Fi AP provisioning, advertise BLE.
   - `Update`: validate and install a pending signed OTA image.
   - `Recovery`: show recovery screen after failed boot or invalid state.
   - `AppRuntime`: launch the active app host.

## Runtime Boundaries

| Boundary | Owned by firmware | Adapter TODO |
| --- | --- | --- |
| Display | Splash, pairing, launcher, recovery surfaces | LCD panel driver, frame buffer, LVGL or lightweight renderer |
| Touch | Polling/interrupt ownership, app event translation | Touch IC driver and calibration |
| Audio | Mic/speaker lifecycle, routing policy | Codec, I2S, DMA, amp mute control |
| Wi-Fi | Provisioned network state and connection policy | `esp_wifi`, `esp_netif`, NVS persistence |
| BLE | Provisioning transport placeholder | NimBLE or ESP BLE provisioning |
| Cloud | Authenticated session and message pump | TLS, MQTT/WebSocket, Cheeko cloud protocol |
| OTA | Manifest validation and rollback policy | Signature verification, `esp_https_ota`, partition table |
| Permissions | App capability checks | Persistent grants and UX prompts |

## Permission Model

Apps should request capabilities before accessing sensitive services:

- `Display`
- `Touch`
- `Microphone`
- `Speaker`
- `Network`
- `Cloud`
- `Storage`
- `Ota`

The current scaffold grants only launcher display/touch permissions. Cloud
connection intentionally fails until the launcher or pairing flow receives a
cloud grant.

## Product Readiness Path

1. Bind board drivers behind `services/*`.
2. Persist Wi-Fi, pairing, OTA, and permission state in NVS.
3. Replace placeholder pairing codes with signed cloud pairing challenges.
4. Add an OTA partition table and signed manifest verification.
5. Add app package loading, app lifecycle callbacks, and crash isolation.
6. Add production logging, health counters, and manufacturing self-test hooks.

