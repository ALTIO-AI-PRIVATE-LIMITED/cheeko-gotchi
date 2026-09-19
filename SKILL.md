---
name: cheeko-gotchi-esp32s3
description: Build firmware for the Cheeko Gotchi v1.2 ESP32-S3 handheld (240x296 ST7789 LCD, CST810 touch, ES8311 speaker, ES7210 mics, LIS2DH12 accelerometer). Use when writing, flashing, or debugging code for this board, or any ESP32-S3 device with an SPI ST7789-class panel plus an Espressif codec. Covers Arduino toolchain setup, verified pin map, display/touch/audio/tilt bring-up, JPEG rendering, a serial asset-upload protocol, and a long list of hardware gotchas that are expensive to rediscover.
---

# Cheeko Gotchi v1.2 — ESP32-S3 Handheld Firmware Guide

Everything in this document was verified on real hardware. Where a value was
determined empirically (display orientation, touch mapping, tilt signs), that
is stated explicitly, along with **how** it was determined, so you can redo the
measurement if your unit differs.

> **The single most important sentence in this document:** the touch panel is
> rotated 90° relative to the display, the display needs a non-default MADCTL,
> and the build fails at runtime without `PSRAM=opi`. If you only read three
> things, read [Gotcha 1](#gotcha-1-psramopi-is-mandatory),
> [Gotcha 3](#gotcha-3-the-touch-panel-is-rotated-90-vs-the-display), and
> [Gotcha 5](#gotcha-5-usb-cdc-silently-drops-bytes-on-large-writes).

---

## 1. What this hardware actually is

A small battery-powered handheld ("AI companion" toy form factor) built on an
ESP32-S3. This guide covers board revision v1.2.

| Block | Part | Details |
| --- | --- | --- |
| MCU | ESP32-S3R2 | Dual-core Xtensa LX7 @240MHz, Wi-Fi + BLE 5, 40MHz crystal |
| PSRAM | 8MB (reported by esptool as `Embedded PSRAM 8MB (AP_3v3)`) | **Octal (OPI)** — see Gotcha 1 |
| Flash | W25Q128 | Nominally 16MB; the units in hand enumerate and boot as **8MB** — verify yours with `esptool flash-id` |
| Display | ST7789-class SPI LCD | 2.01", **240x296** RGB565, FFC connector. Not a standard 240x240/240x320 panel |
| Touch | CST810 | Capacitive, I²C `0x15`, IRQ on GPIO 1 |
| Speaker out | ES8311 codec + NS4150B amp | Codec I²C `0x18`, 3W class-D into a 4Ω speaker, amp enable on GPIO 4 |
| Mic in | ES7210 4-ch ADC | I²C `0x40`. MIC1/MIC2 = analog MEMS mics, MIC3 = speaker loopback for AEC |
| Motion | LIS2DH12 accelerometer | I²C `0x19` on this board (see Gotcha 7) |
| Power | HM4057 charger + ME6217 LDO | One-key on/off circuit, `POWER_OFF` latch on GPIO 2 |
| USB | Type-C → ESP32-S3 native USB (USB-Serial/JTAG) | No external UART bridge chip |

### Dual-codec architecture (important)

This board uses the same split design as Espressif's ESP-BOX-3:

- **ES8311 handles output only.** Its ADC/mic inputs are *not connected* on the
  PCB. Any firmware that tries to record through the ES8311 will capture
  silence — this is a wiring fact, not a bug in your code.
- **ES7210 handles all capture.** MIC1 and MIC2 are the two physical MEMS mics;
  MIC3 is a loopback of the speaker signal intended for acoustic echo
  cancellation.

Both codecs sit on the same I²C bus and share the same I²S bus (the ESP32 is
I²S master; both codecs are slaves).

---

## 2. Verified pin map

These values are cross-checked against three independent working sketches and
confirmed by running code. [`firmware/hardware_test`](firmware/hardware_test/)
uses the same pin map.

```cpp
// ---- Display (SPI) ----
static constexpr int PIN_LCD_DC    = 8;   // data/command select
static constexpr int PIN_LCD_SCLK  = 9;
static constexpr int PIN_LCD_MOSI  = 10;
static constexpr int PIN_LCD_BL    = 13;  // backlight enable (HIGH = on)
static constexpr int PIN_LCD_CS    = 14;
static constexpr int PIN_LCD_RST   = 17;
// NOTE: there is no MISO. SPI.begin(SCLK, -1, MOSI, CS).

// ---- I2C bus (touch + both codecs + accelerometer) ----
static constexpr int PIN_I2C_SCL   = 11;
static constexpr int PIN_I2C_SDA   = 12;
static constexpr int PIN_TOUCH_IRQ = 1;   // CST810 interrupt (optional; polling works)

// ---- I2S audio ----
static constexpr int PIN_PA_CTRL   = 4;   // speaker amp enable (HIGH = on)
static constexpr int PIN_I2S_MCLK  = 5;
static constexpr int PIN_I2S_DOUT  = 6;   // ESP32 -> ES8311 (playback)
static constexpr int PIN_I2S_DIN   = 7;   // ES7210 -> ESP32 (capture)
static constexpr int PIN_I2S_BCLK  = 15;
static constexpr int PIN_I2S_LRCK  = 16;  // aka WS

// ---- Buttons / power ----
static constexpr int PIN_BOOT_BUTTON  = 0;   // active LOW (often not exposed on the case!)
static constexpr int PIN_POWER_OFF    = 2;   // OUTPUT: drive HIGH to cut power (see Gotcha 8)
static constexpr int PIN_POWER_KEY    = 3;   // middle button, active HIGH + external pulldown
static constexpr int PIN_CHARGE_DET   = 37;  // charge detect
static constexpr int PIN_VOLUME_DOWN  = 39;  // active LOW
static constexpr int PIN_VOLUME_UP    = 40;  // active LOW

// ---- I2C addresses ----
static constexpr uint8_t CST810_ADDR   = 0x15;  // touch
static constexpr uint8_t ES8311_ADDR   = 0x18;  // speaker codec
static constexpr uint8_t LIS2DH12_ADDR = 0x19;  // accelerometer
static constexpr uint8_t ES7210_ADDR   = 0x40;  // mic ADC
```

Bus speeds that work reliably: `Wire.begin(SDA, SCL, 400000)` (400kHz I²C) and
`SPI.setFrequency(40000000)` (40MHz SPI).

---

## 3. Toolchain setup (Arduino, not ESP-IDF)

ESP-IDF works too, but the Arduino path is dramatically faster to iterate with
on this board, and the code in this guide is written for Arduino. For a working
ESP-IDF reference, see [`firmware/hardware_test`](firmware/hardware_test/).

```bash
# 1. arduino-cli
brew install arduino-cli

# 2. ESP32 core (large download, ~1GB with toolchains)
arduino-cli core install esp32:esp32

# 3. Host-side tooling for flashing and asset upload
pip3 install --break-system-packages esptool pyserial

# 4. Optional, only if you render JPEGs on-device
arduino-cli lib install "TJpg_Decoder"

# 5. Optional, only if you use LVGL (see Gotcha 9 first!)
arduino-cli lib install "lvgl@9.3.0"
```

### The FQBN — copy this exactly

```
esp32:esp32:XIAO_ESP32S3:PSRAM=opi,PartitionScheme=tinyuf2_noota
```

Why each part matters:

- **`XIAO_ESP32S3`** — there is no board definition for this exact hardware.
  The Seeed XIAO ESP32-S3 profile is pin-compatible enough and, critically,
  *stable*. The generic `esp32:esp32:esp32s3` ("ESP32S3 Dev Module") profile
  was tried and produced flaky behaviour on GPIO 39/40 (those are JTAG-shared
  pins on the S3 and board variants differ in whether they're freed for plain
  GPIO). **Stay on the XIAO profile.**
- **`PSRAM=opi`** — mandatory, see Gotcha 1.
- **`PartitionScheme=tinyuf2_noota`** — gives a 4MB app slot and a **3.7MB FFat
  data partition** for assets. The default `default_8MB` scheme only leaves
  1.5MB of SPIFFS. Other useful options: `max_app_8MB` (7.9MB app, no
  filesystem) if you embed everything in the binary instead.

### Build / flash / monitor

```bash
SKETCH="path/to/your_sketch"          # folder containing your_sketch.ino
FQBN="esp32:esp32:XIAO_ESP32S3:PSRAM=opi,PartitionScheme=tinyuf2_noota"
PORT=$(ls /dev/cu.usbmodem*)

arduino-cli compile --fqbn "$FQBN" "$SKETCH"
arduino-cli upload  --fqbn "$FQBN" -p "$PORT" "$SKETCH"
arduino-cli monitor -p "$PORT" -c baudrate=115200
```

Export standalone binaries (for distributing to people without a toolchain):

```bash
arduino-cli compile --fqbn "$FQBN" --export-binaries "$SKETCH"
# -> $SKETCH/build/esp32.esp32.XIAO_ESP32S3/*.bin
```

Flash exported binaries directly (offsets come from the build's `flash_args`):

```bash
esptool.py --chip esp32s3 --port "$PORT" --baud 921600 write_flash \
  0x0      bootloader.bin \
  0x8000   partitions.bin \
  0xe000   boot_app0.bin \
  0x10000  app.bin \
  0x410000 tinyuf2.bin      # recovery bootloader — keep it, it saves bricked units
```

> Do **not** try to combine the build's `*.merged.bin` (which already spans the
> whole flash from 0x0) with a separate `tinyuf2.bin` at 0x410000 — esptool
> rejects it with an "overlap detected" error. Use either the merged image
> alone, or the four individual binaries plus tinyuf2 as shown above.

---

## 4. Display bring-up

### Init sequence (verified)

```cpp
static void lcdInit() {
  pinMode(PIN_LCD_CS, OUTPUT);
  pinMode(PIN_LCD_DC, OUTPUT);
  pinMode(PIN_LCD_RST, OUTPUT);
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_CS, HIGH);
  digitalWrite(PIN_LCD_BL, HIGH);      // backlight on

  digitalWrite(PIN_LCD_RST, LOW);  delay(20);
  digitalWrite(PIN_LCD_RST, HIGH); delay(120);

  lcdWriteCommand(0x01); delay(150);   // SWRESET
  lcdWriteCommand(0x11); delay(120);   // SLPOUT
  lcdWriteCommand(0x36); lcdWriteData(0x40);  // MADCTL — see below
  lcdWriteCommand(0x3a); lcdWriteData(0x55);  // COLMOD = 16-bit RGB565
  lcdWriteCommand(0x20);               // INVOFF (this panel does NOT want inversion)
  lcdWriteCommand(0x13);               // NORON
  lcdWriteCommand(0x29);               // DISPON
}
```

### MADCTL (register 0x36) — orientation and colour order

**Use `0x40` for portrait 240x296.** This is `MX` (mirror X) only, RGB colour
order.

The bits, for reference: `MY=0x80, MX=0x40, MV=0x20, ML=0x10, BGR=0x08`.

Two independent sources landed on `0x40` — a bring-up sketch whose comment
records that it was chosen to "correct the panel's left/right text flip", and a
separate MicroPython driver. A third sketch used `0x48` (adding the BGR bit)
with an unverified comment; `0x48` renders with red and blue swapped.

**How to verify orientation yourself, correctly:**

Solid-colour corner squares are *not sufficient* — a mirrored solid rectangle
looks identical to an unmirrored one, so you can pass that test while still
being mirrored. Use **text or an asymmetric image**. Draw a word like
`ABC` in a corner; if it reads backwards, you have a mirror; if it's upside
down, you have a 180° rotation.

Landscape (rotated 90°) was attempted on this hardware and is a rabbit hole:

- `MV` (row/column exchange) must be set, which also changes which axis needs
  offsetting.
- The panel's addressable GRAM is **wider than the visible 296 columns**, so a
  column offset (~24px) is needed to stop a band of noise/static appearing at
  one edge.
- Content drawn flush against the true physical edge shows colour corruption
  (yellow rendering as green, green as cyan). Keeping content inset ~20px
  avoids it.

Portrait needs none of these workarounds. **Prefer portrait unless you have a
strong reason not to.**

### Drawing primitives

There is no framebuffer — you write pixels straight out over SPI. The pattern:

```cpp
static void lcdSetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  lcdWriteCommand(0x2a);                        // CASET (column address set)
  lcdWriteData(x0 >> 8); lcdWriteData(x0 & 0xff);
  lcdWriteData(x1 >> 8); lcdWriteData(x1 & 0xff);
  lcdWriteCommand(0x2b);                        // RASET (row address set)
  lcdWriteData(y0 >> 8); lcdWriteData(y0 & 0xff);
  lcdWriteData(y1 >> 8); lcdWriteData(y1 & 0xff);
  lcdWriteCommand(0x2c);                        // RAMWR — pixel data follows
}

static void lcdFillRect(int x, int y, int w, int h, uint16_t color) {
  // clip first, then:
  lcdSetWindow(x, y, x + w - 1, y + h - 1);
  digitalWrite(PIN_LCD_DC, HIGH);
  digitalWrite(PIN_LCD_CS, LOW);
  for (int i = 0; i < w * h; ++i) { SPI.write(color >> 8); SPI.write(color & 0xff); }
  digitalWrite(PIN_LCD_CS, HIGH);
}
```

For bulk data (image rows), `SPI.writeBytes(buf, n)` is far faster than a
per-pixel loop. Circles/diamonds are cheap to build from horizontal 1px-tall
`lcdFillRect` spans.

**Partial redraw beats full redraw.** For an animated sprite, erase only the
sprite's bounding box, repaint whatever background objects intersect that box,
then draw the sprite. Full-screen clears at 40MHz SPI are visibly slow
(240×296 pixels ≈ 142KB per frame).

### Rendering JPEGs

`TJpg_Decoder` works well and decodes straight into your blit callback, so you
never need a full framebuffer in RAM:

```cpp
#include <TJpg_Decoder.h>

static bool jpegOutputCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  // clip to screen, then blit the block via lcdSetWindow + SPI writes
  return true;   // return false to abort decoding
}

void setup() {
  TJpgDec.setJpgScale(1);        // 1, 2, 4 or 8 (integer downscale)
  TJpgDec.setSwapBytes(false);   // false matches the big-endian writes above
  TJpgDec.setCallback(jpegOutputCallback);
  // ...
  TJpgDec.drawFsJpg(x, y, "/image.jpg", FFat);   // from a filesystem
}
```

Storage maths, for planning: a raw RGB565 full-screen frame is
`240 × 296 × 2 = 142KB`. The same frame as a quality-10 JPEG is roughly
**5KB** — a ~28× saving. If you want video-like playback, store JPEG frames
and decode per frame; raw frames will exhaust the 3.7MB partition in ~26
frames.

---

## 5. Touch bring-up (CST810)

Reading a touch point is a 5-byte register read from `0x02`:

```cpp
static bool touchReadRaw(int &rawX, int &rawY) {
  uint8_t data[5] = {};
  if (!i2cReadRegs(CST810_ADDR, 0x02, data, sizeof(data))) return false;
  if ((data[0] & 0x0f) == 0) return false;          // no finger down
  rawX = ((data[1] & 0x0f) << 8) | data[2];
  rawY = ((data[3] & 0x0f) << 8) | data[4];
  return true;
}
```

Polling every 5–10ms in the main loop is fine; the IRQ pin is optional.

### The raw coordinates are NOT screen coordinates

On these units, with the display at MADCTL `0x40`:

- raw **Y** tracks screen **X**
- raw **X** tracks screen **Y**, inverted
- both axes have their own scale and offset

The mapping measured on a reference unit:

```cpp
x = constrain((rawY -  12) * 180 / 211 + 30, 0, LCD_WIDTH  - 1);
y = constrain((260 - rawX) * 216 / 222 + 40, 0, LCD_HEIGHT - 1);
```

### How to calibrate this on YOUR unit

Build a `CAL` serial command into your firmware that draws a crosshair at a
known screen coordinate, waits for a press, averages the raw samples, and
prints both. Run it at two points and solve the linear mapping per axis.

**Critical mistake to avoid:** if your two calibration points lie on the screen
*diagonal* (e.g. top-left and bottom-right), the maths is degenerate — a
swapped-axis mapping and an unswapped one both fit the data, and you cannot
tell them apart. This wasted a lot of time. **Use at least one off-diagonal
point** (e.g. also tap top-right), or verify afterwards by logging live
coordinates while tapping a known on-screen button.

A useful debugging habit: `Serial.printf("touch x=%d y=%d\n", x, y)` on every
tap, permanently, behind a flag. When a button "doesn't work", one glance at
the log tells you whether it's a mapping problem or a hit-test problem.

---

## 6. Audio bring-up (ES8311 playback)

### I²S configuration

```cpp
static constexpr uint32_t AUDIO_SAMPLE_RATE = 16000;
static constexpr uint32_t AUDIO_MCLK_HZ     = 4096000;   // 256 × 16kHz

i2s_config_t cfg = {};
cfg.mode                = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
cfg.sample_rate         = AUDIO_SAMPLE_RATE;
cfg.bits_per_sample     = I2S_BITS_PER_SAMPLE_16BIT;
cfg.channel_format      = I2S_CHANNEL_FMT_RIGHT_LEFT;   // stereo frames
cfg.communication_format= I2S_COMM_FORMAT_STAND_I2S;
cfg.dma_buf_count       = 4;
cfg.dma_buf_len         = 128;
cfg.tx_desc_auto_clear  = true;
cfg.fixed_mclk          = AUDIO_MCLK_HZ;

i2s_pin_config_t pins = {};
pins.mck_io_num   = PIN_I2S_MCLK;
pins.bck_io_num   = PIN_I2S_BCLK;
pins.ws_io_num    = PIN_I2S_LRCK;
pins.data_out_num = PIN_I2S_DOUT;
pins.data_in_num  = I2S_PIN_NO_CHANGE;   // or PIN_I2S_DIN if you also capture

i2s_driver_uninstall(I2S_NUM_0);         // harmless error log on first boot
i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
i2s_set_pin(I2S_NUM_0, &pins);
i2s_zero_dma_buffer(I2S_NUM_0);
```

Then enable the amp (`digitalWrite(PIN_PA_CTRL, HIGH)`) and initialise the
codec. The ES8311 register sequence below is a compact DAC-only setup for
16-bit / 16kHz / 4.096MHz MCLK, derived from Espressif's ES8311 component:

```cpp
i2cWriteReg(ES8311_ADDR, 0x00, 0x1f); delay(20);
i2cWriteReg(ES8311_ADDR, 0x00, 0x00);
i2cWriteReg(ES8311_ADDR, 0x00, 0x80);
i2cWriteReg(ES8311_ADDR, 0x01, 0x3f);
i2cWriteReg(ES8311_ADDR, 0x02, 0x00);
i2cWriteReg(ES8311_ADDR, 0x03, 0x10);
i2cWriteReg(ES8311_ADDR, 0x04, 0x10);
i2cWriteReg(ES8311_ADDR, 0x05, 0x00);
i2cWriteReg(ES8311_ADDR, 0x06, 0x03);
i2cWriteReg(ES8311_ADDR, 0x07, 0x00);
i2cWriteReg(ES8311_ADDR, 0x08, 0xff);
i2cWriteReg(ES8311_ADDR, 0x09, 0x0c);
i2cWriteReg(ES8311_ADDR, 0x0a, 0x0c);
i2cWriteReg(ES8311_ADDR, 0x0d, 0x01);
i2cWriteReg(ES8311_ADDR, 0x0e, 0x02);
i2cWriteReg(ES8311_ADDR, 0x12, 0x00);
i2cWriteReg(ES8311_ADDR, 0x13, 0x10);
i2cWriteReg(ES8311_ADDR, 0x1c, 0x6a);
i2cWriteReg(ES8311_ADDR, 0x31, 0x00);
i2cWriteReg(ES8311_ADDR, 0x32, 0xbf);   // DAC volume
i2cWriteReg(ES8311_ADDR, 0x37, 0x08);
```

### Playing tones and PCM

Everything is "generate `int16_t` samples, duplicate mono→stereo, `i2s_write`".

Square waves give an authentic 8-bit/chiptune timbre (sine sounds soft and
modern by comparison):

```cpp
static void playSquare(float freq, int duration_ms, int amplitude) {
  int16_t samples[64 * 2];
  size_t written = 0;
  int total = (AUDIO_SAMPLE_RATE * duration_ms) / 1000;
  int halfPeriod = max(1, (int)(AUDIO_SAMPLE_RATE / freq / 2));
  for (int frame = 0; frame < total; frame += 64) {
    int n_this = min(64, total - frame);
    for (int i = 0; i < n_this; ++i) {
      int n = frame + i;
      float env = 1.0f - (float)n / max(1, total) * 0.6f;   // decay, avoids clicks
      int16_t s = (int16_t)(((n / halfPeriod) % 2 ? -amplitude : amplitude) * env);
      samples[i*2] = s; samples[i*2+1] = s;
    }
    i2s_write(I2S_NUM_0, samples, n_this * 2 * sizeof(int16_t), &written, portMAX_DELAY);
  }
}
```

For music/voice, store **raw 16kHz mono signed-16-bit PCM** (not MP3 — there is
no decoder in this setup) and stream it in chunks:

```bash
ffmpeg -i input.mp3 -ac 1 -ar 16000 \
       -af "afade=t=out:st=4.7:d=0.3" \
       -f s16le -acodec pcm_s16le output.pcm
```

Always add a short fade-out; abruptly ending PCM produces an audible click.
Budget **32KB per second** of audio at 16kHz mono.

Software volume control is just scaling the samples before writing
(`s = s * volumeLevel / 4`), which pairs naturally with the physical volume
buttons.

---

## 7. Accelerometer bring-up (LIS2DH12)

```cpp
static bool accelInit() {
  uint8_t who = 0;
  if (!i2cReadReg(LIS2DH12_ADDR, 0x0f, who)) return false;   // WHO_AM_I
  if (who != 0x33) return false;                             // expected ID
  i2cWriteReg(LIS2DH12_ADDR, 0x20, 0x57);   // CTRL_REG1: 100Hz, X/Y/Z enabled
  i2cWriteReg(LIS2DH12_ADDR, 0x23, 0x88);   // CTRL_REG4: block data update, high-res
  return true;
}

static bool accelRead(int16_t &x, int16_t &y, int16_t &z) {
  uint8_t raw[6] = {};
  // 0x80 sets the auto-increment bit so one transaction reads all 6 registers
  if (!i2cReadRegs(LIS2DH12_ADDR, 0x28 | 0x80, raw, sizeof(raw))) return false;
  x = (int16_t)((raw[1] << 8) | raw[0]) >> 4;   // 12-bit left-justified
  y = (int16_t)((raw[3] << 8) | raw[2]) >> 4;
  z = (int16_t)((raw[5] << 8) | raw[4]) >> 4;
  return true;
}
```

Values are roughly in milli-g. **Axis signs are a per-board-orientation
question**, not something you can assume. On these units, mapping tilt to
screen coordinates in portrait needs both axes negated:

```cpp
tiltX = -accelX;   // +tiltX pushes a ball toward screen-right
tiltY = -accelY;   // +tiltY pushes a ball toward screen-bottom
```

Determine this by shipping a debug command that prints raw X/Y/Z for a few
seconds while you tilt the device, or just build the feature, try it, and flip
the sign the user reports as backwards. It's a one-character fix.

### A usable tilt-physics loop

```cpp
velX += tiltX * 0.004f;                       // tilt → acceleration
velY += tiltY * 0.004f;
velX = constrain(velX, -6.0f, 6.0f);          // terminal velocity
velY = constrain(velY, -6.0f, 6.0f);
velX *= 0.985f;                               // rolling friction
velY *= 0.985f;
// Then move one axis at a time and test collisions per-axis, so a wall hit
// on X doesn't also kill legitimate Y movement (this makes sliding along
// walls feel right instead of sticky).
```

Tuned for a ~50–60Hz loop (`delay(16)`).

---

## 8. Getting assets onto the device

The 3.7MB FFat partition is the natural home for images, audio and data. There
are two approaches:

1. **Embed in the binary** as `PROGMEM` arrays. Simple, no tooling, but every
   asset change means a full recompile-and-flash cycle.
2. **Upload over serial into FFat.** Slightly more code, but you can iterate on
   assets in seconds without touching the firmware. This is what's recommended
   here, and it avoids needing a filesystem-image builder (`mkfatfs` isn't
   reliably bundled with the Arduino ESP32 core).

### A simple, robust upload protocol

Device side — commands are newline-terminated ASCII:

| Command | Effect |
| --- | --- |
| `LIST` | prints `path size` per file, then `ENDLIST` |
| `PUT <name> <size>` | then `<size>` raw bytes follow; replies `OK` |
| `DEL <name>` | delete a file |
| `FORMAT` | wipe the filesystem |

The `PUT` implementation must be **chunked and acknowledged** (see Gotcha 5):

```cpp
static constexpr size_t CHUNK = 64;          // matches USB full-speed packet size
uint8_t buf[CHUNK];
long remaining = size;
while (remaining > 0) {
  size_t want = min((long)CHUNK, remaining), got = 0;
  uint32_t lastProgress = millis();
  while (got < want) {
    size_t n = Serial.readBytes(buf + got, want - got);
    if (n == 0) {
      if (millis() - lastProgress > 5000) { /* timeout, bail out */ }
      continue;
    }
    got += n;
    lastProgress = millis();
  }
  out.write(buf, got);
  remaining -= got;
  Serial.write('.');                          // ACK this chunk — creates backpressure
}
Serial.println("OK");
```

**Always include the timeout.** Without it, a transfer that dies mid-way leaves
the device stuck in a blocking read loop forever, and it stops responding to
everything — which looks exactly like a crash.

Host side, in Python:

```python
def send_file(ser, local_path, remote_name):
    size = os.path.getsize(local_path)
    data = open(local_path, "rb").read()
    ser.write(f"PUT {remote_name} {size}\n".encode())
    sent = 0
    while sent < len(data):
        chunk = data[sent:sent+64]
        ser.write(chunk)
        if ser.read(1) != b".":              # wait for the ACK before sending more
            raise RuntimeError(f"no ack at byte {sent}")
        sent += len(chunk)
    assert ser.readline().decode().strip() == "OK"
```

Throughput is roughly 70KB/s — fine for a few hundred KB of assets.

**Drain the boot log before sending commands.** Opening the serial port resets
the ESP32, which then emits several seconds of bootloader chatter. If you start
your protocol immediately, those log bytes get read as protocol responses.
Wait until the port has been quiet for ~600–800ms first.

---

## 9. Gotchas

These are the expensive ones. Each cost real debugging time.

### Gotcha 1: `PSRAM=opi` is mandatory

**Symptom:** the firmware boots, then panics and reboots in an endless loop,
with a backtrace ending in `lv_tlsf_create` / `lv_mem_init` (or any allocator).
`Guru Meditation Error: Core 1 panic'ed (StoreProhibited)`.

**Cause:** any code path that calls `ps_malloc()` (allocating from PSRAM) gets
`NULL` back when PSRAM isn't enabled in the build, and the caller dereferences
it. The board default is `PSRAM=disabled`.

**Fix:** always build with `PSRAM=opi` on this board (it has *octal* PSRAM;
`PSRAM=enabled` means quad/QSPI and is wrong here).

### Gotcha 2: colour order and mirroring are separate bugs, and solid shapes hide both

**Symptom:** the display "looks wrong" in a way that's hard to describe.

**Cause / fix:** these are two independent MADCTL bits (`BGR` for colour,
`MX`/`MY`/`MV` for geometry) and you can have either or both wrong.
Diagnose them **separately**:

- **Colour:** fill the entire screen with pure red, then pure green, then pure
  blue. No geometry involved, so the answer is unambiguous.
- **Geometry:** draw *text* or an asymmetric shape. Solid-colour blocks are
  useless for detecting mirroring.

### Gotcha 3: the touch panel is rotated 90° vs the display

**Symptom:** tap-anywhere interactions work fine, but the moment you add real
buttons with hit-testing, nothing responds — or responds in the wrong place.

**Cause:** raw touch X maps to screen Y and vice versa, plus per-axis scale and
offset. See [section 5](#5-touch-bring-up-cst810). Calibrate with at least one
off-diagonal reference point.

### Gotcha 4: not all buttons are exposed, and the middle button is active-HIGH

The BOOT button (GPIO 0) exists electrically but is often **not accessible**
through the enclosure — so "hold BOOT while plugging in" bootloader advice may
be impossible to follow. Plan around it (see Gotcha 6).

Of the three buttons typically exposed: volume up (GPIO 40) and volume down
(GPIO 39) are **active LOW** with internal pullups, while the middle/power key
(GPIO 3) is **active HIGH** with an external pulldown. Getting this backwards
means the button appears permanently pressed.

```cpp
pinMode(PIN_VOLUME_UP,   INPUT_PULLUP);   bool volUp  = digitalRead(PIN_VOLUME_UP)   == LOW;
pinMode(PIN_POWER_KEY,   INPUT);          bool middle = digitalRead(PIN_POWER_KEY)   == HIGH;
```

### Gotcha 5: USB-CDC silently drops bytes on large writes

**Symptom:** you send a 35KB file over serial; the device reports receiving
~230 bytes of the first 256-byte chunk and then times out. No error anywhere.

**Cause:** the ESP32-S3's native USB-CDC receive buffer is small and has **no
flow control**. Bytes beyond its capacity are *discarded*, not delayed. Host
`flush()` does not help — the data left the host fine.

**Fix:** chunk to 64 bytes (the USB full-speed packet size) and have the device
ACK each chunk before the host sends the next. This is the natural backpressure
the protocol otherwise lacks.

### Gotcha 6: recovering a "bricked" device without a BOOT button

**Symptom:** after an interrupted flash, the device stops responding to
`esptool`, and the serial port name changes from something like
`/dev/cu.usbmodem101` to a MAC-address-style `/dev/cu.usbmodem68EE8F60BC2C1`.

**Cause:** the app partition is damaged and the device fell back to its
**TinyUF2 recovery bootloader**.

**Fix:** open that port at **1200 baud** and immediately close it. This is the
standard "1200bps touch" convention; the device resets and re-enumerates under
its normal port name, ready to be flashed again.

```python
import serial, time
serial.Serial("/dev/cu.usbmodemXXXXXXXXXXXX1", 1200).close()
time.sleep(4)   # then flash normally
```

This is exactly why you should keep flashing `tinyuf2.bin` at `0x410000` — it's
your safety net.

### Gotcha 7: the accelerometer address collides with the codec

LIS2DH12 can sit at `0x18` **or** `0x19` depending on its SA0 pin. On this
board `0x18` is already taken by the ES8311 codec, so the accelerometer is at
**`0x19`**. Probing `0x18` for a WHO_AM_I will read codec registers and return
garbage. Always verify `WHO_AM_I == 0x33` before trusting the device.

### Gotcha 8: GPIO 2 cuts the power

`POWER_OFF` (GPIO 2) drives a transistor that pulls `POWER_EN` low through the
power controller — i.e. **driving it HIGH turns the device off**. Initialise it
LOW explicitly at startup:

```cpp
pinMode(PIN_POWER_OFF, OUTPUT);
digitalWrite(PIN_POWER_OFF, LOW);
```

Leaving it floating or accidentally driving it HIGH results in a device that
mysteriously shuts down. (Deliberate power-off = drive HIGH and hold ~2.8s.)

### Gotcha 9: a stale global `lv_conf.h` will silently break LVGL

**Symptom:** LVGL renders but looks visibly degraded (blocky, wrong quality),
and/or crashes at init.

**Cause:** the Arduino LVGL library reads `lv_conf.h` from the *libraries root*
(e.g. `~/Documents/Arduino/libraries/lv_conf.h`). A leftover file there from an
older project — especially an **LVGL v8** config used against **v9** — will be
picked up silently. Many macros were renamed between v8 and v9
(`LV_DRAW_COMPLEX` → `LV_DRAW_SW_COMPLEX`, the whole `LV_MEM_CUSTOM` block →
`LV_USE_STDLIB_MALLOC`, etc.), so the stale settings simply don't apply and you
get defaults you never chose.

**Fix:** copy the template that ships with the installed version and enable it:

```bash
cp ~/Documents/Arduino/libraries/lvgl/lv_conf_template.h \
   ~/Documents/Arduino/libraries/lv_conf.h
# then change the guard at the top:  #if 0  ->  #if 1
```

### Gotcha 10: LVGL's ARM assembly breaks the xtensa build

**Symptom:** compiling LVGL for ESP32 produces a flood of
`Error: unknown opcode or format name 'typedef'` from the assembler.

**Cause:** the library ships ARM NEON/Helium SIMD assembly
(`lv_blend_neon.S`, `lv_blend_helium.S`) that the Arduino builder compiles
unconditionally regardless of target architecture.

**Fix:** rename them out of the build (they're dead code on xtensa anyway):

```bash
cd ~/Documents/Arduino/libraries/lvgl/src/draw/sw/blend
mv neon/lv_blend_neon.S     neon/lv_blend_neon.S.disabled
mv helium/lv_blend_helium.S helium/lv_blend_helium.S.disabled
```

### Gotcha 11: the port disappears right after flashing

`arduino-cli upload` resets the board, which makes the USB device re-enumerate.
Running another `upload` or opening the port immediately afterwards fails with
"could not open port" or "No serial data received".

**Fix:** `sleep 2–5` between operations, and have scripts retry / re-detect the
port rather than caching it.

---

## 10. Suggested project skeleton

```
my_project/
├── my_project/
│   └── my_project.ino          # sketch folder name must match the .ino name
├── tools/
│   ├── make_assets.py          # generate images/audio on the host
│   └── upload_assets.py        # push them over serial
└── provision_kit/              # for flashing units without a toolchain
    ├── bootloader.bin partitions.bin boot_app0.bin app.bin tinyuf2.bin
    ├── assets/
    ├── provision.py            # flash + upload in one command
    └── README.md
```

A `provision.py` that does flash-then-upload turns setting up an additional
unit into a single command, which matters a lot if you're building more than
one device.

Firmware structure that works well on this hardware:

```cpp
enum class AppState { Menu, Game, Guide, /* ... */ };
static AppState state = AppState::Menu;

void loop() {
  handleSerial();          // asset upload / debug commands, always live
  pollPhysicalButtons();   // volume, menu shortcut

  bool tapped = /* edge-detected touch */;

  switch (state) {
    case AppState::Menu:  if (tapped) menuHandleTap(x, y); delay(5);  break;
    case AppState::Game:  gameTick();                      delay(16); break;
    case AppState::Guide: if (tapped) guideHandleTap(x, y); delay(5); break;
  }
}
```

Keeping the serial command handler running in *every* state is worth it — you
can re-upload assets or query state without rebooting into a special mode.

---

## 11. Fast debugging checklist

| Symptom | First thing to check |
| --- | --- |
| Boot loop, panic in an allocator | `PSRAM=opi` missing from the FQBN (Gotcha 1) |
| Blank screen | Backlight pin HIGH? `lcdInit()` actually called? SPI `MISO = -1`? |
| Colours wrong | Fill the screen with solid R/G/B; fix the `BGR` MADCTL bit |
| Text mirrored / upside down | MADCTL geometry bits; never diagnose with solid shapes |
| Noise band at a screen edge | Column/row offset needed (only in rotated modes) |
| Touch does nothing | I²C scan for `0x15`; then log raw coords on every tap |
| Touch off by a lot | Axis swap (Gotcha 3) — recalibrate with off-diagonal points |
| Buttons "always pressed" | Active-HIGH vs active-LOW mixed up (Gotcha 4) |
| No sound | `PIN_PA_CTRL` HIGH? codec init returned true? volume non-zero? |
| Recording is silent | You're using the ES8311 — capture is on the ES7210 |
| Serial upload truncates | Chunk to 64 bytes with per-chunk ACK (Gotcha 5) |
| Device unresponsive after a failed upload | Missing timeout in the `PUT` loop |
| esptool can't connect, odd port name | TinyUF2 recovery; 1200-baud touch (Gotcha 6) |
| Device randomly powers off | GPIO 2 not held LOW (Gotcha 8) |
| Accelerometer not found | Probe `0x19`, not `0x18`; check `WHO_AM_I == 0x33` |

Useful one-liners:

```bash
ls /dev/cu.*                                   # find the port
esptool.py --chip esp32s3 --port PORT flash-id # verify chip + real flash size
arduino-cli monitor -p PORT -c baudrate=115200 # serial monitor
```

An I²C scanner in `setup()` that prints every responding address is worth its
few lines permanently — it instantly distinguishes "my driver is wrong" from
"the chip isn't there".

---

## 12. Reference material

- **Working bring-up code for this board:**
  [`firmware/hardware_test`](firmware/hardware_test/) in this repository — an
  ESP-IDF firmware that drives the display, touch, buttons, speaker,
  microphones and accelerometer. A prebuilt image is on the
  [Releases](https://github.com/ALTIO-AI-PRIVATE-LIMITED/cheeko-gotchi/releases)
  page.
- **Espressif ES8311 / ES7210 drivers** (`esp_codec_dev`) — the origin of the
  register sequences above, if you need capture or more codec features.
- **TJpg_Decoder** — <https://github.com/Bodmer/TJpg_Decoder>
- **ST7789 datasheet** — for the full command set (`0x36` MADCTL, `0x3a`
  COLMOD, `0x2a`/`0x2b`/`0x2c` addressing).

Note that the C++ app API in this repository (`sdk/include/cheeko.h`) is not
wired to the hardware yet: the base firmware in `firmware/cheekoai_base` is a
**scaffold** whose display, touch and audio services are empty stubs with
`// TODO(board)` comments. It's useful as an architectural reference, but it
will not drive this hardware as-is. Until that lands,
[`firmware/hardware_test`](firmware/hardware_test/) is the working reference.
