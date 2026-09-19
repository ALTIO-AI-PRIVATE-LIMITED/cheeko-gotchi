# Cheeko Gotchi — Hardware Test Firmware

One firmware that checks every part of the Cheeko Gotchi in a single flash.
Use it to confirm a device works (new, or after you've erased or reflashed it),
and as a reference for how each part of the board is driven.

It talks to the hardware directly with ESP-IDF, so it runs today. It does not
use the app SDK (`sdk/include/cheeko.h`); `examples/self_test` is the same idea
written against the SDK and will run once the base firmware's hardware layer is
connected.

## What it tests

| Part | Chip | What you see |
| --- | --- | --- |
| Display | JD9853, 2.01" 240×296, SPI | Title, 7 colour swatches and live status rows |
| Touch | CST810 (I²C `0x15`) | 3-dot calibration on first boot, then a marker follows your finger; `TXY` shows coordinates |
| Buttons | Boot, Power, Volume −, Volume + | `BTN` row shows each button's state; every press plays a tone |
| Speaker | ES8311 (I²C `0x18`) + amplifier | Boot chirp, button tones, a short blip on touch |
| Microphones | ES7210 (I²C `0x40`) | `MIC` level bar moves when you speak or tap near the device |
| Motion | LIS2DH12 (I²C `0x19`) | `ACCEL` shows live X/Y/Z |
| Power | Charge detect, power-off latch | `POWER` row shows the charging state |
| I²C bus | – | Every device is scanned and named at boot (serial log) |

Everything is also printed to the USB serial console at 115200 baud.

## Flash the prebuilt image (no toolchain needed)

Download `cheeko-gotchi-hardware-test_merged.bin` from this repository's
[Releases](https://github.com/ALTIO-AI-PRIVATE-LIMITED/cheeko-gotchi/releases)
page, or build it yourself (see below). It is a single file containing the
bootloader, partition table and app. You only need
[esptool](https://docs.espressif.com/projects/esptool/) (`pip install esptool`):

```bash
esptool.py --chip esp32s3 -p PORT -b 460800 write_flash 0x0 cheeko-gotchi-hardware-test_merged.bin
```

- `PORT` is `/dev/cu.usbmodem…` on macOS, `/dev/ttyACM0` on Linux, `COM…` on Windows.
- **After flashing, unplug the USB cable and plug it back in.** The board stays
  in download mode after a reset from the computer, so the new firmware only
  starts after a power cycle.

## What you'll see

1. **First boot:** `TOUCH CALIBRATION` — tap the three dots as they appear.
   The result is saved. To calibrate again, flash the image again; that clears
   the saved calibration.
2. The title `CHEEKO GOTCHI  HW TEST` with colour swatches and live rows, and a
   boot chirp from the speaker.
3. **Touch** the screen: a marker follows your finger, a blip plays, `TXY` updates.
4. **Press each button:** the `BTN` row changes and a tone plays.
5. **Speak or tap** near the device: the `MIC` bar moves.
6. **Tilt** it: the `ACCEL` values change.
7. **Plug or unplug USB:** the `POWER` row changes.

## Build from source

Requires ESP-IDF v5.5. LVGL and the LVGL port are downloaded automatically by
the ESP-IDF component manager; `dependencies.lock` pins their versions.

```bash
cd firmware/hardware_test
idf.py set-target esp32s3        # first time only
idf.py build
idf.py -p PORT flash             # then unplug/replug USB
idf.py merge-bin                 # optional: single image at build/merged-binary.bin
```

For serial logs, power-cycle the board first and then run
`idf.py -p PORT monitor --no-reset` (Ctrl-] to exit).

## Board reference

| Item | Value |
| --- | --- |
| MCU | ESP32-S3 |
| Flash | 16 MB |
| PSRAM | 8 MB, octal (not used by this test) |
| USB | Native USB-Serial/JTAG (no separate USB-UART chip) |

### Pin map

| Function | GPIO | Function | GPIO |
| --- | --- | --- | --- |
| LCD SCLK | 9 | I²C SCL | 11 |
| LCD MOSI | 10 | I²C SDA | 12 |
| LCD CS | 14 | Touch IRQ | 1 |
| LCD DC | 8 | I²S MCLK | 5 |
| LCD reset | 17 | I²S BCLK | 15 |
| LCD backlight | 13 | I²S LRCK (WS) | 16 |
| Speaker amp enable | 4 | I²S DOUT → ES8311 (speaker) | 6 |
| Boot button | 0 | I²S DIN ← ES7210 (mics) | 7 |
| Power key | 3 | Volume − | 39 |
| Power-off latch | 2 | Volume + | 40 |
| Charge detect | 37 | | |

### I²C devices

| Address | Device |
| --- | --- |
| `0x15` | CST810 touch controller |
| `0x18` | ES8311 speaker codec |
| `0x19` | LIS2DH12 accelerometer |
| `0x40` | ES7210 microphone ADC |

### Things worth knowing

- **Two audio chips.** The ES8311 only plays sound; its microphone inputs are
  not connected. All recording goes through the ES7210: MIC1 and MIC2 are the two
  microphones, MIC3 is a loopback of the speaker for echo cancellation. Both
  chips share one I²S bus, with the ESP32-S3 as master.
- **Speaker amplifier (GPIO4).** Turn it on only while playing; an idle I²S line
  can otherwise make the speaker buzz.
- **Power key (GPIO3)** is active-high with an external pull-down. Don't enable
  an internal pull-up on it.
- **Power-off latch (GPIO2).** Driving it high switches the device off when it
  runs on battery. Keep it low.
- **Display.** The JD9853 uses `COLMOD 0x05` for RGB565. Orientation and colour
  order are set by `MADCTL` = `0x08` (BGR, no mirroring). If red and blue look
  swapped, flip the BGR bit; `0x80` flips vertically, `0x40` mirrors horizontally.
  The full panel init sequence is `JD9853_PANEL_INIT` in `main/cheeko_self_test.c`.
- **Download mode after flashing.** A reset from the computer leaves the board in
  download mode; power-cycle it to run your firmware.

## Licence

MIT, like the rest of this repository, except `components/esp_lcd_jd9853`, which
is Espressif's Apache-2.0 driver template (see the header in each file).
