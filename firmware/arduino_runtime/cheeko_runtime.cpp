// Cheeko Gotchi Arduino runtime — implementation of the cheeko.h app API
// against the OSTB_XIAOZHI_V1.2 hardware, plus the cheeko_rt entry points
// the sketch template (CheekoRuntime.ino.tpl) calls from setup()/loop().
//
// Layering: this file owns all SDK-facing state and event dispatch and calls
// down into cheeko_hw.* for register-level work. Apps only ever see cheeko.h.

#ifdef CHEEKO_SYNTAX_CHECK
#include "test/arduino_stubs.h"
#else
#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#endif

#include <cmath>

#include "cheeko.h"
#include "cheeko_font.h"
#include "cheeko_hw.h"

namespace {

using cheeko::ButtonEvent;
using cheeko::CheekoApp;
using cheeko::MotionSample;
using cheeko::TouchEvent;

// ---------------------------------------------------------------------------
// Runtime state
// ---------------------------------------------------------------------------

CheekoApp *g_app = nullptr;

// Display: background colour used by Clear(); Text() reuses it as the glyph
// cell background (there is no framebuffer to composite against).
uint32_t g_bg_rgb = 0x000000;

// Touch state for Touch::Get()/IsPressed() and edge/move dispatch.
TouchEvent g_touch;

// Buttons: pin, SDK id, polarity, last state.
struct ButtonState {
  int pin;
  ButtonEvent::Button id;
  bool active_high;
  bool pressed;
};
ButtonState g_buttons[4] = {
    {PIN_BOOT_BUTTON, ButtonEvent::Button::Boot, false, false},
    {PIN_POWER_KEY, ButtonEvent::Button::Power, true, false},
    {PIN_VOLUME_UP, ButtonEvent::Button::VolumeUp, false, false},
    {PIN_VOLUME_DOWN, ButtonEvent::Button::VolumeDown, false, false},
};

// Motion / shake detection.
bool g_accel_ok = false;
MotionSample g_last_motion;
bool g_shake_latch = false;
uint32_t g_last_shake_ms = 0;
constexpr float kShakeThresholdG = 0.6f;    // |magnitude - 1g| beyond this
constexpr uint32_t kShakeRefractoryMs = 400;

// Audio.
bool g_audio_ok = false;
int g_volume = 60;  // 0..100
constexpr int kMaxToneAmplitude = 26000;
constexpr int kAmpTailMs = 40;  // let I2S DMA drain before cutting the amp

// Storage (NVS via Preferences).
Preferences g_prefs;
bool g_prefs_ok = false;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

uint16_t To565(uint32_t rgb) {
  return (uint16_t)(((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) |
                    ((rgb >> 3) & 0x001f));
}

void LogLine(const char *prefix, const std::string &message) {
  Serial.print(prefix);
  Serial.println(message.c_str());
}

void DispatchCloudText(const std::string &text) {
  if (g_app) g_app->OnCloudText(text);
}

// ---------------------------------------------------------------------------
// Text rendering: shared 5x7 font at 2x scale. Each glyph cell is 12x14
// (5 columns * 2 + 2px spacing, 7 rows * 2), rendered into a small RGB565
// buffer and pushed with one SPI burst — per-pixel writes are far too slow.
// ---------------------------------------------------------------------------

constexpr int kGlyphCellW = 12;  // 6px advance * 2
constexpr int kGlyphCellH = 14;  // 7px * 2

void DrawGlyph2x(int x, int y, char c, uint16_t fg, uint16_t bg) {
  const uint8_t *glyph = cheeko::FontGlyph(c);
  uint8_t buf[kGlyphCellW * kGlyphCellH * 2];
  int idx = 0;
  for (int row = 0; row < cheeko::kFontHeight; ++row) {
    for (int rep = 0; rep < 2; ++rep) {          // 2x vertical
      for (int col = 0; col < cheeko::kFontWidth; ++col) {
        uint16_t color = (glyph[col] >> row) & 0x01 ? fg : bg;
        uint8_t hi = (uint8_t)(color >> 8), lo = (uint8_t)(color & 0xff);
        buf[idx++] = hi; buf[idx++] = lo;        // 2x horizontal
        buf[idx++] = hi; buf[idx++] = lo;
      }
      uint8_t hi = (uint8_t)(bg >> 8), lo = (uint8_t)(bg & 0xff);
      buf[idx++] = hi; buf[idx++] = lo;          // 2px spacing column
      buf[idx++] = hi; buf[idx++] = lo;
    }
  }
  cheeko_hw::lcdSetWindow((uint16_t)x, (uint16_t)y,
                          (uint16_t)(x + kGlyphCellW - 1),
                          (uint16_t)(y + kGlyphCellH - 1));
  cheeko_hw::lcdPushColors(buf, sizeof(buf));
}

// ---------------------------------------------------------------------------
// Polling: touch, buttons, motion. Called from cheeko_rt::RuntimePoll().
// ---------------------------------------------------------------------------

void PollTouch() {
  int x = 0, y = 0;
  if (cheeko_hw::touchRead(x, y)) {
    bool was_pressed = g_touch.pressed;
    bool moved = (x != g_touch.x) || (y != g_touch.y);
    g_touch.x = x;
    g_touch.y = y;
    g_touch.pressed = true;
    // Press edge, plus movement while held.
    if ((!was_pressed || moved) && g_app) g_app->OnTouch(g_touch);
  } else if (g_touch.pressed) {
    g_touch.pressed = false;  // release edge at the last known position
    if (g_app) g_app->OnTouch(g_touch);
  }
}

void PollButtons() {
  for (int i = 0; i < 4; ++i) {
    ButtonState &b = g_buttons[i];
    int raw = digitalRead(b.pin);
    bool pressed = b.active_high ? (raw == HIGH) : (raw == LOW);
    if (pressed != b.pressed) {
      b.pressed = pressed;
      if (g_app) {
        ButtonEvent event;
        event.button = b.id;
        event.pressed = pressed;
        g_app->OnButton(event);
      }
    }
  }
}

void PollMotion(uint32_t now_ms) {
  if (!g_accel_ok) return;
  int16_t rx = 0, ry = 0, rz = 0;
  if (!cheeko_hw::accelRead(rx, ry, rz)) return;
  // Raw values are roughly milli-g. Both X and Y are negated so that +x
  // tilts toward screen-right and +y toward screen-bottom (SKILL.md sec. 7).
  g_last_motion.x = -(float)rx / 1000.0f;
  g_last_motion.y = -(float)ry / 1000.0f;
  g_last_motion.z = (float)rz / 1000.0f;

  float mag = sqrtf(g_last_motion.x * g_last_motion.x +
                    g_last_motion.y * g_last_motion.y +
                    g_last_motion.z * g_last_motion.z);
  if (fabsf(mag - 1.0f) > kShakeThresholdG &&
      (now_ms - g_last_shake_ms) > kShakeRefractoryMs) {
    g_last_shake_ms = now_ms;
    g_shake_latch = true;
    if (g_app) g_app->OnShake();
  }
}

}  // namespace

// ===========================================================================
// cheeko_rt — entry points for the sketch template
// ===========================================================================

namespace cheeko_rt {

void RuntimeInit() {
  // Gotcha 8, again, defensively: GPIO 2 is the POWER_OFF latch. The sketch
  // template already did this as its very first statement; repeating it here
  // is harmless and protects anyone who writes a custom .ino.
  pinMode(PIN_POWER_OFF, OUTPUT);
  digitalWrite(PIN_POWER_OFF, LOW);

  Serial.begin(115200);
  delay(50);
  Serial.println("[I] cheeko runtime " CHEEKO_SDK_VERSION_STRING " starting");

  cheeko_hw::busInit();
  cheeko_hw::lcdInit();

  // Buttons (SKILL.md Gotcha 4: the middle/power key is active HIGH with an
  // external pulldown; the others are active LOW with internal pullups).
  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
  pinMode(PIN_POWER_KEY, INPUT);
  pinMode(PIN_VOLUME_UP, INPUT_PULLUP);
  pinMode(PIN_VOLUME_DOWN, INPUT_PULLUP);

  g_audio_ok = cheeko_hw::audioInit();
  if (!g_audio_ok) Serial.println("[W] ES8311 codec not responding; audio disabled");

  g_accel_ok = cheeko_hw::accelInit();
  if (!g_accel_ok) Serial.println("[W] LIS2DH12 not found at 0x19; motion disabled");

  g_prefs_ok = g_prefs.begin("cheeko_app", false);
  if (!g_prefs_ok) Serial.println("[W] NVS namespace cheeko_app unavailable");

  Serial.println("[I] hardware init complete");
}

void RuntimeAttachApp(cheeko::CheekoApp *app) {
  g_app = app;
}

void RuntimePoll() {
  uint32_t now = millis();
  PollTouch();
  PollButtons();
  PollMotion(now);
  if (g_app) g_app->OnTick(now);
}

}  // namespace cheeko_rt

// ===========================================================================
// cheeko.h implementation
// ===========================================================================

namespace cheeko {

// ---- Display --------------------------------------------------------------

void Display::Clear(uint32_t rgb) {
  g_bg_rgb = rgb;
  cheeko_hw::lcdFillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, To565(rgb));
}

void Display::Text(int x, int y, const std::string &text) {
  // Fixed ink-on-background rendering: no framebuffer means every pixel of
  // the glyph cell is written, so the cell background is the last Clear()
  // colour. Glyph cells that do not fit entirely on screen are skipped.
  const uint16_t fg = To565(Color::Ink);
  const uint16_t bg = To565(g_bg_rgb);
  int cx = x;
  for (size_t i = 0; i < text.size(); ++i) {
    if (cx >= 0 && cx + kGlyphCellW <= LCD_WIDTH && y >= 0 &&
        y + kGlyphCellH <= LCD_HEIGHT) {
      DrawGlyph2x(cx, y, text[i], fg, bg);
    }
    cx += kGlyphCellW;  // 12px advance
  }
}

void Display::CenterText(int y, const std::string &text) {
  int w = (int)text.size() * kGlyphCellW;
  int x = (LCD_WIDTH - w) / 2;
  if (x < 0) x = 0;
  Text(x, y, text);
}

void Display::Rect(int x, int y, int w, int h, uint32_t rgb) {
  if (w <= 0 || h <= 0) return;
  uint16_t c = To565(rgb);
  cheeko_hw::lcdFillRect(x, y, w, 1, c);          // top
  cheeko_hw::lcdFillRect(x, y + h - 1, w, 1, c);  // bottom
  cheeko_hw::lcdFillRect(x, y, 1, h, c);          // left
  cheeko_hw::lcdFillRect(x + w - 1, y, 1, h, c);  // right
}

void Display::FillRect(int x, int y, int w, int h, uint32_t rgb) {
  cheeko_hw::lcdFillRect(x, y, w, h, To565(rgb));
}

void Display::Circle(int x, int y, int radius, uint32_t rgb) {
  if (radius < 0) return;
  uint16_t c = To565(rgb);
  if (radius == 0) {
    cheeko_hw::lcdFillRect(x, y, 1, 1, c);
    return;
  }
  // Midpoint circle; each octant point is a 1px fill (clipped by lcdFillRect).
  int dx = 0, dy = radius, d = 3 - 2 * radius;
  while (dy >= dx) {
    cheeko_hw::lcdFillRect(x + dx, y + dy, 1, 1, c);
    cheeko_hw::lcdFillRect(x - dx, y + dy, 1, 1, c);
    cheeko_hw::lcdFillRect(x + dx, y - dy, 1, 1, c);
    cheeko_hw::lcdFillRect(x - dx, y - dy, 1, 1, c);
    cheeko_hw::lcdFillRect(x + dy, y + dx, 1, 1, c);
    cheeko_hw::lcdFillRect(x - dy, y + dx, 1, 1, c);
    cheeko_hw::lcdFillRect(x + dy, y - dx, 1, 1, c);
    cheeko_hw::lcdFillRect(x - dy, y - dx, 1, 1, c);
    if (d > 0) {
      d += 4 * (dx - dy) + 10;
      --dy;
    } else {
      d += 4 * dx + 6;
    }
    ++dx;
  }
}

void Display::FillCircle(int x, int y, int radius, uint32_t rgb) {
  if (radius < 0) return;
  uint16_t c = To565(rgb);
  // Horizontal spans are cheap on this panel (SKILL.md section 4).
  for (int dy = -radius; dy <= radius; ++dy) {
    int span = (int)sqrtf((float)(radius * radius - dy * dy));
    cheeko_hw::lcdFillRect(x - span, y + dy, span * 2 + 1, 1, c);
  }
}

void Display::Line(int x1, int y1, int x2, int y2, uint32_t rgb) {
  uint16_t c = To565(rgb);
  // Bresenham with 1px fills.
  int dx = x2 > x1 ? x2 - x1 : x1 - x2;
  int dy = y2 > y1 ? y2 - y1 : y1 - y2;
  int sx = x1 < x2 ? 1 : -1;
  int sy = y1 < y2 ? 1 : -1;
  int err = dx - dy;
  int x = x1, y = y1;
  while (true) {
    cheeko_hw::lcdFillRect(x, y, 1, 1, c);
    if (x == x2 && y == y2) break;
    int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x += sx;
    }
    if (e2 < dx) {
      err += dx;
      y += sy;
    }
  }
}

void Display::Image(const std::string &path) {
  LogLine("[W] ", "Display::Image not yet supported: " + path);
}

// ---- Speaker ---------------------------------------------------------------

void Speaker::Play(const std::string &path) {
  LogLine("[W] ", "Speaker::Play not yet supported (upload PCM assets in v2): " + path);
}

void Speaker::Tone(int frequency_hz, int duration_ms) {
  if (!g_audio_ok) {
    Serial.println("[W] Tone ignored: audio init failed");
    return;
  }
  if (frequency_hz <= 0 || duration_ms <= 0 || g_volume <= 0) return;
  int amplitude = kMaxToneAmplitude * g_volume / 100;
  cheeko_hw::ampSet(true);  // amp on only while playing
  cheeko_hw::audioPlaySquare((float)frequency_hz, duration_ms, amplitude);
  delay(kAmpTailMs);        // let the DMA buffers drain before cutting power
  cheeko_hw::ampSet(false);
}

void Speaker::SetVolume(int volume) {
  if (volume < 0) volume = 0;
  if (volume > 100) volume = 100;
  g_volume = volume;
}

// ---- Microphone ------------------------------------------------------------
// Capture lives on the ES7210 ADC, which runtime v1 does not drive.
// OnMicAudio never fires; see README.md.

void Microphone::Start(int sample_rate_hz) {
  (void)sample_rate_hz;
  Serial.println(
      "[W] Microphone::Start: capture (ES7210) is not implemented in runtime "
      "v1; OnMicAudio will not fire");
}

void Microphone::Stop() {
  Serial.println("[W] Microphone::Stop: capture is not implemented in runtime v1");
}

// ---- Touch -----------------------------------------------------------------

TouchEvent Touch::Get() { return g_touch; }

bool Touch::IsPressed() { return g_touch.pressed; }

// ---- Motion ----------------------------------------------------------------

MotionSample Motion::Read() {
  // Fresh sample when possible; falls back to the last polled value.
  int16_t rx = 0, ry = 0, rz = 0;
  if (g_accel_ok && cheeko_hw::accelRead(rx, ry, rz)) {
    g_last_motion.x = -(float)rx / 1000.0f;  // signs: SKILL.md section 7
    g_last_motion.y = -(float)ry / 1000.0f;
    g_last_motion.z = (float)rz / 1000.0f;
  }
  return g_last_motion;
}

bool Motion::IsShaken() {
  bool shaken = g_shake_latch;
  g_shake_latch = false;  // edge-triggered: report each shake once
  return shaken;
}

// ---- Storage ---------------------------------------------------------------
// NVS-backed via the Preferences library, namespace "cheeko_app".
// Note the NVS limit: keys must be 15 characters or fewer.

void Storage::PutInt(const std::string &key, int value) {
  if (!g_prefs_ok) return;
  g_prefs.putInt(key.c_str(), value);
}

int Storage::GetInt(const std::string &key, int fallback) {
  if (!g_prefs_ok) return fallback;
  return g_prefs.getInt(key.c_str(), fallback);
}

void Storage::PutString(const std::string &key, const std::string &value) {
  if (!g_prefs_ok) return;
  g_prefs.putString(key.c_str(), value.c_str());
}

std::string Storage::GetString(const std::string &key,
                               const std::string &fallback) {
  if (!g_prefs_ok) return fallback;
  String v = g_prefs.getString(key.c_str(), String(fallback.c_str()));
  return std::string(v.c_str());
}

bool Storage::Has(const std::string &key) {
  if (!g_prefs_ok) return false;
  return g_prefs.isKey(key.c_str());
}

void Storage::Remove(const std::string &key) {
  if (!g_prefs_ok) return;
  g_prefs.remove(key.c_str());
}

// ---- Log -------------------------------------------------------------------

void Log::Info(const std::string &message) { LogLine("[I] ", message); }

void Log::Warn(const std::string &message) { LogLine("[W] ", message); }

void Log::Error(const std::string &message) { LogLine("[E] ", message); }

// ---- Cloud -----------------------------------------------------------------

void Cloud::Connect() {
  Serial.println("[I] Cloud::Connect: no cloud endpoint configured; treating as success");
}

void Cloud::StartVoiceSession() {
  Serial.println("[W] Cloud::StartVoiceSession: not available in runtime v1");
  DispatchCloudText("voice sessions require the Cheeko cloud service");
}

void Cloud::SendText(const std::string &text) {
  LogLine("[W] ", "Cloud::SendText not available in runtime v1: " + text);
  DispatchCloudText("voice sessions require the Cheeko cloud service");
}

void Cloud::GetJson(const std::string &url) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[W] Cloud::GetJson: Wi-Fi not connected");
    return;
  }
  HTTPClient http;
  if (!http.begin(String(url.c_str()))) {
    LogLine("[E] ", "Cloud::GetJson: bad URL: " + url);
    return;
  }
  int code = http.GET();
  if (code > 0) {
    String body = http.getString();
    Serial.print("[I] Cloud::GetJson HTTP ");
    Serial.println(code);
    DispatchCloudText(std::string(body.c_str()));
  } else {
    Serial.print("[E] Cloud::GetJson failed, code ");
    Serial.println(code);
  }
  http.end();
}

void Cloud::PostJson(const std::string &url, const std::string &json) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[W] Cloud::PostJson: Wi-Fi not connected");
    return;
  }
  HTTPClient http;
  if (!http.begin(String(url.c_str()))) {
    LogLine("[E] ", "Cloud::PostJson: bad URL: " + url);
    return;
  }
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(String(json.c_str()));
  if (code > 0) {
    String body = http.getString();
    Serial.print("[I] Cloud::PostJson HTTP ");
    Serial.println(code);
    DispatchCloudText(std::string(body.c_str()));
  } else {
    Serial.print("[E] Cloud::PostJson failed, code ");
    Serial.println(code);
  }
  http.end();
}

// ---- Wifi ------------------------------------------------------------------
// Credentials come from the system NVS namespace "cheeko_sys" (keys
// "wifi_ssid"/"wifi_pass"), provisioned by the base firmware / dev tools.

void Wifi::Connect() {
  Preferences sys;
  if (!sys.begin("cheeko_sys", true)) {
    Serial.println("[W] Wifi::Connect: no cheeko_sys NVS namespace (device not provisioned)");
    return;
  }
  String ssid = sys.getString("wifi_ssid", String(""));
  String pass = sys.getString("wifi_pass", String(""));
  sys.end();
  if (ssid.length() == 0) {
    Serial.println("[W] Wifi::Connect: no wifi_ssid stored");
    return;
  }

  Serial.print("[I] Wifi::Connect: joining ");
  Serial.println(ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
    delay(100);  // bounded wait: 8s, then give up without blocking the app
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[I] Wifi::Connect: connected");
  } else {
    Serial.println("[W] Wifi::Connect: timed out after 8s");
  }
}

bool Wifi::IsConnected() const { return WiFi.status() == WL_CONNECTED; }

// ---- Runtime singleton -----------------------------------------------------

Display &CheekoRuntime::display() {
  static Display instance;
  return instance;
}

Speaker &CheekoRuntime::speaker() {
  static Speaker instance;
  return instance;
}

Microphone &CheekoRuntime::mic() {
  static Microphone instance;
  return instance;
}

Touch &CheekoRuntime::touch() {
  static Touch instance;
  return instance;
}

Motion &CheekoRuntime::motion() {
  static Motion instance;
  return instance;
}

Storage &CheekoRuntime::storage() {
  static Storage instance;
  return instance;
}

Log &CheekoRuntime::log() {
  static Log instance;
  return instance;
}

Cloud &CheekoRuntime::cloud() {
  static Cloud instance;
  return instance;
}

Wifi &CheekoRuntime::wifi() {
  static Wifi instance;
  return instance;
}

CheekoRuntime &Cheeko() {
  static CheekoRuntime runtime;
  return runtime;
}

}  // namespace cheeko
