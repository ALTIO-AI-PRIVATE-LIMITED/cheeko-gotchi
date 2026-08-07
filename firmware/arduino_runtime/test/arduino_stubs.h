// Minimal fake Arduino / ESP32 API declarations for host-side syntax checking
// of the Cheeko Gotchi Arduino runtime. NOT a simulator — every function is a
// no-op. Only the symbols the runtime actually touches are declared.
//
// Used two ways (see test/syntax_check.sh):
//   1. Force-included via `g++ -include test/arduino_stubs.h`.
//   2. Included by the runtime sources themselves under -DCHEEKO_SYNTAX_CHECK
//      in place of <Arduino.h>, <SPI.h>, <Wire.h>, <Preferences.h>, <WiFi.h>,
//      <HTTPClient.h> and <driver/i2s.h>.
// The classic include guard makes the double inclusion harmless.

#ifndef CHEEKO_TEST_ARDUINO_STUBS_H_
#define CHEEKO_TEST_ARDUINO_STUBS_H_

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// Core Arduino
// ---------------------------------------------------------------------------

#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define LOW 0x0
#define HIGH 0x1

#define constrain(amt, low, high) \
  ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

inline void pinMode(int pin, int mode) { (void)pin; (void)mode; }
inline void digitalWrite(int pin, int value) { (void)pin; (void)value; }
inline int digitalRead(int pin) { (void)pin; return LOW; }
inline void delay(unsigned long ms) { (void)ms; }
inline void delayMicroseconds(unsigned int us) { (void)us; }
inline unsigned long millis() { return 0; }

// Arduino String — just enough for Preferences / HTTPClient / WiFi call sites.
class String {
 public:
  String() {}
  String(const char* s) : value_(s ? s : "") {}  // NOLINT: implicit, like Arduino
  const char* c_str() const { return value_.c_str(); }
  unsigned int length() const { return (unsigned int)value_.size(); }

 private:
  std::string value_;
};

class HardwareSerial {
 public:
  void begin(unsigned long baud) { (void)baud; }
  void print(const char* s) { (void)s; }
  void print(int v) { (void)v; }
  void println(const char* s) { (void)s; }
  void println(int v) { (void)v; }
  void println() {}
  int printf(const char* fmt, ...) { (void)fmt; return 0; }
  int available() { return 0; }
  int read() { return -1; }
};
static HardwareSerial Serial;

// ---------------------------------------------------------------------------
// SPI
// ---------------------------------------------------------------------------

class SPIClass {
 public:
  void begin(int sclk, int miso, int mosi, int cs) {
    (void)sclk; (void)miso; (void)mosi; (void)cs;
  }
  void setFrequency(uint32_t hz) { (void)hz; }
  void write(uint8_t b) { (void)b; }
  void writeBytes(const uint8_t* data, size_t len) { (void)data; (void)len; }
};
static SPIClass SPI;

// ---------------------------------------------------------------------------
// Wire (I2C)
// ---------------------------------------------------------------------------

class TwoWire {
 public:
  bool begin(int sda, int scl, uint32_t freq) {
    (void)sda; (void)scl; (void)freq; return true;
  }
  void beginTransmission(uint8_t addr) { (void)addr; }
  size_t write(uint8_t b) { (void)b; return 1; }
  uint8_t endTransmission(bool sendStop = true) { (void)sendStop; return 0; }
  uint8_t requestFrom(uint8_t addr, uint8_t len) { (void)addr; return len; }
  int available() { return 0; }
  int read() { return 0; }
};
static TwoWire Wire;

// ---------------------------------------------------------------------------
// Preferences (NVS)
// ---------------------------------------------------------------------------

class Preferences {
 public:
  bool begin(const char* name, bool readOnly = false) {
    (void)name; (void)readOnly; return true;
  }
  void end() {}
  size_t putInt(const char* key, int32_t value) { (void)key; (void)value; return 4; }
  int32_t getInt(const char* key, int32_t defaultValue = 0) {
    (void)key; return defaultValue;
  }
  size_t putString(const char* key, const char* value) {
    (void)key; (void)value; return 0;
  }
  String getString(const char* key, String defaultValue = String()) {
    (void)key; return defaultValue;
  }
  bool isKey(const char* key) { (void)key; return false; }
  bool remove(const char* key) { (void)key; return true; }
};

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------

#define WIFI_STA 1
#define WL_CONNECTED 3

class WiFiClass {
 public:
  void mode(int m) { (void)m; }
  void begin(const char* ssid, const char* pass) { (void)ssid; (void)pass; }
  int status() { return 0; }
};
static WiFiClass WiFi;

// ---------------------------------------------------------------------------
// HTTPClient
// ---------------------------------------------------------------------------

class HTTPClient {
 public:
  bool begin(String url) { (void)url; return true; }
  void addHeader(const String& name, const String& value) {
    (void)name; (void)value;
  }
  int GET() { return -1; }
  int POST(String body) { (void)body; return -1; }
  String getString() { return String(); }
  void end() {}
};

// ---------------------------------------------------------------------------
// ESP-IDF I2S driver (legacy driver/i2s.h API, as used by the ESP32 core)
// ---------------------------------------------------------------------------

typedef int esp_err_t;
typedef uint32_t TickType_t;
#define portMAX_DELAY ((TickType_t)0xffffffffUL)

typedef enum { I2S_NUM_0 = 0, I2S_NUM_1 = 1 } i2s_port_t;

typedef enum {
  I2S_MODE_MASTER = 1,
  I2S_MODE_SLAVE = 2,
  I2S_MODE_TX = 4,
  I2S_MODE_RX = 8,
} i2s_mode_t;

typedef enum { I2S_BITS_PER_SAMPLE_16BIT = 16 } i2s_bits_per_sample_t;
typedef enum { I2S_CHANNEL_FMT_RIGHT_LEFT = 0 } i2s_channel_fmt_t;
typedef enum { I2S_COMM_FORMAT_STAND_I2S = 1 } i2s_comm_format_t;

#define I2S_PIN_NO_CHANGE (-1)

typedef struct {
  i2s_mode_t mode;
  uint32_t sample_rate;
  i2s_bits_per_sample_t bits_per_sample;
  i2s_channel_fmt_t channel_format;
  i2s_comm_format_t communication_format;
  int intr_alloc_flags;
  int dma_buf_count;
  int dma_buf_len;
  bool use_apll;
  bool tx_desc_auto_clear;
  int fixed_mclk;
} i2s_config_t;

typedef struct {
  int mck_io_num;
  int bck_io_num;
  int ws_io_num;
  int data_out_num;
  int data_in_num;
} i2s_pin_config_t;

inline esp_err_t i2s_driver_install(i2s_port_t port, const i2s_config_t* cfg,
                                    int queue_size, void* queue) {
  (void)port; (void)cfg; (void)queue_size; (void)queue; return 0;
}
inline esp_err_t i2s_driver_uninstall(i2s_port_t port) { (void)port; return 0; }
inline esp_err_t i2s_set_pin(i2s_port_t port, const i2s_pin_config_t* pins) {
  (void)port; (void)pins; return 0;
}
inline esp_err_t i2s_zero_dma_buffer(i2s_port_t port) { (void)port; return 0; }
inline esp_err_t i2s_write(i2s_port_t port, const void* src, size_t size,
                           size_t* bytes_written, TickType_t ticks_to_wait) {
  (void)port; (void)src; (void)ticks_to_wait;
  if (bytes_written) *bytes_written = size;
  return 0;
}

#endif  // CHEEKO_TEST_ARDUINO_STUBS_H_
