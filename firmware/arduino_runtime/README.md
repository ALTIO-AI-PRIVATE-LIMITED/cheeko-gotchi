# Cheeko Gotchi — Arduino Runtime

The on-device runtime for the Cheeko Gotchi SDK, targeting the
**OSTB_XIAOZHI_V1.2** ESP32-S3 handheld (240x296 ST7789 LCD, CST810 touch,
ES8311 speaker codec, LIS2DH12 accelerometer). It implements every class in
`sdk/include/cheeko.h` directly against the hardware, so that an app's
`app.cc` + this runtime + the SDK headers, compiled together as one Arduino
sketch, produce a flashable firmware image.

All hardware constants, init sequences, and calibration values are taken from
`SKILL.md` at the repo root — the verified bring-up guide for this board. Do
not change them without re-verifying on hardware.

## Files

| File | Role |
| --- | --- |
| `CheekoRuntime.ino.tpl` | Sketch template. `setup()` = power-pin safety, hardware init, `CreateCheekoApp()`, `OnStart()`. `loop()` = poll touch/buttons/motion, dispatch events, `OnTick()`, `delay(5)`. |
| `cheeko_runtime.cpp` | Implementation of all `cheeko.h` classes plus the `Cheeko()` singleton and the `cheeko_rt::{RuntimeInit, RuntimeAttachApp, RuntimePoll}` entry points the sketch calls. |
| `cheeko_hw.h` | Verified pin map, I2C addresses, panel geometry, bus speeds, audio constants — copied from SKILL.md — plus declarations of the low-level helpers. |
| `cheeko_hw.cpp` | Low-level drivers: LCD SPI command/data + ST7789 init, I2C register helpers, CST810 raw read + calibration mapping, ES8311 register init + I2S setup, square-wave synth, LIS2DH12 init/read. |
| `test/arduino_stubs.h` | Minimal no-op fakes of the Arduino/ESP-IDF APIs, for host-side syntax checking only. Not copied into sketches. |
| `test/syntax_check.sh` | Runs the host-side g++ syntax check (see below). |

## How a sketch gets assembled

An Arduino sketch is a folder whose name matches its `.ino` file; the builder
compiles every `.cpp`/`.c`/`.S` in that folder. A Cheeko app build therefore
looks like:

```
build/<app>/
├── <app>.ino            <- copy of CheekoRuntime.ino.tpl
├── cheeko.h             <- copy of sdk/include/cheeko.h
├── cheeko_font.h        <- copy of sdk/runtime/cheeko_font.h
├── cheeko_hw.h          <- copy from this directory
├── cheeko_hw.cpp        <- copy from this directory
├── cheeko_runtime.cpp   <- copy from this directory
└── app.cpp              <- copy of the app's src/app.cc (renamed, see note)
```

The app registers itself with `CHEEKO_APP(MyApp)`, which emits
`extern "C" cheeko::CheekoApp* CreateCheekoApp()`; the `.ino` declares and
calls that symbol, so exactly one app must be present per sketch.

> Note: `app.cc` is renamed to `app.cpp` because the Arduino builder does not
> reliably treat `.cc` as a source extension inside a sketch folder.

## Build / flash / monitor (from SKILL.md section 3)

```bash
arduino-cli core install esp32:esp32

SKETCH="build/<app>"                  # folder containing <app>.ino
FQBN="esp32:esp32:XIAO_ESP32S3:PSRAM=opi,PartitionScheme=tinyuf2_noota"
PORT=$(ls /dev/cu.usbmodem*)          # or COMx on Windows

arduino-cli compile --fqbn "$FQBN" "$SKETCH"
arduino-cli upload  --fqbn "$FQBN" -p "$PORT" "$SKETCH"
arduino-cli monitor -p "$PORT" -c baudrate=115200
```

The FQBN must be copied exactly: `PSRAM=opi` is mandatory (octal PSRAM;
without it the firmware boot-loops — SKILL.md Gotcha 1), the `XIAO_ESP32S3`
profile is the one verified stable for GPIO 39/40, and `tinyuf2_noota` gives
a 4MB app slot plus a 3.7MB FFat data partition.

## Hardware verification status

Everything here is **derived from the verified bring-up code in SKILL.md**
(pin map, ST7789 init with MADCTL `0x40`/INVOFF, CST810 calibration mapping,
ES8311 register sequence, I2S clocking, LIS2DH12 config, button polarities,
the GPIO 2 power-off latch). The runtime itself has **not yet been re-tested
on hardware** — it has only passed the host-side syntax check below. Expected
first-run risks: none known, but the touch calibration constants were measured
on a reference unit and may need per-unit recalibration (SKILL.md section 5).

## Behaviour notes and v1 limitations

- **Display** — no framebuffer; every primitive writes straight to the panel
  over 40MHz SPI. `Text`/`CenterText` render the shared 5x7 font at 2x scale
  (12px advance, 14px tall) in `Color::Ink`, with the glyph cell background
  taken from the last `Clear()` colour (there is nothing to composite
  against). Glyph cells that don't fit entirely on screen are skipped.
  `Image(path)` logs "not yet supported".
- **Audio** — playback only, at **16kHz** (the verified codec/I2S config).
  `Tone()` is a blocking square-wave synth with a decay envelope; the amp
  (GPIO 4) is enabled only while playing, with a ~40ms tail so the DMA
  drains before power-off. `SetVolume(0..100)` scales sample amplitude.
  `Play(path)` logs "not yet supported".
- **Microphone** — capture lives on the ES7210 ADC, which v1 does not drive.
  `mic().Start()/Stop()` log a warning and **`OnMicAudio` never fires**. (Note
  `AudioFrame` defaults to 24000Hz in `cheeko.h`; this device runs 16kHz — a
  future capture implementation will report the real rate in each frame.)
- **Touch** — polled every loop (~5ms) with the SKILL.md section 5 axis-swap
  calibration. `OnTouch` fires on press and release edges and on movement
  while pressed; `touch().Get()/IsPressed()` return the latest state.
- **Buttons** — volume up/down (GPIO 40/39) and Boot (GPIO 0) are active-LOW
  with pullups; the power key (GPIO 3) is active-HIGH with an external
  pulldown (Gotcha 4). Edge-triggered `OnButton` on press and release.
- **Motion** — readings in g, with X and Y negated per SKILL.md section 7 so
  +x tilts screen-right and +y screen-bottom. Shake = |magnitude − 1g| >
  0.6g, with a 400ms refractory period; dispatches `OnShake()` and latches
  for `IsShaken()`.
- **Storage** — NVS via the Preferences library, namespace `cheeko_app`.
  NVS keys must be **≤ 15 characters**.
- **Wifi** — `Connect()` reads `wifi_ssid`/`wifi_pass` from NVS namespace
  `cheeko_sys` and waits up to 8s for association.
- **Cloud** — `GetJson`/`PostJson` run a synchronous HTTP request and deliver
  the response body to the app's `OnCloudText`. `Connect()` is a logged
  no-op success. `StartVoiceSession`/`SendText` log a warning and dispatch a
  canned `OnCloudText("voice sessions require the Cheeko cloud service")` so
  apps never hang waiting for a reply.
- **Safety** — the very first statements in `setup()` drive GPIO 2 (the
  POWER_OFF latch) LOW; otherwise the board can power itself off mid-boot
  (Gotcha 8). `RuntimeInit()` repeats this defensively.

## Host-side syntax check

There is no Arduino toolchain on the dev box, so correctness against the C++
contract is checked with plain g++ and stub headers:

```bash
bash firmware/arduino_runtime/test/syntax_check.sh
```

which runs (from `firmware/arduino_runtime/`):

```bash
g++ -std=c++14 -fsyntax-only -DCHEEKO_SYNTAX_CHECK \
  -I ../../sdk/include -I ../../sdk/runtime -I ../../firmware/arduino_runtime \
  -include test/arduino_stubs.h \
  cheeko_runtime.cpp cheeko_hw.cpp
```

plus the same check on a `.cpp` copy of the sketch template and on
`examples/animated_face/src/app.cc`. Under `-DCHEEKO_SYNTAX_CHECK` the
runtime sources include `test/arduino_stubs.h` instead of the real
`<Arduino.h>`/`<SPI.h>`/`<Wire.h>`/`<Preferences.h>`/`<WiFi.h>`/
`<HTTPClient.h>`/`<driver/i2s.h>`. This proves the code is well-formed C++14
with the right signatures — it proves nothing about hardware behaviour.

## How the CLI will use this

The assembly contract for the Cheeko CLI:

1. Create `build/<app>/` and copy `CheekoRuntime.ino.tpl` to
   `build/<app>/<app>.ino` (folder name and .ino name must match).
2. Copy `cheeko_runtime.cpp`, `cheeko_hw.h`, `cheeko_hw.cpp` from
   `firmware/arduino_runtime/`, `cheeko.h` from `sdk/include/`, and
   `cheeko_font.h` from `sdk/runtime/` into the same folder.
3. Copy the app's `src/app.cc` into the folder as `app.cpp`.
4. Run `arduino-cli compile --fqbn
   "esp32:esp32:XIAO_ESP32S3:PSRAM=opi,PartitionScheme=tinyuf2_noota"
   build/<app>` (add `--export-binaries` to get distributable `.bin` files),
   then `arduino-cli upload` with the same FQBN and the detected port.

Nothing from `test/` is ever copied into a sketch; the
`#include "test/arduino_stubs.h"` lines in the sources are guarded by
`CHEEKO_SYNTAX_CHECK`, which the Arduino build never defines.
