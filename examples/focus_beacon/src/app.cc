#include "cheeko.h"

using namespace cheeko;

class FocusBeaconApp : public CheekoApp {
 public:
  void OnStart() override {
    session_started_ms_ = 0;
    Draw("Focus", "25:00");
  }

  void OnTick(uint32_t uptime_ms) override {
    if (session_started_ms_ == 0) {
      session_started_ms_ = uptime_ms;
    }

    const uint32_t elapsed = uptime_ms - session_started_ms_;
    if (elapsed >= kFocusMs) {
      Draw("Break", "stand up");
      Cheeko().speaker().Tone(660, 140);
      return;
    }

    const uint32_t remaining_s = (kFocusMs - elapsed) / 1000;
    if (remaining_s != last_remaining_s_) {
      last_remaining_s_ = remaining_s;
      const uint32_t minutes = remaining_s / 60;
      const uint32_t seconds = remaining_s % 60;
      Draw("Focus", TwoDigits(minutes) + ":" + TwoDigits(seconds));
    }
  }

  void OnTouch(const TouchEvent& event) override {
    if (event.pressed) {
      session_started_ms_ = 0;
      last_remaining_s_ = 0;
      Draw("Focus", "25:00");
    }
  }

 private:
  void Draw(const std::string& title, const std::string& value) {
    Cheeko().display().Clear(0x080d12);
    Cheeko().display().CenterText(42, title);
    Cheeko().display().FillCircle(120, 132, 46, Color::Mint);
    Cheeko().display().FillCircle(104, 122, 5, Color::Black);
    Cheeko().display().FillCircle(136, 122, 5, Color::Black);
    Cheeko().display().Line(104, 150, 136, 150, Color::Black);
    Cheeko().display().CenterText(224, value);
  }

  std::string TwoDigits(uint32_t value) {
    return value < 10 ? "0" + std::to_string(value) : std::to_string(value);
  }

  static constexpr uint32_t kFocusMs = 25 * 60 * 1000;
  uint32_t session_started_ms_ = 0;
  uint32_t last_remaining_s_ = 0;
};

CHEEKO_APP(FocusBeaconApp);
