#pragma once

// Cheeko Gotchi Arduino runtime — hardware constants and low-level helpers.
//
// Every constant in this file is copied from SKILL.md (the verified
// OSTB_XIAOZHI_V1.2 bring-up guide). Do not "fix" values here without
// re-verifying on hardware; several of them were determined empirically.

#ifdef CHEEKO_SYNTAX_CHECK
#include "test/arduino_stubs.h"
#else
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <driver/i2s.h>
#endif

#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// Verified pin map (SKILL.md section 2 — copied verbatim)
// ---------------------------------------------------------------------------

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
static constexpr int PIN_POWER_OFF    = 2;   // OUTPUT: drive HIGH to cut power (Gotcha 8)
static constexpr int PIN_POWER_KEY    = 3;   // middle button, active HIGH + external pulldown
static constexpr int PIN_CHARGE_DET   = 37;  // charge detect
static constexpr int PIN_VOLUME_DOWN  = 39;  // active LOW
static constexpr int PIN_VOLUME_UP    = 40;  // active LOW

// ---- I2C addresses ----
static constexpr uint8_t CST810_ADDR   = 0x15;  // touch
static constexpr uint8_t ES8311_ADDR   = 0x18;  // speaker codec
static constexpr uint8_t LIS2DH12_ADDR = 0x19;  // accelerometer
static constexpr uint8_t ES7210_ADDR   = 0x40;  // mic ADC (capture — unused in runtime v1)

// ---- Panel geometry (SKILL.md section 1/4: portrait 240x296, MADCTL 0x40) ----
static constexpr int LCD_WIDTH  = 240;
static constexpr int LCD_HEIGHT = 296;

// ---- Bus speeds that work reliably (SKILL.md section 2) ----
static constexpr uint32_t I2C_FREQ_HZ = 400000;    // 400kHz I2C
static constexpr uint32_t SPI_FREQ_HZ = 40000000;  // 40MHz SPI

// ---- Audio (SKILL.md section 6) ----
static constexpr uint32_t AUDIO_SAMPLE_RATE = 16000;
static constexpr uint32_t AUDIO_MCLK_HZ     = 4096000;  // 256 x 16kHz

namespace cheeko_hw {

// ---- I2C register helpers ----------------------------------------------
bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t value);
bool i2cReadReg(uint8_t addr, uint8_t reg, uint8_t &value);
bool i2cReadRegs(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len);

// ---- Bus bring-up -------------------------------------------------------
// SPI (display) + Wire (touch/codec/accel) at the verified speeds.
void busInit();

// ---- Display (ST7789-class, portrait 240x296, INVOFF) -------------------
// Panel orientation is PER-UNIT: SKILL.md's reference unit wants MADCTL 0x40,
// but other panel batches are mounted rotated/mirrored. The runtime loads the
// unit's values from NVS (namespace "cheeko_sys") and they can be tuned live
// over serial (MADCTL/OFFSET/SAVE commands) without reflashing.
extern uint8_t g_madctl;      // current MADCTL (0x36) value
extern int g_caset_offset;    // added to CASET range (GRAM column offset)
extern int g_raset_offset;    // added to RASET range (GRAM row offset)
// Touch post-transform applied after the SKILL.md calibration mapping, so a
// rotated panel's touch tracks the rotated display. Tuned via TOUCHMAP.
extern bool g_touch_swap_xy;
extern bool g_touch_invert_x;
extern bool g_touch_invert_y;

void lcdWriteCommand(uint8_t cmd);
void lcdWriteData(uint8_t data);
void lcdInit();
void lcdClearGram();               // raw wipe of the full 240x320 GRAM
void lcdSetMadctl(uint8_t value);  // writes 0x36, updates g_madctl, wipes GRAM
bool lcdSwapped();                 // MV bit set: logical canvas is landscape
// Logical canvas size. Follows the MV bit: 240x296 portrait normally,
// 296x240 landscape when the panel mapping is rotated. Landscape-mounted
// units (they exist) become full-screen landscape simply by rotating with
// the L/R serial commands; apps read the shape via display().Width()/Height().
int lcdWidth();
int lcdHeight();
void lcdSetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
// Clipped solid fill; the workhorse for all drawing primitives.
void lcdFillRect(int x, int y, int w, int h, uint16_t color);
// Stream big-endian RGB565 bytes into the window set by lcdSetWindow().
// No clipping — the caller guarantees the window is fully on screen.
void lcdPushColors(const uint8_t *bytes, size_t len);

// ---- Touch (CST810 @0x15, panel rotated 90 deg vs display) --------------
// Raw 12-bit coordinates; false when no finger is down.
bool touchReadRaw(int &rawX, int &rawY);
// Raw coords mapped to screen space with the SKILL.md section 5 calibration.
bool touchRead(int &x, int &y);

// ---- Audio (I2S master -> ES8311 @0x18, NS4150B amp on GPIO 4) ----------
// Installs the I2S driver (16kHz/16-bit/stereo, 4.096MHz MCLK) and runs the
// verified ES8311 DAC-only register sequence. Returns false if the codec
// does not ACK on I2C.
bool audioInit();
void ampSet(bool on);  // HIGH = amp on. Keep off except while playing.
// Square-wave synth with a decay envelope (SKILL.md section 6). Blocking:
// returns once all samples are queued to I2S DMA. amplitude is 0..32767.
void audioPlaySquare(float freq, int duration_ms, int amplitude);

// ---- Accelerometer (LIS2DH12 @0x19 — NOT 0x18, Gotcha 7) ----------------
// Verifies WHO_AM_I == 0x33 then configures 100Hz high-res mode.
bool accelInit();
// 12-bit readings, roughly milli-g.
bool accelRead(int16_t &x, int16_t &y, int16_t &z);

}  // namespace cheeko_hw
