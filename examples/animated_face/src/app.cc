#include "cheeko.h"

using namespace cheeko;

class AnimatedFaceApp : public CheekoApp {
 public:
  void OnStart() override {
    DrawFace(/*blink=*/false, /*mouth_open=*/false);
  }

  void OnTick(uint32_t uptime_ms) override {
    const bool blink = uptime_ms % 3600 > 3440;
    const bool mouth_open = talking_ && ((uptime_ms / 180) % 2 == 0);
    DrawFace(blink, mouth_open);
  }

  void OnTouch(const TouchEvent& event) override {
    if (!event.pressed) return;
    talking_ = !talking_;
    Cheeko().speaker().Tone(talking_ ? 880 : 440, 70);
  }

 private:
  void DrawFace(bool blink, bool mouth_open) {
    auto& display = Cheeko().display();
    display.Clear(0x101820);

    display.FillCircle(120, 120, 78, Color::Amber);
    display.FillCircle(86, 110, 38, Color::White);
    display.FillCircle(154, 110, 38, Color::White);
    display.FillCircle(120, 164, 46, 0xfff4cf);

    if (blink) {
      display.Line(74, 108, 98, 108, Color::Black);
      display.Line(142, 108, 166, 108, Color::Black);
    } else {
      display.FillCircle(86, 108, 10, Color::Black);
      display.FillCircle(154, 108, 10, Color::Black);
      display.FillCircle(90, 104, 3, Color::White);
      display.FillCircle(158, 104, 3, Color::White);
    }

    display.FillCircle(120, 146, 7, Color::Black);
    if (mouth_open) {
      display.FillCircle(120, 176, 14, Color::Black);
      display.FillRect(108, 166, 24, 10, 0xfff4cf);
    } else {
      display.Line(106, 174, 120, 184, Color::Black);
      display.Line(120, 184, 134, 174, Color::Black);
    }

    display.CenterText(264, talking_ ? "talking..." : "tap to talk");
  }

  bool talking_ = false;
};

CHEEKO_APP(AnimatedFaceApp);
