# Firmware Test Plan

These checks are meant for the first ESP32-S3 hardware bring-up. They are not a
replacement for full automated integration tests once board adapters exist.

## Host Syntax Check

From `firmware/cheekoai_base`:

```bash
g++ -std=c++17 -fsyntax-only main/*.cc main/services/*.cc -Imain
```

This verifies that the scaffold stays mechanically valid without requiring
ESP-IDF on the workstation.

## ESP-IDF Build Check

With ESP-IDF installed:

```bash
idf.py set-target esp32s3
idf.py build
```

Expected result today: project configuration and compilation should pass once
IDF is installed. Hardware behavior remains placeholder-only until adapters are
implemented.

## Hardware Smoke Test

1. Flash the app:

   ```bash
   idf.py -p /dev/tty.usbmodemXXXX flash monitor
   ```

2. Confirm boot mode selection:
   - Fresh device with no stored Wi-Fi should enter setup mode.
   - A pending OTA marker should enter update mode.
   - A failed previous boot marker should enter recovery mode.
   - Provisioned Wi-Fi and healthy boot should enter app runtime mode.

3. Connect each adapter as it lands:
   - Display: splash, pairing code, recovery screen, launcher screen.
   - Touch: tap coordinates and press/release events.
   - Audio: codec init, mic stream, speaker tone, amp mute.
   - Wi-Fi: softAP provisioning, STA connect, reconnect.
   - BLE: advertisement visible from mobile provisioning app.
   - Cloud: authenticated session, ping/pong, app message pump.
   - OTA: signed manifest accepted, invalid manifest rejected, rollback works.

4. Record failures with:
   - Board revision
   - ESP-IDF version
   - Flash/PSRAM size
   - Serial monitor log
   - Reproduction steps

