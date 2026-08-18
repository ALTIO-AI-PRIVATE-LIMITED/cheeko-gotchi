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
#include <WiFiClientSecure.h>
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
// Print mapped touch coords on each press (enabled by serial tuning commands).
bool g_touch_echo = false;
// DEBUG serial command: log touch/button/shake events as they dispatch.
bool g_debug_input = false;

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

// Amp lifecycle. The NS4150B class-D amp has a soft-start of several ms that
// swallows the front of whatever plays right after enabling it — long alarm
// tones shrug that off, but short clock-tick clicks vanish entirely. So the
// amp is pre-warmed before a cold start, kept on for a hangover window after
// each sound (rapid sequences then hold it open continuously), and only cut
// by RuntimePoll() once audio has been quiet for a while.
bool g_amp_on = false;
uint32_t g_amp_off_ms = 0;
constexpr uint32_t kAmpHangoverMs = 600;

void AmpWarm() {
  if (!g_amp_on) {
    cheeko_hw::ampSet(true);
    g_amp_on = true;
    delay(12);  // soft-start; without this the first ~10ms of audio is lost
  }
}

void AmpRelease() {  // called right after playback finishes queueing
  g_amp_off_ms = millis() + kAmpTailMs + kAmpHangoverMs;
}

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

// Every log line carries the device uptime, so spacing and ordering stay
// readable when scrolling back through a monitor session.
void LogLine(const char *prefix, const std::string &message) {
  char stamp[16];
  const unsigned s = (unsigned)(millis() / 1000u);
  snprintf(stamp, sizeof(stamp), "%02u:%02u:%02u ", s / 3600u, (s / 60u) % 60u,
           s % 60u);
  Serial.print(prefix);
  Serial.print(stamp);
  Serial.println(message.c_str());
}

// "https://ntfy.sh/topic/json?poll=1" -> "ntfy.sh/topic" — identifies the
// endpoint without drowning the console in query strings.
std::string UrlBrief(const std::string &url) {
  size_t start = url.find("://");
  start = start == std::string::npos ? 0 : start + 3;
  std::string brief = url.substr(start);
  const size_t query = brief.find('?');
  if (query != std::string::npos) brief = brief.substr(0, query);
  if (brief.size() > 40) brief = brief.substr(0, 40) + "...";
  return brief;
}

// Fetch logging with the no-news case de-duplicated: polling loops that get
// HTTP 200 with an empty body forever say nothing new, so those are counted
// silently and summarised once a minute. Anything notable (an error status
// or an actual payload) prints immediately.
void LogFetch(const char *verb, const std::string &url, int code, int bytes,
              uint32_t took_ms) {
  static uint32_t quiet_polls = 0;
  static uint32_t last_summary_ms = 0;
  if (code == 200 && bytes == 0) {
    ++quiet_polls;
    if (millis() - last_summary_ms >= 60000u) {
      LogLine("[I] ", std::string(verb) + " " + UrlBrief(url) + ": " +
                          std::to_string(quiet_polls) +
                          " polls ok, no new data (last minute)");
      quiet_polls = 0;
      last_summary_ms = millis();
    }
    return;
  }
  LogLine(code == 200 ? "[I] " : "[W] ",
          std::string(verb) + " " + UrlBrief(url) + " -> HTTP " +
              std::to_string(code) + ", " + std::to_string(bytes) + "B in " +
              std::to_string(took_ms) + "ms");
  quiet_polls = 0;
  last_summary_ms = millis();
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
  // Cell pixel (gx, gy) at 2x scale; the last two columns are glyph spacing.
  auto cellPixel = [&](int gx, int gy) -> uint16_t {
    if (gx >= cheeko::kFontWidth * 2) return bg;
    return (glyph[gx / 2] >> (gy / 2)) & 0x01 ? fg : bg;
  };
  // Always row-major: the controller's MV bit handles axis exchange, so the
  // stream order is the same in portrait and landscape (see lcdSetWindow).
  for (int gy = 0; gy < kGlyphCellH; ++gy) {
    for (int gx = 0; gx < kGlyphCellW; ++gx) {
      uint16_t color = cellPixel(gx, gy);
      buf[idx++] = (uint8_t)(color >> 8);
      buf[idx++] = (uint8_t)(color & 0xff);
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
    if (!was_pressed && (g_touch_echo || g_debug_input)) {
      char line[48];
      snprintf(line, sizeof(line), "touch down x=%d y=%d", x, y);
      LogLine("[I] ", line);
    }
    // Press edge, plus movement while held.
    if ((!was_pressed || moved) && g_app) g_app->OnTouch(g_touch);
  } else if (g_touch.pressed) {
    g_touch.pressed = false;  // release edge at the last known position
    if (g_debug_input) {
      char line[48];
      snprintf(line, sizeof(line), "touch up x=%d y=%d", g_touch.x, g_touch.y);
      LogLine("[I] ", line);
    }
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
      if (g_debug_input) {
        static const char *kNames[] = {"boot", "power", "vol+", "vol-"};
        LogLine("[I] ", std::string("button ") + kNames[(int)b.id] +
                            (pressed ? " down" : " up"));
      }
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
    if (g_debug_input) LogLine("[I] ", "shake detected");
    if (g_app) g_app->OnShake();
  }
}

// ---------------------------------------------------------------------------
// Per-unit orientation: NVS persistence + live serial tuning.
//
// SKILL.md determined display orientation empirically on one reference unit
// and warns that other units differ (they do: rotated/mirrored panel batches
// exist). Instead of reflashing per guess, these serial commands tune the
// panel live and store the result in NVS namespace "cheeko_sys":
//
//   SHOW                  draw the orientation test pattern
//   L / R                 rotate the screen 90 deg left / right
//   M / F                 mirror left-right / flip top-bottom
//   C                     toggle red/blue color order (BGR bit)
//   MADCTL <hex>          set a raw MADCTL value directly
//   OFFSET <caset> <raset>  GRAM window offsets for rotated panels
//   TOUCHMAP <swap> <invx> <invy>  align touch with the display (0/1 each)
//   SAVE                  persist current values across reboots and flashes
//   INFO                  print current values
//
// Foolproof procedure: SHOW, press L until upright, M once if text is
// mirrored (then L again if needed), C if the RED square shows blue, SAVE.
// ---------------------------------------------------------------------------

void LoadOrientation() {
  Preferences p;
  if (!p.begin("cheeko_sys", true)) return;  // nothing saved yet: defaults
  cheeko_hw::g_madctl = (uint8_t)p.getInt("madctl", cheeko_hw::g_madctl);
  cheeko_hw::g_caset_offset = p.getInt("off_c", cheeko_hw::g_caset_offset);
  cheeko_hw::g_raset_offset = p.getInt("off_r", cheeko_hw::g_raset_offset);
  cheeko_hw::g_touch_swap_xy = p.getInt("t_swap", cheeko_hw::g_touch_swap_xy ? 1 : 0) != 0;
  cheeko_hw::g_touch_invert_x = p.getInt("t_invx", cheeko_hw::g_touch_invert_x ? 1 : 0) != 0;
  cheeko_hw::g_touch_invert_y = p.getInt("t_invy", cheeko_hw::g_touch_invert_y ? 1 : 0) != 0;
  p.end();
}

void PrintOrientation() {
  char line[96];
  snprintf(line, sizeof(line),
           "[I] MADCTL=0x%02X OFFSET=%d,%d TOUCHMAP=%d,%d,%d",
           cheeko_hw::g_madctl, cheeko_hw::g_caset_offset,
           cheeko_hw::g_raset_offset, cheeko_hw::g_touch_swap_xy ? 1 : 0,
           cheeko_hw::g_touch_invert_x ? 1 : 0,
           cheeko_hw::g_touch_invert_y ? 1 : 0);
  Serial.println(line);
}

void SaveOrientation() {
  Preferences p;
  if (!p.begin("cheeko_sys", false)) {
    Serial.println("[E] could not open NVS to save orientation");
    return;
  }
  p.putInt("madctl", (int)cheeko_hw::g_madctl);
  p.putInt("off_c", cheeko_hw::g_caset_offset);
  p.putInt("off_r", cheeko_hw::g_raset_offset);
  p.putInt("t_swap", cheeko_hw::g_touch_swap_xy ? 1 : 0);
  p.putInt("t_invx", cheeko_hw::g_touch_invert_x ? 1 : 0);
  p.putInt("t_invy", cheeko_hw::g_touch_invert_y ? 1 : 0);
  p.end();
  Serial.println("[I] orientation saved; it now survives reboots and reflashes");
}

void DrawTestPattern() {
  auto &d = cheeko::Cheeko().display();
  const int w = cheeko_hw::lcdWidth();
  const int h = cheeko_hw::lcdHeight();
  d.Clear(0x101820);
  d.FillRect(0, 0, 44, 44, 0xff0000);
  d.Text(6, 50, "RED");
  d.FillRect(w - 44, 0, 44, 44, 0x00ff00);
  d.Text(w - 42, 50, "GRN");
  d.FillRect(0, h - 44, 44, 44, 0x0000ff);
  d.Text(6, h - 64, "BLU");
  d.FillRect(w - 44, h - 44, 44, 44, 0xffffff);
  d.Text(w - 42, h - 64, "WHT");
  d.CenterText(h / 3, "ABC abc 123");
  char line[32];
  snprintf(line, sizeof(line), "%s %dx%d",
           cheeko_hw::lcdSwapped() ? "LANDSCAPE" : "PORTRAIT", w, h);
  d.CenterText(h / 3 + 26, line);
  snprintf(line, sizeof(line), "MADCTL 0x%02X", cheeko_hw::g_madctl);
  d.CenterText(128, line);
  snprintf(line, sizeof(line), "OFFSET %d %d",
           cheeko_hw::g_caset_offset, cheeko_hw::g_raset_offset);
  d.CenterText(148, line);
  d.CenterText(190, "buttons must be UP");
  d.CenterText(210, "RED top-left");
  PrintOrientation();
}

// The 8 MADCTL orientations form the square's symmetry group. To make L/R/M/F
// behave like real rotations/mirrors of what the user SEES (L twice = 180,
// not back-to-start), compose 2x2 transform matrices instead of XORing bits.
struct Mat2 {
  int a, b, c, d;  // [[a, b], [c, d]]
};

Mat2 MadctlToMatrix(uint8_t v) {
  // Displayed = Mirror(MX, MY) applied after the optional MV transpose.
  Mat2 m = (v & 0x20) ? Mat2{0, 1, 1, 0} : Mat2{1, 0, 0, 1};
  if (v & 0x40) { m.a = -m.a; m.b = -m.b; }  // MX
  if (v & 0x80) { m.c = -m.c; m.d = -m.d; }  // MY
  return m;
}

uint8_t MatrixToMadctl(Mat2 m, bool bgr) {
  uint8_t v = bgr ? 0x08 : 0x00;
  if (m.a != 0) {  // no transpose component
    if (m.a < 0) v |= 0x40;
    if (m.d < 0) v |= 0x80;
  } else {
    v |= 0x20;
    if (m.b < 0) v |= 0x40;
    if (m.c < 0) v |= 0x80;
  }
  return v;
}

Mat2 Mul(Mat2 x, Mat2 y) {
  return Mat2{x.a * y.a + x.b * y.c, x.a * y.b + x.b * y.d,
              x.c * y.a + x.d * y.c, x.c * y.b + x.d * y.d};
}

void ApplyVisualTransform(Mat2 t) {
  uint8_t v = cheeko_hw::g_madctl;
  Mat2 next = Mul(t, MadctlToMatrix(v));
  cheeko_hw::lcdSetMadctl(MatrixToMadctl(next, (v & 0x08) != 0));
  g_touch_echo = true;
  DrawTestPattern();
}

void PollSerialTuning() {
  static char line[96];
  static size_t len = 0;
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (len < sizeof(line) - 1) line[len++] = c;
      continue;
    }
    line[len] = '\0';
    len = 0;
    if (line[0] == '\0') continue;

    if (strcmp(line, "L") == 0) {
      ApplyVisualTransform(Mat2{0, -1, 1, 0});
    } else if (strcmp(line, "R") == 0) {
      ApplyVisualTransform(Mat2{0, 1, -1, 0});
    } else if (strcmp(line, "M") == 0) {
      ApplyVisualTransform(Mat2{-1, 0, 0, 1});
    } else if (strcmp(line, "F") == 0) {
      ApplyVisualTransform(Mat2{1, 0, 0, -1});
    } else if (strcmp(line, "C") == 0) {
      cheeko_hw::lcdSetMadctl(cheeko_hw::g_madctl ^ 0x08);
      DrawTestPattern();
    } else if (strncmp(line, "MADCTL ", 7) == 0) {
      cheeko_hw::lcdSetMadctl((uint8_t)strtol(line + 7, nullptr, 16));
      g_touch_echo = true;
      DrawTestPattern();
    } else if (strncmp(line, "OFFSET ", 7) == 0) {
      int off_c = 0, off_r = 0;
      if (sscanf(line + 7, "%d %d", &off_c, &off_r) == 2) {
        cheeko_hw::g_caset_offset = off_c;
        cheeko_hw::g_raset_offset = off_r;
        cheeko_hw::lcdClearGram();  // stale pixels outside the shifted window
        DrawTestPattern();
      }
    } else if (strncmp(line, "TOUCHMAP ", 9) == 0) {
      int swap_xy = 0, inv_x = 0, inv_y = 0;
      if (sscanf(line + 9, "%d %d %d", &swap_xy, &inv_x, &inv_y) == 3) {
        cheeko_hw::g_touch_swap_xy = swap_xy != 0;
        cheeko_hw::g_touch_invert_x = inv_x != 0;
        cheeko_hw::g_touch_invert_y = inv_y != 0;
        g_touch_echo = true;
        Serial.println("[I] touch map updated; tap the corners to verify");
        PrintOrientation();
      }
    } else if (strncmp(line, "WIFI ", 5) == 0) {
      const char *sep = strchr(line + 5, '|');
      if (!sep) {
        Serial.println("[W] usage: WIFI <ssid>|<password>");
      } else {
        const std::string ssid(line + 5, sep - (line + 5));
        Preferences sys;
        if (sys.begin("cheeko_sys", false)) {
          sys.putString("wifi_ssid", ssid.c_str());
          sys.putString("wifi_pass", sep + 1);
          sys.end();
          LogLine("[I] ", "wifi credentials saved for '" + ssid +
                              "'; apps join on their next Connect()");
        } else {
          Serial.println("[E] could not open NVS to save wifi credentials");
        }
      }
    } else if (strcmp(line, "SHOW") == 0) {
      g_touch_echo = true;
      DrawTestPattern();
    } else if (strcmp(line, "SAVE") == 0) {
      SaveOrientation();
    } else if (strcmp(line, "INFO") == 0) {
      PrintOrientation();
    } else if (strcmp(line, "SCAN") == 0) {
      Serial.println("[I] scanning (the radio is 2.4GHz-only; 5GHz networks are invisible)...");
      WiFi.mode(WIFI_STA);
      WiFi.disconnect();  // an in-progress connect attempt makes scans return nothing
      delay(150);
      int found = WiFi.scanNetworks();
      for (int i = 0; i < found; ++i) {
        char row[96];
        snprintf(row, sizeof(row), "[I]   '%s'  ch%d  %ddBm  %s",
                 WiFi.SSID(i).c_str(), WiFi.channel(i), (int)WiFi.RSSI(i),
                 WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "secured");
        Serial.println(row);
      }
      if (found <= 0) Serial.println("[W]   no networks visible");
      WiFi.scanDelete();
    } else if (strcmp(line, "STATUS") == 0) {
      char row[120];
      const unsigned s = (unsigned)(millis() / 1000u);
      snprintf(row, sizeof(row),
               "uptime %02u:%02u:%02u | heap %uKB free (min %uKB)", s / 3600u,
               (s / 60u) % 60u, s % 60u,
               (unsigned)(ESP.getFreeHeap() / 1024u),
               (unsigned)(ESP.getMinFreeHeap() / 1024u));
      LogLine("[I] ", row);
      if (WiFi.status() == WL_CONNECTED) {
        snprintf(row, sizeof(row), "wifi '%s' | ip %s | %ddBm",
                 WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                 (int)WiFi.RSSI());
        LogLine("[I] ", row);
      } else {
        LogLine("[W] ", "wifi not connected");
      }
      snprintf(row, sizeof(row),
               "audio %s | accel %s | storage %s | input debug %s",
               g_audio_ok ? "ok" : "DOWN", g_accel_ok ? "ok" : "DOWN",
               g_prefs_ok ? "ok" : "DOWN", g_debug_input ? "on" : "off");
      LogLine("[I] ", row);
    } else if (strcmp(line, "DEBUG") == 0) {
      g_debug_input = !g_debug_input;
      LogLine("[I] ", std::string("input debug ") +
                          (g_debug_input ? "on (touch/buttons/shake will log)"
                                         : "off"));
    } else {
      Serial.println(
          "[W] commands: STATUS | DEBUG | SHOW | L | R | M | F | C | SAVE | "
          "INFO | WIFI <ssid>|<pass> | SCAN | MADCTL <hex> | OFFSET <c> <r> | "
          "TOUCHMAP <s> <ix> <iy>");
      Serial.println(
          "[W] procedure: SHOW, L until upright, M if mirrored, "
          "C if RED shows blue, then SAVE");
    }
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

  // Per-unit panel orientation from NVS must be loaded before the panel
  // is initialised (lcdInit writes MADCTL).
  LoadOrientation();

  cheeko_hw::busInit();
  cheeko_hw::lcdInit();
  Serial.println("[I] wrong screen orientation? type SHOW in the serial monitor");

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
  PollSerialTuning();  // always live, in every app (SKILL.md section 10)
  PollTouch();
  PollButtons();
  PollMotion(now);
  if (g_amp_on && g_amp_off_ms != 0 && now >= g_amp_off_ms) {
    g_amp_off_ms = 0;   // audio has been quiet long enough; cut the amp
    g_amp_on = false;
    cheeko_hw::ampSet(false);
  }
  if (g_app) g_app->OnTick(now);
}

}  // namespace cheeko_rt

// ===========================================================================
// cheeko.h implementation
// ===========================================================================

namespace cheeko {

// ---- Display --------------------------------------------------------------

int Display::Width() { return cheeko_hw::lcdWidth(); }

int Display::Height() { return cheeko_hw::lcdHeight(); }

void Display::Clear(uint32_t rgb) {
  g_bg_rgb = rgb;
  cheeko_hw::lcdFillRect(0, 0, cheeko_hw::lcdWidth(), cheeko_hw::lcdHeight(),
                         To565(rgb));
}

void Display::Text(int x, int y, const std::string &text) {
  // Fixed ink-on-background rendering: no framebuffer means every pixel of
  // the glyph cell is written, so the cell background is the last Clear()
  // colour. Glyph cells that do not fit entirely on screen are skipped.
  const uint16_t fg = To565(Color::Ink);
  const uint16_t bg = To565(g_bg_rgb);
  int cx = x;
  for (size_t i = 0; i < text.size(); ++i) {
    if (cx >= 0 && cx + kGlyphCellW <= cheeko_hw::lcdWidth() && y >= 0 &&
        y + kGlyphCellH <= cheeko_hw::lcdHeight()) {
      DrawGlyph2x(cx, y, text[i], fg, bg);
    }
    cx += kGlyphCellW;  // 12px advance
  }
}

void Display::CenterText(int y, const std::string &text) {
  int w = (int)text.size() * kGlyphCellW;
  int x = (cheeko_hw::lcdWidth() - w) / 2;
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

// A failed boot-time init shouldn't mute the device forever (the codec can
// NACK transiently); when sound is requested, re-attempt init at most once
// every 5 seconds until it sticks.
static bool AudioReady() {
  static uint32_t retry_after_ms = 0;
  if (g_audio_ok) return true;
  if (millis() < retry_after_ms) return false;
  retry_after_ms = millis() + 5000;
  g_audio_ok = cheeko_hw::audioInit();
  Serial.println(g_audio_ok ? "[I] audio recovered on re-init"
                            : "[W] audio still down (ES8311 not responding)");
  return g_audio_ok;
}

void Speaker::Tone(int frequency_hz, int duration_ms) {
  if (!AudioReady()) {
    Serial.println("[W] Tone ignored: audio init failed");
    return;
  }
  if (frequency_hz <= 0 || duration_ms <= 0 || g_volume <= 0) return;
  int amplitude = kMaxToneAmplitude * g_volume / 100;
  AmpWarm();
  cheeko_hw::audioPlaySquare((float)frequency_hz, duration_ms, amplitude);
  AmpRelease();
}

void Speaker::PlayPcm(const int16_t *samples, size_t sample_count,
                      int sample_rate_hz) {
  if (samples == nullptr || sample_count == 0 || g_volume <= 0 ||
      !AudioReady()) {
    return;
  }
  if (sample_rate_hz != (int)AUDIO_SAMPLE_RATE) {
    LogLine("[W] ", "PlayPcm: clip rate differs from the I2S rate; pitch will shift");
  }
  AmpWarm();
  int16_t frame[64 * 2];
  size_t written = 0;
  for (size_t off = 0; off < sample_count; off += 64) {
    const size_t n = sample_count - off < 64 ? sample_count - off : 64;
    for (size_t i = 0; i < n; ++i) {
      const int32_t s = (int32_t)samples[off + i] * g_volume / 100;
      frame[i * 2] = (int16_t)s;      // duplicate mono -> stereo frames
      frame[i * 2 + 1] = (int16_t)s;
    }
    i2s_write(I2S_NUM_0, frame, n * 2 * sizeof(int16_t), &written, portMAX_DELAY);
  }
  AmpRelease();
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

// https:// URLs need a TLS transport. Certificate validation is skipped (no
// CA store in runtime v1) — fine for a dev toy, not for anything sensitive.
bool BeginRequest(HTTPClient &http, const std::string &url) {
  static WiFiClientSecure tls;
  if (url.compare(0, 8, "https://") == 0) {
    tls.setInsecure();
    return http.begin(tls, String(url.c_str()));
  }
  return http.begin(String(url.c_str()));
}

void Cloud::GetJson(const std::string &url) {
  if (WiFi.status() != WL_CONNECTED) {
    // Polling apps hit this every few seconds when Wi-Fi is down; once per
    // half-minute is enough to say so.
    static uint32_t last_warn_ms = 0;
    if (last_warn_ms == 0 || millis() - last_warn_ms >= 30000u) {
      last_warn_ms = millis();
      LogLine("[W] ", "GET skipped: Wi-Fi not connected");
    }
    return;
  }
  HTTPClient http;
  if (!BeginRequest(http, url)) {
    LogLine("[E] ", "Cloud::GetJson: bad URL: " + url);
    return;
  }
  const uint32_t started_ms = millis();
  int code = http.GET();
  if (code > 0) {
    String body = http.getString();
    LogFetch("GET", url, code, (int)body.length(), millis() - started_ms);
    DispatchCloudText(std::string(body.c_str()));
  } else {
    LogLine("[E] ", "GET " + UrlBrief(url) + " failed, client error " +
                        std::to_string(code));
  }
  http.end();
}

void Cloud::PostJson(const std::string &url, const std::string &json) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[W] Cloud::PostJson: Wi-Fi not connected");
    return;
  }
  HTTPClient http;
  if (!BeginRequest(http, url)) {
    LogLine("[E] ", "Cloud::PostJson: bad URL: " + url);
    return;
  }
  http.addHeader("Content-Type", "application/json");
  const uint32_t started_ms = millis();
  int code = http.POST(String(json.c_str()));
  if (code > 0) {
    String body = http.getString();
    // Posts are app-initiated actions, so they always log.
    LogLine(code < 400 ? "[I] " : "[W] ",
            "POST " + UrlBrief(url) + " -> HTTP " + std::to_string(code) +
                ", " + std::to_string((int)body.length()) + "B in " +
                std::to_string(millis() - started_ms) + "ms");
    DispatchCloudText(std::string(body.c_str()));
  } else {
    LogLine("[E] ", "POST " + UrlBrief(url) + " failed, client error " +
                        std::to_string(code));
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
  static bool reason_hooked = false;
  if (!reason_hooked) {
    reason_hooked = true;
    // Reason codes: 201 = no AP found (wrong SSID, or a 5GHz-only network this
    // 2.4GHz radio cannot see), 15/202/205 = bad password / auth failure.
    WiFi.onEvent(
        [](WiFiEvent_t, WiFiEventInfo_t info) {
          Serial.print("[W] Wifi: disconnected, reason=");
          Serial.println((int)info.wifi_sta_disconnected.reason);
        },
        ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  }
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
    delay(100);  // bounded wait: 8s, then give up without blocking the app
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[I] Wifi::Connect: connected, ip=");
    Serial.println(WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[W] Wifi::Connect: timed out after 8s (SCAN lists visible networks)");
  }
}

bool Wifi::IsConnected() const { return WiFi.status() == WL_CONNECTED; }

int Wifi::Scan(WifiNetwork *out, int max_count) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();  // an in-progress connect attempt makes scans return nothing
  delay(120);
  const int found = WiFi.scanNetworks();
  int count = 0;
  for (int i = 0; i < found && count < max_count; ++i) {
    if (WiFi.SSID(i).length() == 0) continue;  // skip hidden networks
    out[count].ssid = WiFi.SSID(i).c_str();
    out[count].rssi = (int)WiFi.RSSI(i);
    out[count].secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    ++count;
  }
  WiFi.scanDelete();
  Serial.printf("[I] Wifi::Scan: %d networks\n", count);
  return count;
}

void Wifi::SetCredentials(const std::string &ssid, const std::string &password) {
  Preferences sys;
  if (sys.begin("cheeko_sys", false)) {
    sys.putString("wifi_ssid", ssid.c_str());
    sys.putString("wifi_pass", password.c_str());
    sys.end();
    Serial.print("[I] Wifi::SetCredentials: saved for '");
    Serial.print(ssid.c_str());
    Serial.println("'");
  } else {
    Serial.println("[E] Wifi::SetCredentials: could not open NVS");
  }
  Connect();  // join right away with the new credentials
}

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
