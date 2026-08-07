// Cheeko Gotchi Arduino runtime — low-level hardware drivers.
//
// All sequences here are transcribed from SKILL.md, the verified bring-up
// guide for the OSTB_XIAOZHI_V1.2 board. Comments reference the relevant
// SKILL.md section / gotcha so future edits can be checked against it.

#include "cheeko_hw.h"

namespace cheeko_hw {

// ===========================================================================
// I2C register helpers
// ===========================================================================

bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool i2cReadReg(uint8_t addr, uint8_t reg, uint8_t &value) {
  return i2cReadRegs(addr, reg, &value, 1);
}

bool i2cReadRegs(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;  // repeated start
  size_t got = Wire.requestFrom(addr, (uint8_t)len);
  if (got != len) {
    while (Wire.available()) Wire.read();  // drain partial data
    return false;
  }
  for (size_t i = 0; i < len; ++i) buf[i] = (uint8_t)Wire.read();
  return true;
}

// ===========================================================================
// Bus bring-up (SKILL.md section 2: 400kHz I2C, 40MHz SPI, no MISO)
// ===========================================================================

void busInit() {
  SPI.begin(PIN_LCD_SCLK, -1, PIN_LCD_MOSI, PIN_LCD_CS);
  SPI.setFrequency(SPI_FREQ_HZ);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);
}

// ===========================================================================
// Display (SKILL.md section 4)
// ===========================================================================

void lcdWriteCommand(uint8_t cmd) {
  digitalWrite(PIN_LCD_DC, LOW);
  digitalWrite(PIN_LCD_CS, LOW);
  SPI.write(cmd);
  digitalWrite(PIN_LCD_CS, HIGH);
}

void lcdWriteData(uint8_t data) {
  digitalWrite(PIN_LCD_DC, HIGH);
  digitalWrite(PIN_LCD_CS, LOW);
  SPI.write(data);
  digitalWrite(PIN_LCD_CS, HIGH);
}

// Verified init sequence: SWRESET, SLPOUT, MADCTL=0x40 (MX only, RGB order,
// portrait 240x296), COLMOD=RGB565, INVOFF (this panel does NOT want
// inversion), NORON, DISPON.
void lcdInit() {
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
  lcdWriteCommand(0x36); lcdWriteData(0x40);  // MADCTL — portrait, see SKILL.md
  lcdWriteCommand(0x3a); lcdWriteData(0x55);  // COLMOD = 16-bit RGB565
  lcdWriteCommand(0x20);               // INVOFF
  lcdWriteCommand(0x13);               // NORON
  lcdWriteCommand(0x29);               // DISPON
}

void lcdSetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  lcdWriteCommand(0x2a);                        // CASET (column address set)
  lcdWriteData(x0 >> 8); lcdWriteData(x0 & 0xff);
  lcdWriteData(x1 >> 8); lcdWriteData(x1 & 0xff);
  lcdWriteCommand(0x2b);                        // RASET (row address set)
  lcdWriteData(y0 >> 8); lcdWriteData(y0 & 0xff);
  lcdWriteData(y1 >> 8); lcdWriteData(y1 & 0xff);
  lcdWriteCommand(0x2c);                        // RAMWR — pixel data follows
}

void lcdPushColors(const uint8_t *bytes, size_t len) {
  digitalWrite(PIN_LCD_DC, HIGH);
  digitalWrite(PIN_LCD_CS, LOW);
  SPI.writeBytes(bytes, len);
  digitalWrite(PIN_LCD_CS, HIGH);
}

void lcdFillRect(int x, int y, int w, int h, uint16_t color) {
  // Clip to the panel first.
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
  if (x + w > LCD_WIDTH)  w = LCD_WIDTH - x;
  if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;
  if (w <= 0 || h <= 0) return;

  lcdSetWindow((uint16_t)x, (uint16_t)y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));

  // Bulk writeBytes is far faster than a per-pixel SPI.write loop
  // (SKILL.md section 4, "Drawing primitives").
  static uint8_t chunk[512];  // 256 pixels per burst
  const uint8_t hi = (uint8_t)(color >> 8);
  const uint8_t lo = (uint8_t)(color & 0xff);
  int total = w * h;
  int chunkPixels = (int)(sizeof(chunk) / 2);
  if (chunkPixels > total) chunkPixels = total;
  for (int i = 0; i < chunkPixels; ++i) {
    chunk[i * 2] = hi;
    chunk[i * 2 + 1] = lo;
  }

  digitalWrite(PIN_LCD_DC, HIGH);
  digitalWrite(PIN_LCD_CS, LOW);
  int remaining = total;
  while (remaining > 0) {
    int n = remaining < chunkPixels ? remaining : chunkPixels;
    SPI.writeBytes(chunk, (size_t)n * 2);
    remaining -= n;
  }
  digitalWrite(PIN_LCD_CS, HIGH);
}

// ===========================================================================
// Touch (SKILL.md section 5)
// ===========================================================================

bool touchReadRaw(int &rawX, int &rawY) {
  uint8_t data[5] = {};
  if (!i2cReadRegs(CST810_ADDR, 0x02, data, sizeof(data))) return false;
  if ((data[0] & 0x0f) == 0) return false;          // no finger down
  rawX = ((data[1] & 0x0f) << 8) | data[2];
  rawY = ((data[3] & 0x0f) << 8) | data[4];
  return true;
}

bool touchRead(int &x, int &y) {
  int rawX = 0, rawY = 0;
  if (!touchReadRaw(rawX, rawY)) return false;
  // Measured mapping from the reference unit (SKILL.md section 5).
  // The panel is rotated 90 deg vs the display: raw Y tracks screen X,
  // raw X tracks screen Y inverted, each axis with its own scale/offset.
  x = constrain((rawY -  12) * 180 / 211 + 30, 0, LCD_WIDTH  - 1);
  y = constrain((260 - rawX) * 216 / 222 + 40, 0, LCD_HEIGHT - 1);
  return true;
}

// ===========================================================================
// Audio (SKILL.md section 6 — ES8311 playback only; capture is on the ES7210
// and is not implemented in runtime v1)
// ===========================================================================

bool audioInit() {
  pinMode(PIN_PA_CTRL, OUTPUT);
  digitalWrite(PIN_PA_CTRL, LOW);  // amp off until something actually plays

  i2s_config_t cfg = {};
  cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate          = AUDIO_SAMPLE_RATE;
  cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;   // stereo frames
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count        = 4;
  cfg.dma_buf_len          = 128;
  cfg.tx_desc_auto_clear   = true;
  cfg.fixed_mclk           = AUDIO_MCLK_HZ;

  i2s_pin_config_t pins = {};
  pins.mck_io_num   = PIN_I2S_MCLK;
  pins.bck_io_num   = PIN_I2S_BCLK;
  pins.ws_io_num    = PIN_I2S_LRCK;
  pins.data_out_num = PIN_I2S_DOUT;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;  // capture unused in v1

  i2s_driver_uninstall(I2S_NUM_0);        // harmless error log on first boot
  i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);

  // ES8311 DAC-only register sequence for 16-bit / 16kHz / 4.096MHz MCLK,
  // derived from Espressif's ES8311 component (SKILL.md section 6).
  if (!i2cWriteReg(ES8311_ADDR, 0x00, 0x1f)) return false;  // codec absent?
  delay(20);
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
  return true;
}

void ampSet(bool on) {
  digitalWrite(PIN_PA_CTRL, on ? HIGH : LOW);
}

// Square waves give the authentic chiptune timbre (SKILL.md section 6).
// The decay envelope avoids clicks at note end.
void audioPlaySquare(float freq, int duration_ms, int amplitude) {
  if (freq <= 0.0f || duration_ms <= 0 || amplitude <= 0) return;
  if (amplitude > 32767) amplitude = 32767;

  int16_t samples[64 * 2];
  size_t written = 0;
  int total = (int)(((long)AUDIO_SAMPLE_RATE * duration_ms) / 1000);
  if (total < 1) total = 1;
  int halfPeriod = (int)(AUDIO_SAMPLE_RATE / freq / 2);
  if (halfPeriod < 1) halfPeriod = 1;

  for (int frame = 0; frame < total; frame += 64) {
    int n_this = (total - frame) < 64 ? (total - frame) : 64;
    for (int i = 0; i < n_this; ++i) {
      int n = frame + i;
      float env = 1.0f - (float)n / (float)total * 0.6f;   // decay, avoids clicks
      int16_t s = (int16_t)(((n / halfPeriod) % 2 ? -amplitude : amplitude) * env);
      samples[i * 2] = s;       // duplicate mono -> stereo
      samples[i * 2 + 1] = s;
    }
    i2s_write(I2S_NUM_0, samples, (size_t)n_this * 2 * sizeof(int16_t), &written,
              portMAX_DELAY);
  }
}

// ===========================================================================
// Accelerometer (SKILL.md section 7; address 0x19, NOT 0x18 — Gotcha 7)
// ===========================================================================

bool accelInit() {
  uint8_t who = 0;
  if (!i2cReadReg(LIS2DH12_ADDR, 0x0f, who)) return false;   // WHO_AM_I
  if (who != 0x33) return false;                             // expected ID
  i2cWriteReg(LIS2DH12_ADDR, 0x20, 0x57);   // CTRL_REG1: 100Hz, X/Y/Z enabled
  i2cWriteReg(LIS2DH12_ADDR, 0x23, 0x88);   // CTRL_REG4: block data update, high-res
  return true;
}

bool accelRead(int16_t &x, int16_t &y, int16_t &z) {
  uint8_t raw[6] = {};
  // 0x80 sets the auto-increment bit so one transaction reads all 6 registers
  if (!i2cReadRegs(LIS2DH12_ADDR, 0x28 | 0x80, raw, sizeof(raw))) return false;
  x = (int16_t)((raw[1] << 8) | raw[0]) >> 4;   // 12-bit left-justified
  y = (int16_t)((raw[3] << 8) | raw[2]) >> 4;
  z = (int16_t)((raw[5] << 8) | raw[4]) >> 4;
  return true;
}

}  // namespace cheeko_hw
