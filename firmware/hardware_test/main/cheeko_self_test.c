// Cheeko Gotchi — all-in-one hardware self-test (ESP-IDF + LVGL).
//
// Target board: Cheeko Gotchi v1.2 (ESP32-S3).
// Display:  2.01" 240x296, JD9853 controller (SPI), driven via esp_lcd
//           (DMA) + LVGL through esp_lvgl_port, using the panel init sequence below.
// Touch:    CST810 @ I2C 0x15 (custom LVGL pointer indev)
// Audio:    ES8311 speaker DAC @ 0x18, ES7210 mic ADC @ 0x40, shared I2S.
//           The speaker amp (GPIO4) is gated ON only while a tone plays, so the
//           I2S idle state can't buzz the speaker.
// Motion:   LIS2DH12 accelerometer @ 0x19
// Power:    one-key latch (GPIO2), charge detect (GPIO37)

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/spi_master.h"
#include "esp_lcd_jd9853.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "cheeko-selftest";

// ---------------------------------------------------------------------------
// Pin map — Cheeko Gotchi v1.2
// ---------------------------------------------------------------------------
#define PIN_BOOT_BUTTON 0
#define PIN_TOUCH_IRQ 1
#define PIN_POWER_OFF 2
#define PIN_POWER_KEY 3
#define PIN_PA_CTRL 4
#define PIN_I2S_MCLK 5
#define PIN_I2S_DOUT 6
#define PIN_I2S_DIN 7
#define PIN_LCD_DC 8
#define PIN_LCD_SCLK 9
#define PIN_LCD_MOSI 10
#define PIN_I2C_SCL 11
#define PIN_I2C_SDA 12
#define PIN_LCD_BL 13
#define PIN_LCD_CS 14
#define PIN_I2S_BCLK 15
#define PIN_I2S_LRCK 16
#define PIN_LCD_RST 17
#define PIN_CHARGE_DET 37
#define PIN_VOLUME_DOWN 39
#define PIN_VOLUME_UP 40

#define CST810_ADDR 0x15
#define ES8311_ADDR 0x18
#define ES7210_ADDR 0x40
#define LIS2DH12_ADDR 0x19
#define LIS2DH12_WHO_AM_I 0x33

#define AUDIO_SAMPLE_RATE 16000
#define LCD_H_RES 240
#define LCD_V_RES 296
#define LCD_SPI_HOST SPI2_HOST

static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

// ---- Touch calibration: 3-point affine (raw -> screen), persisted in NVS ----
static float s_cal[6] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f};  // identity until calibrated
static bool s_have_cal = false;

static bool solve3(const double rx[3], const double ry[3], const double s[3], double o[3]) {
  double det = rx[0] * (ry[1] - ry[2]) - ry[0] * (rx[1] - rx[2]) + (rx[1] * ry[2] - rx[2] * ry[1]);
  if (fabs(det) < 1e-6) return false;
  o[0] = (s[0] * (ry[1] - ry[2]) - ry[0] * (s[1] - s[2]) + (s[1] * ry[2] - s[2] * ry[1])) / det;
  o[1] = (rx[0] * (s[1] - s[2]) - s[0] * (rx[1] - rx[2]) + (rx[1] * s[2] - rx[2] * s[1])) / det;
  o[2] = (rx[0] * (ry[1] * s[2] - ry[2] * s[1]) - ry[0] * (rx[1] * s[2] - rx[2] * s[1]) +
          s[0] * (rx[1] * ry[2] - rx[2] * ry[1])) / det;
  return true;
}
static void apply_cal(int rawx, int rawy, int *sx, int *sy) {
  int x = (int)(s_cal[0] * rawx + s_cal[1] * rawy + s_cal[2] + 0.5f);
  int y = (int)(s_cal[3] * rawx + s_cal[4] * rawy + s_cal[5] + 0.5f);
  *sx = imin(imax(x, 0), LCD_H_RES - 1);
  *sy = imin(imax(y, 0), LCD_V_RES - 1);
}
static void cal_load(void) {
  nvs_handle_t h;
  if (nvs_open("selftest", NVS_READONLY, &h) != ESP_OK) return;
  size_t sz = sizeof(s_cal);
  if (nvs_get_blob(h, "touchcal", s_cal, &sz) == ESP_OK && sz == sizeof(s_cal)) s_have_cal = true;
  nvs_close(h);
}
static void cal_save(void) {
  nvs_handle_t h;
  if (nvs_open("selftest", NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_blob(h, "touchcal", s_cal, sizeof(s_cal));
  nvs_commit(h);
  nvs_close(h);
}

// ---------------------------------------------------------------------------
// JD9853 2.01" 240x296 panel init sequence. esp_lcd panel_io sends
// SLPOUT + MADCTL + COLMOD before this; the 0x3A here sets RGB565 (0x05).
// ---------------------------------------------------------------------------
static const jd9853_lcd_init_cmd_t JD9853_PANEL_INIT[] = {
    {0xDF, (uint8_t[]){0x98, 0x53}, 2, 0},
    {0xDE, (uint8_t[]){0x00}, 1, 0},
    {0xB2, (uint8_t[]){0x25}, 1, 0},
    {0xB7, (uint8_t[]){0x00, 0x29, 0x00, 0x51}, 4, 0},
    {0xBB, (uint8_t[]){0x4F, 0x1A, 0x55, 0x73, 0x63, 0xF0}, 6, 0},
    {0xC0, (uint8_t[]){0x44, 0xA4}, 2, 0},
    {0xC1, (uint8_t[]){0x12}, 1, 0},
    {0xC3, (uint8_t[]){0x7D, 0x07, 0x14, 0x06, 0xC8, 0x71, 0x6C, 0x77}, 8, 0},
    {0xC4, (uint8_t[]){0x00, 0x00, 0x94, 0x79, 0x25, 0x0A, 0x16, 0x79, 0x25, 0x0A, 0x16, 0x82}, 12, 0},
    {0xC8, (uint8_t[]){0x3F, 0x34, 0x2D, 0x26, 0x2B, 0x2B, 0x25, 0x24, 0x23, 0x22, 0x20, 0x17, 0x14, 0x0E, 0x06, 0x00,
                       0x3F, 0x34, 0x2D, 0x26, 0x2B, 0x2B, 0x25, 0x24, 0x23, 0x22, 0x20, 0x17, 0x14, 0x0E, 0x06, 0x00}, 32, 0},
    {0xD0, (uint8_t[]){0x04, 0x06, 0x6B, 0x0F, 0x00}, 5, 0},
    {0xD7, (uint8_t[]){0x00, 0x30}, 2, 0},
    {0xE6, (uint8_t[]){0x10}, 1, 0},
    {0xDE, (uint8_t[]){0x01}, 1, 0},
    {0xB7, (uint8_t[]){0x03, 0x13, 0xEF, 0x35, 0x35}, 5, 0},
    {0xC1, (uint8_t[]){0x14, 0x15, 0xC0}, 3, 0},
    {0xC2, (uint8_t[]){0x06, 0x3A}, 2, 0},
    {0xC4, (uint8_t[]){0x72, 0x12}, 2, 0},
    {0xBE, (uint8_t[]){0x00}, 1, 0},
    {0xDE, (uint8_t[]){0x00}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x3A, (uint8_t[]){0x05}, 1, 0},                    // RGB565
    {0x2A, (uint8_t[]){0x00, 0x00, 0x00, 0xEF}, 4, 0},  // CASET 0..239
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0x27}, 4, 0},  // RASET 0..295
    {0x11, (uint8_t[]){0x00}, 0, 120},                  // sleep out
    {0x29, (uint8_t[]){0x00}, 0, 10},                   // display on
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_dev_cst810, s_dev_es8311, s_dev_es7210, s_dev_accel;
static i2s_chan_handle_t s_tx_chan, s_rx_chan;
static bool s_seen_i2c[128];
static bool s_audio_ready, s_accel_ready;

typedef struct { const char *name; int pin; bool active_high; bool down; } button_t;
static button_t s_buttons[] = {
    {"BOOT", PIN_BOOT_BUTTON, false, false},
    {"PWR", PIN_POWER_KEY, true, false},   // active-high w/ external pulldown
    {"VOL-", PIN_VOLUME_DOWN, false, false},
    {"VOL+", PIN_VOLUME_UP, false, false},
};
#define NUM_BUTTONS (sizeof(s_buttons) / sizeof(s_buttons[0]))

// touch state (read in the main loop; consumed by the LVGL indev read_cb)
static volatile bool s_touch;
static volatile int s_touch_x, s_touch_y;

// LVGL objects
static lv_obj_t *lbl_i2c, *lbl_touch, *lbl_audio, *lbl_btn, *lbl_power, *lbl_accel,
    *lbl_txy, *lbl_uptime, *bar_mic, *lbl_mic;

// ---------------------------------------------------------------------------
// I2C
// ---------------------------------------------------------------------------
static i2c_master_dev_handle_t i2c_add(uint8_t addr) {
  i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = 400000};
  i2c_master_dev_handle_t dev = NULL;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c_bus, &cfg, &dev));
  return dev;
}
static bool i2c_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val) {
  uint8_t b[2] = {reg, val};
  return i2c_master_transmit(dev, b, 2, 50) == ESP_OK;
}
static bool i2c_read_regs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *buf, size_t len) {
  return i2c_master_transmit_receive(dev, &reg, 1, buf, len, 50) == ESP_OK;
}
static void i2c_init(void) {
  i2c_master_bus_config_t cfg = {
      .clk_source = I2C_CLK_SRC_DEFAULT, .i2c_port = I2C_NUM_0,
      .scl_io_num = PIN_I2C_SCL, .sda_io_num = PIN_I2C_SDA,
      .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true};
  ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &s_i2c_bus));
  s_dev_cst810 = i2c_add(CST810_ADDR);
  s_dev_es8311 = i2c_add(ES8311_ADDR);
  s_dev_es7210 = i2c_add(ES7210_ADDR);
  s_dev_accel = i2c_add(LIS2DH12_ADDR);
}
static void i2c_scan(void) {
  memset(s_seen_i2c, 0, sizeof(s_seen_i2c));
  ESP_LOGI(TAG, "I2C scan:");
  for (uint8_t a = 1; a < 127; ++a) {
    if (i2c_master_probe(s_i2c_bus, a, 50) == ESP_OK) {
      s_seen_i2c[a] = true;
      const char *who = a == CST810_ADDR ? " CST810" : a == ES8311_ADDR ? " ES8311"
                        : a == ES7210_ADDR ? " ES7210" : a == LIS2DH12_ADDR ? " LIS2DH12" : "";
      ESP_LOGI(TAG, "  0x%02X%s", a, who);
    }
  }
}
static bool touch_read(int *x, int *y) {
  uint8_t d[5] = {0};
  if (!i2c_read_regs(s_dev_cst810, 0x02, d, sizeof(d))) return false;
  if ((d[0] & 0x0f) == 0) return false;
  *x = ((d[1] & 0x0f) << 8) | d[2];
  *y = ((d[3] & 0x0f) << 8) | d[4];
  return true;
}

// ---------------------------------------------------------------------------
// Audio (amp gated so idle I2S can't buzz)
// ---------------------------------------------------------------------------
static bool es8311_init(void) {
  static const uint8_t r[][2] = {
      {0x00, 0x1f}, {0x00, 0x00}, {0x00, 0x80}, {0x01, 0x3f}, {0x02, 0x00}, {0x03, 0x10}, {0x04, 0x10},
      {0x05, 0x00}, {0x06, 0x03}, {0x07, 0x00}, {0x08, 0xff}, {0x09, 0x0c}, {0x0a, 0x0c}, {0x0d, 0x01},
      {0x0e, 0x02}, {0x12, 0x00}, {0x13, 0x10}, {0x1c, 0x6a}, {0x31, 0x00}, {0x32, 0xbf}, {0x37, 0x08}};
  bool ok = true;
  for (size_t i = 0; i < sizeof(r) / sizeof(r[0]); ++i) {
    ok &= i2c_write_reg(s_dev_es8311, r[i][0], r[i][1]);
    if (i == 0) vTaskDelay(pdMS_TO_TICKS(20));
  }
  return ok;
}
static bool es7210_init(void) {
  static const uint8_t r[][2] = {
      {0x00, 0xff}, {0x00, 0x41}, {0x01, 0x3f}, {0x09, 0x30}, {0x0a, 0x30}, {0x23, 0x2a}, {0x22, 0x0a},
      {0x20, 0x0a}, {0x21, 0x2a}, {0x08, 0x00}, {0x40, 0x43}, {0x41, 0x70}, {0x42, 0x70}, {0x07, 0x20},
      {0x02, 0xc1}, {0x11, 0x60}, {0x12, 0x00}, {0x01, 0x34}, {0x06, 0x00}, {0x47, 0x08}, {0x48, 0x08},
      {0x4b, 0x00}, {0x4c, 0xff}, {0x43, 0x1a}, {0x44, 0x1a}, {0x40, 0x43}, {0x00, 0x71}, {0x00, 0x41}};
  bool ok = true;
  for (size_t i = 0; i < sizeof(r) / sizeof(r[0]); ++i) ok &= i2c_write_reg(s_dev_es7210, r[i][0], r[i][1]);
  return ok;
}
static bool audio_init(void) {
  gpio_config_t pa = {.pin_bit_mask = (1ULL << PIN_PA_CTRL), .mode = GPIO_MODE_OUTPUT};
  gpio_config(&pa);
  gpio_set_level(PIN_PA_CTRL, 1);  // amp ON; idle TX is silenced via auto_clear (no buzz)

  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;  // underrun/idle sends zeros, not stale buffer -> silent, not a beep
  if (i2s_new_channel(&chan_cfg, &s_tx_chan, &s_rx_chan) != ESP_OK) return false;
  i2s_std_config_t std = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {.mclk = PIN_I2S_MCLK, .bclk = PIN_I2S_BCLK, .ws = PIN_I2S_LRCK,
                   .dout = PIN_I2S_DOUT, .din = PIN_I2S_DIN, .invert_flags = {0}}};
  if (i2s_channel_init_std_mode(s_tx_chan, &std) != ESP_OK) return false;
  if (i2s_channel_init_std_mode(s_rx_chan, &std) != ESP_OK) return false;
  if (i2s_channel_enable(s_tx_chan) != ESP_OK) return false;
  if (i2s_channel_enable(s_rx_chan) != ESP_OK) return false;
  bool dac = es8311_init(), adc = es7210_init();
  ESP_LOGI(TAG, "ES8311 %s, ES7210 %s", dac ? "ok" : "FAIL", adc ? "ok" : "FAIL");
  return dac && adc;
}
static int mic_read_peak(void) {
  int16_t s[128 * 2];
  size_t n = 0;
  if (i2s_channel_read(s_rx_chan, s, sizeof(s), &n, pdMS_TO_TICKS(20)) != ESP_OK || n == 0) return -1;
  int peak = 0, c = n / sizeof(int16_t);
  for (int i = 0; i < c; ++i) { int v = s[i] < 0 ? -s[i] : s[i]; if (v > peak) peak = v; }
  return peak;
}
static void play_tone(float freq, int ms, int amp) {
  if (!s_audio_ready) return;  // amp stays on; auto_clear keeps idle silent
  int16_t buf[64 * 2];
  size_t w = 0;
  int total = (AUDIO_SAMPLE_RATE * ms) / 1000;
  for (int f = 0; f < total; f += 64) {
    int n = imin(64, total - f);
    for (int i = 0; i < n; ++i) {
      float t = (float)(f + i) / AUDIO_SAMPLE_RATE;
      float env = 1.0f - ((float)(f + i) / imax(1, total));
      int16_t v = (int16_t)(sinf(2.0f * (float)M_PI * freq * t) * env * amp);
      buf[i * 2] = v; buf[i * 2 + 1] = v;
    }
    i2s_channel_write(s_tx_chan, buf, n * 2 * sizeof(int16_t), &w, portMAX_DELAY);
  }
}

// ---------------------------------------------------------------------------
// Accelerometer
// ---------------------------------------------------------------------------
static bool accel_init(void) {
  uint8_t who = 0;
  if (!i2c_read_regs(s_dev_accel, 0x0f, &who, 1) || who != LIS2DH12_WHO_AM_I) {
    ESP_LOGW(TAG, "LIS2DH12 not found (WHO_AM_I=0x%02X)", who);
    return false;
  }
  bool ok = i2c_write_reg(s_dev_accel, 0x20, 0x57);
  ok &= i2c_write_reg(s_dev_accel, 0x23, 0x88);
  return ok;
}
static void accel_read(int *x, int *y, int *z) {
  uint8_t raw[6] = {0};
  if (!s_accel_ready || !i2c_read_regs(s_dev_accel, 0x28 | 0x80, raw, sizeof(raw))) { *x = *y = *z = 0; return; }
  *x = (int16_t)((raw[1] << 8) | raw[0]) >> 4;
  *y = (int16_t)((raw[3] << 8) | raw[2]) >> 4;
  *z = (int16_t)((raw[5] << 8) | raw[4]) >> 4;
}

// ---------------------------------------------------------------------------
// Buttons / power GPIO
// ---------------------------------------------------------------------------
static void gpio_inputs_init(void) {
  gpio_config_t off = {.pin_bit_mask = (1ULL << PIN_POWER_OFF), .mode = GPIO_MODE_OUTPUT};
  gpio_config(&off);
  gpio_set_level(PIN_POWER_OFF, 0);  // keep unit on

  uint64_t m = (1ULL << PIN_TOUCH_IRQ) | (1ULL << PIN_CHARGE_DET) | (1ULL << PIN_BOOT_BUTTON) |
               (1ULL << PIN_VOLUME_DOWN) | (1ULL << PIN_VOLUME_UP);
  gpio_config_t in = {.pin_bit_mask = m, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
  gpio_config(&in);
  // POWER_KEY (GPIO3) is active-high w/ external pulldown — no internal pull-up.
  gpio_config_t pwr = {.pin_bit_mask = (1ULL << PIN_POWER_KEY), .mode = GPIO_MODE_INPUT, .pull_down_en = GPIO_PULLDOWN_ENABLE};
  gpio_config(&pwr);
}
static void read_buttons(void) {
  for (size_t i = 0; i < NUM_BUTTONS; ++i) {
    int lv = gpio_get_level(s_buttons[i].pin);
    s_buttons[i].down = s_buttons[i].active_high ? (lv == 1) : (lv == 0);
  }
}

// ---------------------------------------------------------------------------
// Display (esp_lcd + JD9853, DMA)
// ---------------------------------------------------------------------------
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;

static void display_init(void) {
  gpio_config_t bl = {.pin_bit_mask = (1ULL << PIN_LCD_BL), .mode = GPIO_MODE_OUTPUT};
  gpio_config(&bl);
  gpio_set_level(PIN_LCD_BL, 0);  // keep backlight off until first frame is drawn

  spi_bus_config_t bus = JD9853_PANEL_BUS_SPI_CONFIG(PIN_LCD_SCLK, PIN_LCD_MOSI, LCD_H_RES * 40 * 2);
  ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_spi_config_t io_cfg = JD9853_PANEL_IO_SPI_CONFIG(PIN_LCD_CS, PIN_LCD_DC, NULL, NULL);
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_cfg, &s_io));

  jd9853_vendor_config_t vendor = {.init_cmds = JD9853_PANEL_INIT,
                                   .init_cmds_size = sizeof(JD9853_PANEL_INIT) / sizeof(JD9853_PANEL_INIT[0])};
  esp_lcd_panel_dev_config_t panel_cfg = {
      .reset_gpio_num = PIN_LCD_RST,
      .rgb_endian = LCD_RGB_ENDIAN_BGR,   // BGR -> MADCTL 0x08, matches this panel
      .bits_per_pixel = 16,
      .vendor_config = &vendor};
  ESP_ERROR_CHECK(esp_lcd_new_panel_jd9853(s_io, &panel_cfg, &s_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
}

// ---------------------------------------------------------------------------
// LVGL touch indev — reads the cached touch state (no I2C in this callback)
// ---------------------------------------------------------------------------
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  if (s_touch) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = imin(imax(s_touch_x, 0), LCD_H_RES - 1);
    data->point.y = imin(imax(s_touch_y, 0), LCD_V_RES - 1);
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static lv_obj_t *mk_label(int y, const char *txt) {
  lv_obj_t *l = lv_label_create(lv_screen_active());
  lv_obj_set_pos(l, 6, y);
  lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(l, txt);
  return l;
}
static void set_row(lv_obj_t *l, const char *txt, bool ok) {
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_color(l, ok ? lv_color_hex(0x33DD66) : lv_color_hex(0xFF5555), 0);
}

static void ui_create(lv_display_t *disp) {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  lv_obj_t *title = lv_label_create(scr);
  lv_obj_set_pos(title, 6, 4);
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFD166), 0);
  lv_label_set_text(title, "CHEEKO GOTCHI  HW TEST");

  // Color bars (verify display color order).
  const uint32_t cols[] = {0xFF0000, 0xFF7F00, 0xFFFF00, 0x00C040, 0x00FFFF, 0x0040FF, 0xFFFFFF};
  int n = sizeof(cols) / sizeof(cols[0]), w = LCD_H_RES / n;
  for (int i = 0; i < n; ++i) {
    lv_obj_t *r = lv_obj_create(scr);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, w, 16);
    lv_obj_set_pos(r, i * w, 26);
    lv_obj_set_style_bg_color(r, lv_color_hex(cols[i]), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
  }

  lbl_i2c = mk_label(50, "I2C: ...");
  lbl_touch = mk_label(72, "TOUCH: ...");
  lbl_audio = mk_label(94, "AUDIO: ...");
  lbl_btn = mk_label(116, "BTN: ....");
  lbl_power = mk_label(138, "POWER: ...");

  lbl_mic = lv_label_create(scr);
  lv_obj_set_pos(lbl_mic, 6, 160);
  lv_obj_set_style_text_color(lbl_mic, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(lbl_mic, "MIC");
  bar_mic = lv_bar_create(scr);
  lv_obj_set_size(bar_mic, 180, 14);
  lv_obj_set_pos(bar_mic, 50, 160);
  lv_bar_set_range(bar_mic, 0, 100);
  lv_bar_set_value(bar_mic, 0, LV_ANIM_OFF);

  lbl_accel = mk_label(182, "ACCEL: ...");
  lbl_txy = mk_label(204, "TXY: ...");
  lbl_uptime = mk_label(226, "UPTIME: 0 s");
  lv_obj_set_style_text_color(lbl_uptime, lv_color_hex(0x48D5FF), 0);

  // Custom pointer indev fed by the cached CST810 state, with a visible cursor.
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_read_cb);
  lv_indev_set_display(indev, disp);
  lv_obj_t *cursor = lv_obj_create(scr);
  lv_obj_remove_style_all(cursor);
  lv_obj_set_size(cursor, 16, 16);
  lv_obj_set_style_radius(cursor, 8, 0);
  lv_obj_set_style_bg_color(cursor, lv_color_hex(0xFF5FA2), 0);
  lv_obj_set_style_bg_opa(cursor, LV_OPA_COVER, 0);
  lv_indev_set_cursor(indev, cursor);
}

// ---------------------------------------------------------------------------
// Touch calibration UI: show 3 dots, collect raw taps, solve + save transform.
// ---------------------------------------------------------------------------
static void run_calibration(void) {
  const int tgt[3][2] = {{30, 40}, {210, 40}, {120, 256}};
  double rx[3], ry[3], sxv[3], syv[3];

  lv_obj_t *scr = lv_screen_active();
  lv_obj_t *dot = NULL, *info = NULL;
  if (lvgl_port_lock(0)) {
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    info = lv_label_create(scr);
    lv_obj_set_style_text_color(info, lv_color_hex(0xFFD166), 0);
    lv_obj_set_pos(info, 24, 130);
    lv_label_set_text(info, "TOUCH CALIBRATION");
    dot = lv_obj_create(scr);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 20, 20);
    lv_obj_set_style_radius(dot, 10, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x33DD66), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lvgl_port_unlock();
  }

  int rxr, ryr;
  for (int i = 0; i < 3; ++i) {
    if (lvgl_port_lock(0)) {
      lv_obj_set_pos(dot, tgt[i][0] - 10, tgt[i][1] - 10);
      lv_label_set_text_fmt(info, "TOUCH THE DOT  %d/3", i + 1);
      lvgl_port_unlock();
    }
    while (touch_read(&rxr, &ryr)) vTaskDelay(pdMS_TO_TICKS(20));   // wait release
    while (!touch_read(&rxr, &ryr)) vTaskDelay(pdMS_TO_TICKS(20));  // wait press
    long ax = 0, ay = 0;
    int cnt = 0;
    for (int k = 0; k < 10; ++k) {  // average while held
      int xx, yy;
      if (touch_read(&xx, &yy)) { ax += xx; ay += yy; cnt++; }
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (cnt == 0) cnt = 1;
    rx[i] = (double)ax / cnt;
    ry[i] = (double)ay / cnt;
    sxv[i] = tgt[i][0];
    syv[i] = tgt[i][1];
    ESP_LOGI(TAG, "cal pt %d: raw=%.0f,%.0f -> screen=%d,%d", i + 1, rx[i], ry[i], tgt[i][0], tgt[i][1]);
    if (lvgl_port_lock(0)) { lv_label_set_text(info, "OK"); lvgl_port_unlock(); }
    while (touch_read(&rxr, &ryr)) vTaskDelay(pdMS_TO_TICKS(20));  // wait release
  }

  double cx[3], cy[3];
  if (solve3(rx, ry, sxv, cx) && solve3(rx, ry, syv, cy)) {
    s_cal[0] = cx[0]; s_cal[1] = cx[1]; s_cal[2] = cx[2];
    s_cal[3] = cy[0]; s_cal[4] = cy[1]; s_cal[5] = cy[2];
    s_have_cal = true;
    cal_save();
    ESP_LOGI(TAG, "cal X: %.4f %.4f %.1f | Y: %.4f %.4f %.1f", cx[0], cx[1], cx[2], cy[0], cy[1], cy[2]);
  } else {
    ESP_LOGW(TAG, "calibration solve failed (collinear taps?)");
  }
  if (lvgl_port_lock(0)) {
    if (dot) lv_obj_delete(dot);
    if (info) lv_obj_delete(info);
    lvgl_port_unlock();
  }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
void app_main(void) {
  ESP_LOGI(TAG, "Cheeko Gotchi hardware test (LVGL) / JD9853 240x296");

  esp_err_t nerr = nvs_flash_init();
  if (nerr == ESP_ERR_NVS_NO_FREE_PAGES || nerr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }

  gpio_inputs_init();
  i2c_init();
  i2c_scan();
  display_init();

  // LVGL via esp_lvgl_port (DMA flush, partial buffer, RGB565 byte-swapped).
  lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
  port_cfg.task_priority = 4;
  port_cfg.task_stack = 6144;
#if CONFIG_SOC_CPU_CORES_NUM > 1
  port_cfg.task_affinity = 1;
#endif
  ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

  lvgl_port_display_cfg_t disp_cfg = {
      .io_handle = s_io,
      .panel_handle = s_panel,
      .buffer_size = LCD_H_RES * 20,
      .double_buffer = false,
      .hres = LCD_H_RES,
      .vres = LCD_V_RES,
      .monochrome = false,
      .rotation = {.swap_xy = false, .mirror_x = false, .mirror_y = false},
      .color_format = LV_COLOR_FORMAT_RGB565,
      .flags = {.buff_dma = 1, .swap_bytes = 1},
  };
  lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
  gpio_set_level(PIN_LCD_BL, 1);  // backlight on

  // Touch calibration: run if none is saved, or if BOOT is held at power-up.
  cal_load();
  bool force_cal = (gpio_get_level(PIN_BOOT_BUTTON) == 0);
  if (!s_have_cal || force_cal) {
    ESP_LOGI(TAG, "touch calibration (have_cal=%d force=%d)", s_have_cal, force_cal);
    run_calibration();
  }

  if (lvgl_port_lock(0)) {
    ui_create(disp);
    lvgl_port_unlock();
  }

  s_audio_ready = audio_init();
  s_accel_ready = accel_init();
  ESP_LOGI(TAG, "audio=%d accel=%d", s_audio_ready, s_accel_ready);
  play_tone(1000.0f, 90, 7000);  // single boot chirp

  bool prev_btn[NUM_BUTTONS] = {false}, was_touch = false;
  int frame_peak = -1, tick = 0;

  while (1) {
    read_buttons();
    for (size_t i = 0; i < NUM_BUTTONS; ++i) {
      if (s_buttons[i].down && !prev_btn[i]) {
        ESP_LOGI(TAG, "button %s", s_buttons[i].name);
        play_tone(660.0f + i * 110.0f, 45, 7000);
      }
      prev_btn[i] = s_buttons[i].down;
    }

    int trx, tryy;
    bool t = touch_read(&trx, &tryy);
    if (t) { int sx, sy; apply_cal(trx, tryy, &sx, &sy); s_touch_x = sx; s_touch_y = sy; }
    s_touch = t;
    if (t && !was_touch) {
      ESP_LOGI(TAG, "touch raw %d,%d -> %d,%d", trx, tryy, s_touch_x, s_touch_y);
      play_tone(2200.0f, 35, 7000);
    }
    was_touch = t;

    if (s_audio_ready) { int p = mic_read_peak(); if (p > frame_peak) frame_peak = p; }

    if (++tick % 3 == 0) {  // update UI ~every 150ms
      int ax, ay, az; accel_read(&ax, &ay, &az);
      int peak = frame_peak < 0 ? 0 : frame_peak; frame_peak = -1;
      bool core = s_seen_i2c[CST810_ADDR] && s_seen_i2c[ES8311_ADDR] && s_seen_i2c[ES7210_ADDR];
      char b[64];
      if (lvgl_port_lock(0)) {
        set_row(lbl_i2c, core ? "I2C: CORE OK" : "I2C: CHECK BUS", core);
        set_row(lbl_touch, s_seen_i2c[CST810_ADDR] ? "TOUCH: 0x15 OK" : "TOUCH: MISS", s_seen_i2c[CST810_ADDR]);
        set_row(lbl_audio, s_audio_ready ? "AUDIO: ES8311+ES7210" : "AUDIO: INIT FAIL", s_audio_ready);
        snprintf(b, sizeof(b), "BTN: %s %s %s %s", s_buttons[0].down ? "BOOT" : "----",
                 s_buttons[1].down ? "PWR" : "---", s_buttons[2].down ? "V-" : "--", s_buttons[3].down ? "V+" : "--");
        lv_label_set_text(lbl_btn, b);
        lv_obj_set_style_text_color(lbl_btn, lv_color_hex(0xFFFFFF), 0);
        snprintf(b, sizeof(b), "POWER: chg %s", gpio_get_level(PIN_CHARGE_DET) ? "HIGH" : "LOW");
        lv_label_set_text(lbl_power, b);
        lv_obj_set_style_text_color(lbl_power, lv_color_hex(0xFFFFFF), 0);
        int pct = imin(peak, 12000) * 100 / 12000;
        lv_bar_set_value(bar_mic, pct, LV_ANIM_OFF);
        if (s_accel_ready) { snprintf(b, sizeof(b), "ACCEL: %d %d %d", ax, ay, az); set_row(lbl_accel, b, true); }
        else set_row(lbl_accel, "ACCEL: NOT FOUND", false);
        if (s_touch) snprintf(b, sizeof(b), "TXY: %d %d", s_touch_x, s_touch_y);
        else snprintf(b, sizeof(b), "TXY: --");
        lv_label_set_text(lbl_txy, b);
        lv_obj_set_style_text_color(lbl_txy, lv_color_hex(0xFFFFFF), 0);
        snprintf(b, sizeof(b), "UPTIME: %d s", tick / 20);
        lv_label_set_text(lbl_uptime, b);
        lvgl_port_unlock();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
