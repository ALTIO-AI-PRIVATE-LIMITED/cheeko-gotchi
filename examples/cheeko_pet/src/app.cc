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

  void DrawFace(bool blink) {
    auto& display = Cheeko().display();
    const PetState state = State();
    const uint32_t bg = state == PetState::Sleepy ? 0x0b0f1c : 0x101820;
    const uint32_t body = state == PetState::Happy
                              ? Color::Amber
                              : state == PetState::Neutral ? Color::Orange
                                                           : 0x8a93b8;
    display.Clear(bg);
    display.FillCircle(120, 128, 78, body);
    display.FillCircle(86, 116, 34, Color::White);
    display.FillCircle(154, 116, 34, Color::White);

    if (state == PetState::Sleepy) {
      display.Line(72, 114, 100, 114, Color::Black);
      display.Line(140, 114, 168, 114, Color::Black);
      display.Text(176, 56, "zzz");
    } else if (blink) {
      display.Line(74, 114, 98, 114, Color::Black);
      display.Line(142, 114, 166, 114, Color::Black);
    } else {
      display.FillCircle(86, 114, 9, Color::Black);
      display.FillCircle(154, 114, 9, Color::Black);
      display.FillCircle(90, 110, 3, Color::White);
      display.FillCircle(158, 110, 3, Color::White);
    }

    if (state == PetState::Happy) {
      display.FillCircle(78, 156, 8, Color::Pink);
      display.FillCircle(162, 156, 8, Color::Pink);
      display.Line(104, 170, 120, 182, Color::Black);
      display.Line(120, 182, 136, 170, Color::Black);
    } else if (state == PetState::Neutral) {
      display.Line(106, 176, 134, 176, Color::Black);
    } else {
      display.FillCircle(120, 180, 6, Color::Black);
    }

    display.CenterText(240, StateLabel(state));
    display.CenterText(268, "tap:feed shake:play");
  }

  void DrawStats() {
    auto& display = Cheeko().display();
    display.Clear(0x101820);
    display.CenterText(32, "Cheeko Stats");
    DrawBar(84, "mood", mood_, Color::Mint);
    DrawBar(148, "energy", energy_, Color::Amber);
    display.Text(24, 202, "stars: " + std::to_string(stars_));
    for (int i = 0; i < stars_ && i < 8; ++i) {
      display.FillCircle(124 + i * 14, 208, 5, Color::Amber);
    }
    display.CenterText(268, "volume: back to face");
  }

  void DrawBar(int y, const std::string& label, int value, uint32_t rgb) {
    auto& display = Cheeko().display();
    display.Text(24, y - 20, label);
    display.Rect(24, y, 192, 18, Color::Ink);
    if (value > 0) {
      display.FillRect(26, y + 2, value * 188 / 100, 14, rgb);
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
