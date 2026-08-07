// Self Test — the first app every developer runs on a new Cheeko Gotchi.
//
// A guided, tap-to-advance walk through every hardware block, written entirely
// against the public SDK (sdk/include/cheeko.h). It doubles as a worked example
// of good app structure: a small state machine, periodic work kept short, no
// hardware access, and results persisted with storage() and log().
//
// Flow:  Intro -> Display -> Touch -> Speaker -> Mic -> Wi-Fi -> Done
// Tap to move forward at each step. On the speaker step, VOL+ replays the tone.

#include "cheeko.h"

#include <algorithm>
#include <cmath>
#include <string>

using namespace cheeko;

namespace {
constexpr int kScreenW = 240;
constexpr int kTouchHitsToPass = 5;     // taps required on the touch step
constexpr int kMicPassPercent = 25;     // level that counts as "mic works"
constexpr uint32_t kWifiTimeoutMs = 12000;
}  // namespace

class SelfTestApp : public CheekoApp {
 public:
  enum class Stage { Intro, Display, Touch, Speaker, Mic, Wifi, Done };

  void OnStart() override {
    Cheeko().log().Info("self-test: start");
    EnterStage(Stage::Intro);
  }

  void OnTick(uint32_t uptime_ms) override {
    now_ms_ = uptime_ms;
    switch (stage_) {
      case Stage::Mic:  TickMic();  break;
      case Stage::Wifi: TickWifi(); break;
      default: break;
    }
  }

  void OnTouch(const TouchEvent& event) override {
    if (!event.pressed) return;
    switch (stage_) {
      case Stage::Intro:   EnterStage(Stage::Display); break;
      case Stage::Display: EnterStage(Stage::Touch);   break;
      case Stage::Touch:   RegisterTouchHit(event);    break;
      case Stage::Speaker: EnterStage(Stage::Mic);     break;
      case Stage::Mic:     EnterStage(Stage::Wifi);    break;  // tap = skip
      case Stage::Wifi:    EnterStage(Stage::Done);    break;  // tap = skip
      case Stage::Done:    EnterStage(Stage::Intro);   break;  // run again
    }
  }

  void OnButton(const ButtonEvent& event) override {
    if (stage_ == Stage::Speaker && event.pressed &&
        event.button == ButtonEvent::Button::VolumeUp) {
      PlayTestTone();
    }
  }

  void OnMicAudio(const AudioFrame& frame) override {
    if (stage_ != Stage::Mic || frame.samples == nullptr || frame.sample_count == 0) {
      return;
    }
    double sum = 0.0;
    for (size_t i = 0; i < frame.sample_count; ++i) {
      const double s = frame.samples[i] / 32768.0;
      sum += s * s;
    }
    const double rms = std::sqrt(sum / frame.sample_count);
    mic_percent_ = std::min(std::max(static_cast<int>(rms * 400.0), 0), 100);
  }

 private:
  // ---- Stage entry --------------------------------------------------------

  void EnterStage(Stage stage) {
    if (stage_ == Stage::Mic && stage != Stage::Mic) {
      Cheeko().mic().Stop();
    }
    stage_ = stage;
    stage_started_ms_ = now_ms_;

    switch (stage) {
      case Stage::Intro:   DrawIntro();          break;
      case Stage::Display: DrawColorBars();      break;
      case Stage::Touch:   touch_hits_ = 0;
                           DrawTouchPrompt();    break;
      case Stage::Speaker: DrawSpeaker();
                           PlayTestTone();       break;
      case Stage::Mic:     mic_percent_ = 0;
                           mic_passed_ = false;
                           Cheeko().mic().Start(24000);
                           DrawMic(0);           break;
      case Stage::Wifi:    wifi_done_ = false;
                           Cheeko().wifi().Connect();
                           DrawWifi("connecting...");  break;
      case Stage::Done:    Finish();             break;
    }
  }

  // ---- Per-stage drawing --------------------------------------------------

  void Header(int step, const std::string& title) {
    auto& d = Cheeko().display();
    d.Clear(0x0b1118);
    d.FillRect(0, 0, kScreenW, 34, 0x16202c);
    d.Text(10, 11, "SELF TEST");
    if (step > 0) d.Text(186, 11, std::to_string(step) + "/5");
    d.CenterText(50, title);
  }

  void DrawIntro() {
    Header(0, "");
    auto& d = Cheeko().display();
    d.FillCircle(120, 132, 52, Color::Amber);
    d.FillCircle(102, 122, 7, Color::Black);
    d.FillCircle(138, 122, 7, Color::Black);
    d.Line(104, 150, 136, 150, Color::Black);
    d.CenterText(214, "Cheeko Self Test");
    d.CenterText(250, "tap to begin");
  }

  void DrawColorBars() {
    Header(1, "DISPLAY");
    auto& d = Cheeko().display();
    // Red must look red, orange/yellow must not look blue, white must look white.
    struct Bar { uint32_t rgb; const char* name; uint32_t label; };
    const Bar bars[] = {
      {0xff0000, "RED",    Color::White},
      {Color::Orange, "ORANGE", Color::Black},
      {0xffe000, "YELLOW", Color::Black},
      {0x33cc66, "GREEN",  Color::Black},
      {Color::Cyan, "CYAN", Color::Black},
      {0x3366ff, "BLUE",   Color::White},
      {Color::White, "WHITE", Color::Black},
    };
    const int top = 70, h = 24;
    int y = top;
    for (const auto& b : bars) {
      d.FillRect(0, y, kScreenW, h, b.rgb);
      d.Text(12, y + 7, b.name);
      (void)b.label;
      y += h;
    }
    d.CenterText(252, "colors right? tap");
  }

  void DrawTouchPrompt() {
    Header(2, "TOUCH");
    auto& d = Cheeko().display();
    d.CenterText(96, "tap the screen");
    d.CenterText(126, std::to_string(touch_hits_) + " / " +
                          std::to_string(kTouchHitsToPass));
  }

  void DrawSpeaker() {
    Header(3, "SPEAKER");
    auto& d = Cheeko().display();
    d.FillCircle(120, 132, 40, Color::Mint);
    d.CenterText(196, "hear the tone?");
    d.CenterText(228, "VOL+ replay");
    d.CenterText(258, "tap = yes, next");
  }

  void DrawMic(int percent) {
    Header(4, "MIC");
    auto& d = Cheeko().display();
    d.CenterText(92, "speak or tap mic");
    // Level bar.
    const int bx = 30, by = 140, bw = kScreenW - 60, bh = 26;
    d.Rect(bx, by, bw, bh, Color::White);
    const int fill = std::min(std::max(percent, 0), 100) * (bw - 4) / 100;
    d.FillRect(bx + 2, by + 2, fill, bh - 4, Color::Cyan);
    d.CenterText(196, std::to_string(percent) + "%");
    d.CenterText(258, "tap to skip");
  }

  void DrawWifi(const std::string& status) {
    Header(5, "WI-FI");
    auto& d = Cheeko().display();
    d.CenterText(120, status);
    d.CenterText(258, "tap to skip");
  }

  // ---- Stage logic --------------------------------------------------------

  void RegisterTouchHit(const TouchEvent& event) {
    touch_hits_++;
    Cheeko().speaker().Tone(660, 30);
    auto& d = Cheeko().display();
    d.FillCircle(event.x, event.y, 8, Color::Pink);  // mark where they touched
    d.FillRect(0, 116, kScreenW, 24, 0x0b1118);
    d.CenterText(126, std::to_string(touch_hits_) + " / " +
                          std::to_string(kTouchHitsToPass));
    if (touch_hits_ >= kTouchHitsToPass) {
      Cheeko().log().Info("self-test: touch ok");
      EnterStage(Stage::Speaker);
    }
  }

  void PlayTestTone() {
    Cheeko().speaker().SetVolume(60);
    Cheeko().speaker().Tone(880, 180);
  }

  void TickMic() {
    DrawMic(mic_percent_);
    if (!mic_passed_ && mic_percent_ >= kMicPassPercent) {
      mic_passed_ = true;
      Cheeko().log().Info("self-test: mic ok");
      Cheeko().display().CenterText(228, "MIC OK");
    }
  }

  void TickWifi() {
    if (wifi_done_) return;
    if (Cheeko().wifi().IsConnected()) {
      wifi_done_ = true;
      wifi_ok_ = true;
      Cheeko().log().Info("self-test: wifi ok");
      DrawWifi("connected");
    } else if (now_ms_ - stage_started_ms_ > kWifiTimeoutMs) {
      wifi_done_ = true;
      wifi_ok_ = false;
      Cheeko().log().Warn("self-test: wifi not connected");
      DrawWifi("no wi-fi (tap)");
    }
  }

  void Finish() {
    Cheeko().storage().PutInt("self_test_passed", 1);
    Cheeko().storage().PutInt("self_test_wifi", wifi_ok_ ? 1 : 0);
    Cheeko().log().Info("self-test: done");

    auto& d = Cheeko().display();
    d.Clear(0x07140d);
    d.CenterText(60, "ALL DONE");
    d.FillCircle(120, 140, 46, Color::Mint);
    d.Line(100, 142, 114, 158, Color::Black);
    d.Line(114, 158, 144, 122, Color::Black);  // check mark
    d.CenterText(214, wifi_ok_ ? "wi-fi: ok" : "wi-fi: skipped");
    d.CenterText(258, "tap to run again");
  }

  Stage stage_ = Stage::Intro;
  uint32_t now_ms_ = 0;
  uint32_t stage_started_ms_ = 0;
  int touch_hits_ = 0;
  int mic_percent_ = 0;
  bool mic_passed_ = false;
  bool wifi_done_ = false;
  bool wifi_ok_ = false;
};

CHEEKO_APP(SelfTestApp);
