#include "cheeko.h"

using namespace cheeko;

class CheekoPetApp : public CheekoApp {
 public:
  void OnStart() override {
    mood_ = Cheeko().storage().GetInt("mood", 60);
    energy_ = Cheeko().storage().GetInt("energy", 60);
    stars_ = Cheeko().storage().GetInt("stars", 0);
    DrawFace(false);
  }

  void OnTick(uint32_t uptime_ms) override {
    if (uptime_ms - last_decay_ms_ > 15 * 1000) {
      last_decay_ms_ = uptime_ms;
      mood_ = Clamp(mood_ - 1);
      energy_ = Clamp(energy_ - 1);
      SaveStats();
      if (showing_stats_) DrawStats();
    }
    if (showing_stats_) return;
    const bool blink = uptime_ms % 3600 > 3440;
    DrawFace(blink);
  }

  void OnTouch(const TouchEvent& event) override {
    if (!event.pressed) return;
    energy_ = Clamp(energy_ + 12);
    Cheeko().speaker().Tone(1040, 80);
    AwardStar();
    SaveStats();
    if (showing_stats_) DrawStats();
  }

  void OnShake() override {
    mood_ = Clamp(mood_ + 10);
    energy_ = Clamp(energy_ - 6);
    Cheeko().speaker().Tone(880, 60);
    AwardStar();
    SaveStats();
    if (showing_stats_) DrawStats();
  }

  void OnButton(const ButtonEvent& event) override {
    if (!event.pressed) return;
    if (event.button != ButtonEvent::Button::VolumeUp &&
        event.button != ButtonEvent::Button::VolumeDown) {
      return;
    }
    showing_stats_ = !showing_stats_;
    if (showing_stats_) {
      DrawStats();
    } else {
      DrawFace(false);
    }
  }

 private:
  enum class PetState { Happy, Neutral, Sleepy };

  PetState State() const {
    if (energy_ < 30) return PetState::Sleepy;
    if (mood_ >= 70 && energy_ >= 40) return PetState::Happy;
    return PetState::Neutral;
  }

  // Laid out around the screen center so it fits portrait (240x296) and
  // landscape-mounted (296x240) units alike.
  void DrawFace(bool blink) {
    auto& display = Cheeko().display();
    const int cx = display.Width() / 2;
    const int cy = display.Height() / 2 - 20;
    const PetState state = State();
    const uint32_t bg = state == PetState::Sleepy ? 0x0b0f1c : 0x101820;
    const uint32_t body = state == PetState::Happy
                              ? Color::Amber
                              : state == PetState::Neutral ? Color::Orange
                                                           : 0x8a93b8;
    display.Clear(bg);
    display.FillCircle(cx, cy, 78, body);
    display.FillCircle(cx - 34, cy - 12, 34, Color::White);
    display.FillCircle(cx + 34, cy - 12, 34, Color::White);

    if (state == PetState::Sleepy) {
      display.Line(cx - 48, cy - 14, cx - 20, cy - 14, Color::Black);
      display.Line(cx + 20, cy - 14, cx + 48, cy - 14, Color::Black);
      display.Text(cx + 56, cy - 72, "zzz");
    } else if (blink) {
      display.Line(cx - 46, cy - 14, cx - 22, cy - 14, Color::Black);
      display.Line(cx + 22, cy - 14, cx + 46, cy - 14, Color::Black);
    } else {
      display.FillCircle(cx - 34, cy - 14, 9, Color::Black);
      display.FillCircle(cx + 34, cy - 14, 9, Color::Black);
      display.FillCircle(cx - 30, cy - 18, 3, Color::White);
      display.FillCircle(cx + 38, cy - 18, 3, Color::White);
    }

    if (state == PetState::Happy) {
      display.FillCircle(cx - 42, cy + 28, 8, Color::Pink);
      display.FillCircle(cx + 42, cy + 28, 8, Color::Pink);
      display.Line(cx - 16, cy + 42, cx, cy + 54, Color::Black);
      display.Line(cx, cy + 54, cx + 16, cy + 42, Color::Black);
    } else if (state == PetState::Neutral) {
      display.Line(cx - 14, cy + 48, cx + 14, cy + 48, Color::Black);
    } else {
      display.FillCircle(cx, cy + 52, 6, Color::Black);
    }

    display.CenterText(display.Height() - 56, StateLabel(state));
    display.CenterText(display.Height() - 28, "tap:feed shake:play");
  }

  void DrawStats() {
    auto& display = Cheeko().display();
    const int x0 = (display.Width() - 192) / 2;
    display.Clear(0x101820);
    display.CenterText(32, "Cheeko Stats");
    DrawBar(84, "mood", mood_, Color::Mint);
    DrawBar(148, "energy", energy_, Color::Amber);
    display.Text(x0, 202, "stars: " + std::to_string(stars_));
    for (int i = 0; i < stars_ && i < 8; ++i) {
      display.FillCircle(x0 + 100 + i * 14, 208, 5, Color::Amber);
    }
    display.CenterText(display.Height() - 28, "volume: back to face");
  }

  void DrawBar(int y, const std::string& label, int value, uint32_t rgb) {
    auto& display = Cheeko().display();
    const int x0 = (display.Width() - 192) / 2;
    display.Text(x0, y - 20, label);
    display.Rect(x0, y, 192, 18, Color::Ink);
    if (value > 0) {
      display.FillRect(x0 + 2, y + 2, value * 188 / 100, 14, rgb);
    }
  }

  // One star per climb: re-arms only after a stat falls below 50.
  void AwardStar() {
    if (mood_ < 50 || energy_ < 50) {
      star_armed_ = true;
      return;
    }
    if (mood_ < 90 || energy_ < 90 || !star_armed_) return;
    star_armed_ = false;
    stars_++;
    Cheeko().speaker().Tone(1560, 120);
  }

  void SaveStats() {
    Cheeko().storage().PutInt("mood", mood_);
    Cheeko().storage().PutInt("energy", energy_);
    Cheeko().storage().PutInt("stars", stars_);
  }

  std::string StateLabel(PetState state) const {
    if (state == PetState::Happy) return "happy";
    if (state == PetState::Sleepy) return "sleepy...";
    return "ok";
  }

  static int Clamp(int value) {
    return value < 0 ? 0 : value > 100 ? 100 : value;
  }

  int mood_ = 60;
  int energy_ = 60;
  int stars_ = 0;
  bool showing_stats_ = false;
  bool star_armed_ = true;
  uint32_t last_decay_ms_ = 0;
};

CHEEKO_APP(CheekoPetApp);
