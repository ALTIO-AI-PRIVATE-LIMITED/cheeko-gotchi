#pragma once

// Cheeko Gotchi App SDK — public application API.
//
// This header is the stable contract that developer apps code against. The
// base firmware owns all hardware (display, audio, motion, power, OTA, Wi-Fi);
// apps only ever see the types below. Low-level hardware details never leak
// into app code.
//
// A typical app is one class deriving from CheekoApp, registered with the
// CHEEKO_APP() macro. Everything an app can do hangs off the Cheeko() runtime.
//
// Stability tiers (see docs/sdk/api-reference.md):
//   [Stable]        Frozen for the whole 0.x series. Safe to build products on.
//   [Experimental]  Shape may still change before 1.0. Usable now, but pin the
//                   SDK version (CHEEKO_SDK_VERSION_STRING) if you depend on it.

#include <cstddef>
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// SDK version. Apps can check these at compile time; the runtime reports the
// same string so a device can refuse an app built against an incompatible SDK.
// ---------------------------------------------------------------------------
#define CHEEKO_SDK_VERSION_MAJOR 0
#define CHEEKO_SDK_VERSION_MINOR 1
#define CHEEKO_SDK_VERSION_PATCH 0
#define CHEEKO_SDK_VERSION_STRING "0.1.0"

namespace cheeko {

// ===========================================================================
// Input events and data types
// ===========================================================================

struct TouchEvent {  // [Stable]
  int x = 0;
  int y = 0;
  bool pressed = false;
};

struct ButtonEvent {  // [Stable]
  enum class Button { Boot, Power, VolumeUp, VolumeDown };
  Button button;
  bool pressed = false;
};

// Accelerometer reading in g (1.0 == one gravity). Sourced from the on-board
// accelerometer. [Experimental]
struct MotionSample {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct AudioFrame {  // [Stable]
  const int16_t* samples = nullptr;
  size_t sample_count = 0;
  int sample_rate_hz = 24000;
  int channels = 1;
};

struct Color {  // [Stable]
  static constexpr uint32_t Black = 0x000000;
  static constexpr uint32_t White = 0xffffff;
  static constexpr uint32_t Ink = 0xf5f8fb;
  static constexpr uint32_t Amber = 0xffd166;
  static constexpr uint32_t Orange = 0xff8d4d;
  static constexpr uint32_t Cyan = 0x48d5ff;
  static constexpr uint32_t Mint = 0x55e6b6;
  static constexpr uint32_t Pink = 0xff5fa2;
};

// ===========================================================================
// Subsystems — reached through the Cheeko() runtime, never constructed directly
// ===========================================================================

class Display {  // [Stable]
 public:
  void Clear(uint32_t rgb);
  void Text(int x, int y, const std::string& text);
  void CenterText(int y, const std::string& text);
  void Rect(int x, int y, int w, int h, uint32_t rgb);
  void FillRect(int x, int y, int w, int h, uint32_t rgb);
  void Circle(int x, int y, int radius, uint32_t rgb);
  void FillCircle(int x, int y, int radius, uint32_t rgb);
  void Line(int x1, int y1, int x2, int y2, uint32_t rgb);
  void Image(const std::string& path);
};

class Speaker {  // [Stable]
 public:
  void Play(const std::string& path);
  void Tone(int frequency_hz, int duration_ms);
  void SetVolume(int volume);  // 0..100
};

class Microphone {  // [Stable]
 public:
  void Start(int sample_rate_hz = 24000);
  void Stop();
};

// Poll the latest touch state outside of OnTouch(). [Stable]
class Touch {
 public:
  TouchEvent Get();    // Most recent touch state.
  bool IsPressed();    // Convenience: is the panel touched right now.
};

// On-board accelerometer. For tilt, shake, and orientation. The
// OnShake() callback covers the common case; Read() is for custom logic.
// [Experimental]
class Motion {
 public:
  MotionSample Read();  // Current acceleration in g.
  bool IsShaken();      // Edge-triggered: true once per detected shake.
};

// Key/value storage that survives reboot and OTA (NVS-backed in firmware).
// This is how a pet app keeps mood, energy, and stars between power cycles.
// [Stable]
class Storage {
 public:
  void PutInt(const std::string& key, int value);
  int GetInt(const std::string& key, int fallback = 0);
  void PutString(const std::string& key, const std::string& value);
  std::string GetString(const std::string& key, const std::string& fallback = "");
  bool Has(const std::string& key);
  void Remove(const std::string& key);
};

// Developer logging. Lines surface on the USB serial console and over the dev
// tools in tools/. Use this instead of reaching for printf. [Stable]
class Log {
 public:
  void Info(const std::string& message);
  void Warn(const std::string& message);
  void Error(const std::string& message);
};

class Cloud {  // [Stable]
 public:
  void Connect();
  void StartVoiceSession();
  void SendText(const std::string& text);
  void GetJson(const std::string& url);
  void PostJson(const std::string& url, const std::string& json);
};

class Wifi {  // [Stable]
 public:
  void Connect();
  bool IsConnected() const;
};

// ===========================================================================
// Runtime — the single entry point to every subsystem
// ===========================================================================

class CheekoRuntime {
 public:
  Display& display();
  Speaker& speaker();
  Microphone& mic();
  Touch& touch();      // [Stable]
  Motion& motion();    // [Experimental]
  Storage& storage();  // [Stable]
  Log& log();          // [Stable]
  Cloud& cloud();
  Wifi& wifi();
};

// ===========================================================================
// App contract — derive one class from this, register it with CHEEKO_APP()
// ===========================================================================

class CheekoApp {
 public:
  virtual ~CheekoApp() = default;
  virtual void OnStart() {}                              // App became active.
  virtual void OnStop() {}                               // Release resources.
  virtual void OnTick(uint32_t /*uptime_ms*/) {}         // Periodic; keep it short.
  virtual void OnTouch(const TouchEvent& /*event*/) {}   // Raw touch.
  virtual void OnButton(const ButtonEvent& /*event*/) {} // Boot/power/volume keys.
  virtual void OnShake() {}                              // [Experimental] device shaken.
  virtual void OnMicAudio(const AudioFrame& /*frame*/) {} // PCM after mic().Start().
  virtual void OnCloudText(const std::string& /*text*/) {} // Transcripts / replies.
};

CheekoRuntime& Cheeko();

}  // namespace cheeko

// Register the app entry point. Exactly one per app.
#define CHEEKO_APP(AppClass) \
  extern "C" cheeko::CheekoApp* CreateCheekoApp() { return new AppClass(); }
