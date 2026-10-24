# Cheeko Gotchi — iPhone Agent Firmware

Based on [ESP32 iPhone Agent](https://github.com/onehumanbeing/esp32-iphone-agent)
by Henry Yuan. See [NOTICE](NOTICE).

Plug the Cheeko into an iPhone and it becomes the eyes and hands of an AI agent
that runs in the ESP32 Agent iOS app on the same phone:

```
iPhone ──Screen Mirroring (Wi-Fi)──▶ Cheeko: AirPlay receiver ──BLE──▶ ESP32 Agent app (same iPhone)
   ▲                                   own hotspot                         vision model → action
   └──────── USB HID mouse + keyboard ◀──────────── BLE commands ◀───────────────┘
```

This is the upstream board firmware (`hardware/hid_bridge`) running on the
Cheeko instead of a XIAO ESP32S3. The BLE protocol is unchanged, so the upstream
iOS app works as is. The Cheeko adds two things:

- **A status screen** showing the app link, USB, the hotspot name and password,
  screen mirroring and the last action. Typed text is never shown, only its
  length.
- **The middle button as a stop button.** It releases any held click or key and
  sends `app stop` to the iOS app, which ends the run.

Like `firmware/hardware_test`, it drives the board directly and does not use the
app SDK. [`examples/iphone_agent`](../../examples/iphone_agent/) is the same app
written against the SDK's experimental `phone()` service. It will run once the
base firmware is wired up.

## Build and flash

You need [arduino-cli](https://arduino.github.io/arduino-cli/), the ESP32
core 3.3.x and two libraries:

```bash
arduino-cli core install esp32:esp32@3.3.12 \
  --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install "NimBLE-Arduino@2.5.1" "Adafruit GFX Library"

firmware/iphone_agent/flash.sh            # optional: [serial port] [--debug] [--build-only]
```

`flash.sh` copies the upstream firmware from
[`third_party/esp32-iphone-agent`](../../third_party/esp32-iphone-agent/) into `build/`,
adds the files in `iphone_agent/` (`iphone_agent.ino` is upstream's
`hid_bridge.ino` with the Cheeko hooks added), builds it and flashes it. Always
build with `flash.sh`: it applies upstream's BLE buffer sizes (`nimble_cfg.h`).

If the screen stays dark after flashing, unplug the USB cable and plug it back in.

## Set up the phone

Build and install the iOS app from [`third_party/esp32-iphone-agent/ios`](../../third_party/esp32-iphone-agent/ios/)
on a Mac with Xcode ([Quick start, step 2](../../third_party/esp32-iphone-agent/README.md#2-install-the-app)).
Then plug the Cheeko into the iPhone (USB-C directly, or a Lightning to USB
Camera Adapter) and follow the app's setup. The Cheeko's screen shows each step
turning green:

| Row | Turns green when |
| --- | --- |
| `PHONE APP (BLE)` | the app has paired with the board ("Plug Me In") |
| `USB MOUSE + KEYBOARD` | the phone is unlocked and sees the board as a mouse and keyboard |
| `HOTSPOT` | the hotspot is up. If you join it by hand, the password is shown under it |
| `SCREEN MIRRORING` | Control Center → Screen Mirroring → *ESP32 Agent* is streaming |

While a run is going, `LAST ACTION` shows each pointer or keyboard command.

## License

The files in this directory are MIT, like the rest of this repository.
`iphone_agent/iphone_agent.ino` is adapted from upstream and keeps its MIT
notice (see [NOTICE](NOTICE)).

The upstream AirPlay receiver (`hardware/hid_bridge/src/airplay/`, ported from
UxPlay) is **GPL-3.0**; it is vendored under `third_party/` with its license file.
The firmware binary that `flash.sh` builds links it and is therefore GPL-3.0. If
you distribute that binary, you must also offer its complete source:
`third_party/esp32-iphone-agent` plus this directory.
