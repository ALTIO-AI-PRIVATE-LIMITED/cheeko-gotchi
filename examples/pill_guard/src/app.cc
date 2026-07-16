#include "cheeko.h"

using namespace cheeko;

class PillGuardApp : public CheekoApp {
 public:
  void OnStart() override {
    DrawIdle();
  }

  void OnTick(uint32_t uptime_ms) override {
    if (!dose_due_ && uptime_ms > 10 * 1000) {
      dose_due_ = true;
      reminded_at_ms_ = uptime_ms;
      Cheeko().speaker().Tone(740, 120);
      Cheeko().display().Clear(0x171018);
      Cheeko().display().CenterText(52, "Medicine time");
      Cheeko().display().CenterText(118, "BP pill");
      Cheeko().display().CenterText(206, "tap when done");
    }

    if (dose_due_ && !escalated_ && uptime_ms - reminded_at_ms_ > 5 * 60 * 1000) {
      escalated_ = true;
      Cheeko().cloud().SendText("Medication reminder missed. Please check in.");
      Cheeko().display().CenterText(250, "family notified");
    }
  }

  void OnTouch(const TouchEvent& event) override {
    if (!event.pressed || !dose_due_) return;
    dose_due_ = false;
    escalated_ = false;
    Cheeko().speaker().Tone(1040, 80);
    Cheeko().cloud().SendText("Dose confirmed.");
    DrawIdle();
  }

 private:
  void DrawIdle() {
    Cheeko().display().Clear(0x071018);
    Cheeko().display().CenterText(50, "Pill Guard");
    Cheeko().display().CenterText(128, "Next: 8:00 PM");
    Cheeko().display().CenterText(220, "all good");
  }

  bool dose_due_ = false;
  bool escalated_ = false;
  uint32_t reminded_at_ms_ = 0;
};

CHEEKO_APP(PillGuardApp);
