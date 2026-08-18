#include <cmath>
#include <string>

#include "cheeko.h"

#include "pomo_font.h"

using namespace cheeko;

namespace {

// Palette — dark cube-timer look: near-black body, amber-orange ring.
constexpr uint32_t kBg = 0x0b0c10;
constexpr uint32_t kInk = 0xf2f5f9;
constexpr uint32_t kOrange = 0xffa03c;   // focus ring
constexpr uint32_t kMint = 0x55e6b6;     // break ring
constexpr uint32_t kDim = 0x8b95a7;      // secondary text
constexpr uint32_t kHint = 0x596273;     // tertiary hints
constexpr uint32_t kPanel = 0x151922;
constexpr uint32_t kPanelEdge = 0x2b3547;
constexpr uint32_t kTickDim = 0x2a2e38;  // spent ring ticks
constexpr uint32_t kSegOff = 0x181b22;   // unlit LCD segments (ghost)
constexpr uint32_t kRedEdge = 0x8a3a42;
constexpr uint32_t kRedText = 0xe57373;
constexpr uint32_t kInkFaint = 0x9aa3b2;   // frozen digits on the pause screen
constexpr uint32_t kFlashInk = 0x10131a;   // text on the alarm's lit flash phase

// Tick ring, sized like the cube timer face: 60 ticks around the digits.
constexpr int kRingInner = 88;
constexpr int kRingOuter = 101;
constexpr int kTicks = 60;

// Seven-segment digit cell. Four cells render "MM:SS".
constexpr int kDigW = 30;
constexpr int kDigH = 52;
constexpr int kSegT = 6;
// Digit offsets within the MM:SS block (wider gap for the colon) and its width.
constexpr int kDigOff[4] = {0, 38, 84, 122};
constexpr int kDigSpanW = 152;
// A B C D E F G segment masks for digits 0-9.
constexpr uint8_t kSegMask[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66,
                                  0x6d, 0x7d, 0x07, 0x7f, 0x6f};

struct Preset {
  const char* name;
  int focus_s;
  int rest_s;
  uint32_t accent;
};

constexpr int kPresetCount = 4;
const Preset kPresets[kPresetCount] = {
    {"CLASSIC", 25 * 60, 5 * 60, kOrange},
    {"DEEP WORK", 50 * 60, 10 * 60, Color::Pink},
    {"QUICK", 15 * 60, 3 * 60, kMint},
    {"DEMO", 15, 10, Color::Amber},  // tiny preset for demos
};

// Menu rows: the four presets plus CUSTOM.
constexpr int kMenuRows = kPresetCount + 1;

// Orientation: whichever screen axis gravity dominates decides the rotation
// (0/90/180/270 degrees clockwise from upright). Threshold + debounce keep
// half-turns and wobble from triggering.
constexpr float kFlipThresholdG = 0.35f;
constexpr uint32_t kFlipDebounceMs = 250;

// Case-knock detection while the alarm rings: acceleration magnitude must
// deviate from gravity by this much. Speaker buzz and desk vibration stay
// well under it; a deliberate rap on the housing spikes past it.
constexpr float kKnockG = 0.35f;

constexpr float kPi = 3.14159265f;

struct Box {
  int x, y, w, h;
};

bool Hit(int x, int y, const Box& b) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

}  // namespace

class PomodoroApp : public CheekoApp {
 public:
  void OnStart() override {
    custom_focus_min_ = Cheeko().storage().GetInt("cust_f", 25);
    custom_rest_min_ = Cheeko().storage().GetInt("cust_r", 5);
    volume_ = Cheeko().storage().GetInt("vol", 70);
    muted_ = Cheeko().storage().GetInt("mute", 0) != 0;
    Cheeko().speaker().SetVolume(muted_ ? 0 : volume_);
    SynthTick(tick_pcm_, 850.0f);   // warm wood-block pair, tick above tock
    SynthTick(tock_pcm_, 640.0f);
    EnterMenu();
  }

  void OnTick(uint32_t uptime_ms) override {
    now_ = uptime_ms;
    if (now_ - last_motion_ms_ >= 40) {  // accel is an I2C read; don't spam it
      last_motion_ms_ = now_;
      PollOrientation();
    }
    if (vol_overlay_until_ != 0 && now_ >= vol_overlay_until_) {
      vol_overlay_until_ = 0;
      DrawCurrentScreen();  // wipe the volume overlay
    }
    switch (screen_) {
      case Screen::Run: TickRun(); break;
      case Screen::Alarm: TickAlarm(); break;
      default: break;
    }
  }

  void OnTouch(const TouchEvent& event) override {
    if (!event.pressed) return;
    int x = event.x, y = event.y;
    TouchToLogical(x, y);
    switch (screen_) {
      case Screen::Menu: if (!HandleMuteTap(x, y)) TouchMenu(x, y); break;
      case Screen::Custom: if (!HandleMuteTap(x, y)) TouchCustom(x, y); break;
      case Screen::Armed:
        if (!HandleMuteTap(x, y)) {
          Click();
          EnterMenu();
        }
        break;
      case Screen::Run: HandleMuteTap(x, y); break;  // other taps ignored
      case Screen::Paused: if (!HandleMuteTap(x, y)) TouchPaused(x, y); break;
      case Screen::Alarm: SilenceAlarm(); break;
    }
  }

  void OnButton(const ButtonEvent& event) override {
    if (!event.pressed) return;
    if (event.button == ButtonEvent::Button::VolumeUp) SetVol(volume_ + 10);
    else if (event.button == ButtonEvent::Button::VolumeDown) SetVol(volume_ - 10);
  }

  void OnShake() override {
    if (screen_ == Screen::Alarm) SilenceAlarm();  // a hard rap also counts
  }

 private:
  enum class Screen { Menu, Custom, Armed, Run, Paused, Alarm };

  // ---- Rotation-aware drawing layer ---------------------------------------
  // Everything draws in logical coordinates on a canvas that is 240x296 when
  // the device is upright or upside-down and 296x240 when it is on either
  // side. Each primitive maps through the current rotation, so the UI is
  // always upright for the user. Text goes through the bundled 5x7 font for
  // the same reason (the SDK's Text() cannot rotate).

  int PW() { return Cheeko().display().Width(); }   // physical panel
  int PH() { return Cheeko().display().Height(); }
  bool Land() { return orient_ == 90 || orient_ == 270; }
  int W() { return Land() ? PH() : PW(); }          // logical canvas
  int H() { return Land() ? PW() : PH(); }

  void MapPoint(int& x, int& y) {
    const int lx = x, ly = y;
    switch (orient_) {
      case 90:  x = ly; y = PH() - 1 - lx; break;
      case 180: x = PW() - 1 - lx; y = PH() - 1 - ly; break;
      case 270: x = PW() - 1 - ly; y = lx; break;
      default: break;
    }
  }

  void MapRect(int& x, int& y, int& w, int& h) {
    const int lx = x, ly = y, lw = w, lh = h;
    switch (orient_) {
      case 90:  x = ly; y = PH() - lx - lw; w = lh; h = lw; break;
      case 180: x = PW() - lx - lw; y = PH() - ly - lh; break;
      case 270: x = PW() - ly - lh; y = lx; w = lh; h = lw; break;
      default: break;
    }
  }

  void TouchToLogical(int& x, int& y) {
    const int px = x, py = y;
    switch (orient_) {
      case 90:  x = PH() - 1 - py; y = px; break;
      case 180: x = PW() - 1 - px; y = PH() - 1 - py; break;
      case 270: x = py; y = PW() - 1 - px; break;
      default: break;
    }
  }

  void PFillRect(int x, int y, int w, int h, uint32_t rgb) {
    MapRect(x, y, w, h);
    Cheeko().display().FillRect(x, y, w, h, rgb);
  }

  void PRect(int x, int y, int w, int h, uint32_t rgb) {
    MapRect(x, y, w, h);
    Cheeko().display().Rect(x, y, w, h, rgb);
  }

  void PLine(int x1, int y1, int x2, int y2, uint32_t rgb) {
    MapPoint(x1, y1);
    MapPoint(x2, y2);
    Cheeko().display().Line(x1, y1, x2, y2, rgb);
  }

  static int TextW(const std::string& text, int scale) {
    return text.empty() ? 0 : (static_cast<int>(text.size()) * 6 - 1) * scale;
  }

  void PText(int x, int y, const std::string& text, int scale, uint32_t rgb) {
    for (size_t i = 0; i < text.size(); ++i) {
      const uint8_t* glyph = pomofont::Glyph(text[i]);
      const int gx = x + static_cast<int>(i) * 6 * scale;
      for (int col = 0; col < 5; ++col) {
        const uint8_t bits = glyph[col];
        int row = 0;
        while (row < 7) {  // merge vertical bit runs into single rects
          if (bits & (1u << row)) {
            int run = 1;
            while (row + run < 7 && (bits & (1u << (row + run)))) ++run;
            PFillRect(gx + col * scale, y + row * scale, scale, run * scale, rgb);
            row += run;
          } else {
            ++row;
          }
        }
      }
    }
  }

  void PTextCentered(int cx, int y, const std::string& text, int scale,
                     uint32_t rgb) {
    PText(cx - TextW(text, scale) / 2, y, text, scale, rgb);
  }

  void PCenterText(int y, const std::string& text, int scale, uint32_t rgb) {
    PTextCentered(W() / 2, y, text, scale, rgb);
  }

  // ---- Seven-segment digits -----------------------------------------------

  void Seg(int x, int y, int seg, uint32_t rgb) {
    const int w = kDigW, h = kDigH, t = kSegT;
    const int vh = (h - 3 * t) / 2;  // vertical segment height
    switch (seg) {
      case 0: PFillRect(x + t, y, w - 2 * t, t, rgb); break;              // A
      case 1: PFillRect(x + w - t, y + t, t, vh, rgb); break;             // B
      case 2: PFillRect(x + w - t, y + (h + t) / 2, t, vh, rgb); break;   // C
      case 3: PFillRect(x + t, y + h - t, w - 2 * t, t, rgb); break;      // D
      case 4: PFillRect(x, y + (h + t) / 2, t, vh, rgb); break;           // E
      case 5: PFillRect(x, y + t, t, vh, rgb); break;                     // F
      case 6: PFillRect(x + t, y + (h - t) / 2, w - 2 * t, t, rgb); break;// G
    }
  }

  void DrawDigit(int x, int y, int digit, uint32_t on) {
    const uint8_t mask = kSegMask[digit];
    for (int s = 0; s < 7; ++s) Seg(x, y, s, (mask >> s) & 1 ? on : kSegOff);
  }

  void DrawTimeDigits(int x0, int y, int total_s, uint32_t on) {
    int mm = total_s / 60;
    if (mm > 99) mm = 99;
    const int ss = total_s % 60;
    const int digits[4] = {mm / 10, mm % 10, ss / 10, ss % 10};
    for (int i = 0; i < 4; ++i) DrawDigit(x0 + kDigOff[i], y, digits[i], on);
    // Colon between MM and SS, dots straddling the middle segment line.
    const int colon_x = x0 + kDigSpanW / 2 - kSegT / 2;
    PFillRect(colon_x, y + 13, kSegT, kSegT, on);
    PFillRect(colon_x, y + kDigH - 13 - kSegT, kSegT, kSegT, on);
  }

  // ---- Tick ring ----------------------------------------------------------
  // Tick 0 sits at 12 o'clock; the index runs clockwise. As time drains,
  // ticks go dark clockwise from the top, so the lit arc always ends at 12.
  // In landscape the ring shifts left to make room for a text column.

  int RingCx() { return Land() ? 110 : 120; }
  int RingCy() { return Land() ? 120 : 150; }
  int RingDigitsX() { return RingCx() - kDigSpanW / 2; }
  int RingDigitsY() { return RingCy() - kDigH / 2; }
  int SideCx() { return 253; }  // landscape text column, right of the ring

  void DrawTick(int i, uint32_t rgb) {
    const float a = static_cast<float>(i) * (2.0f * kPi / kTicks) - kPi / 2.0f;
    const float ca = std::cos(a), sa = std::sin(a);
    for (int o = -1; o <= 1; ++o) {  // 3px-thick tick from three parallel lines
      const float px = -sa * o, py = ca * o;
      PLine(static_cast<int>(RingCx() + ca * kRingInner + px + 0.5f),
            static_cast<int>(RingCy() + sa * kRingInner + py + 0.5f),
            static_cast<int>(RingCx() + ca * kRingOuter + px + 0.5f),
            static_cast<int>(RingCy() + sa * kRingOuter + py + 0.5f), rgb);
    }
  }

  void DrawRing(int lit, uint32_t lit_rgb) {
    for (int i = 0; i < kTicks; ++i) {
      DrawTick(i, i >= kTicks - lit ? lit_rgb : kTickDim);
    }
  }

  int RingLit(uint32_t remaining_ms) {
    const uint32_t total_ms = static_cast<uint32_t>(period_total_s_) * 1000u;
    if (total_ms == 0) return 0;
    int lit = static_cast<int>((remaining_ms * static_cast<uint32_t>(kTicks) +
                                total_ms - 1) / total_ms);
    if (lit > kTicks) lit = kTicks;
    return lit;
  }

  // ---- Orientation (auto-rotate + flip gestures) --------------------------
  // A quick 180-degree flip is the gesture (start / pause / resume); a
  // 90-degree turn only re-lays-out the current screen. A slow 180 that
  // lingers sideways long enough to register 90 first counts as two turns.

  void PollOrientation() {
    const MotionSample m = Cheeko().motion().Read();
    int cand = -1;
    if (std::fabs(m.y) >= std::fabs(m.x)) {
      if (m.y > kFlipThresholdG) cand = 0;          // upright
      else if (m.y < -kFlipThresholdG) cand = 180;  // upside-down
    } else {
      if (m.x > kFlipThresholdG) cand = 90;         // right edge down (CW)
      else if (m.x < -kFlipThresholdG) cand = 270;  // left edge down (CCW)
    }
    if (cand < 0 || cand == orient_) {
      orient_candidate_ = -1;
      return;
    }
    if (cand != orient_candidate_) {
      orient_candidate_ = cand;
      orient_since_ = now_;
      return;
    }
    if (now_ - orient_since_ < kFlipDebounceMs) return;
    orient_candidate_ = -1;
    orient_ = cand;
    OnRotated();
  }

  // Any rotation, 90 or 180, is the timer control: start when armed, pause
  // when running, resume when paused. Menu and alarm screens just re-orient.
  void OnRotated() {
    switch (screen_) {
      case Screen::Menu: DrawMenu(); break;
      case Screen::Custom:
      case Screen::Armed:
        StartTimer();
        break;
      case Screen::Run: PauseTimer(); break;
      case Screen::Paused: ResumeTimer(); break;
      case Screen::Alarm: DrawAlarm(); break;     // only a tap/knock silences
    }
  }

  // ---- Sounds -------------------------------------------------------------

  void Click() { Cheeko().speaker().Tone(1200, 25); }

  // Volume keys always unmute: pressing them means "I want to hear it".
  void SetVol(int v) {
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    volume_ = v;
    Cheeko().storage().PutInt("vol", volume_);
    if (muted_) {
      muted_ = false;
      Cheeko().storage().PutInt("mute", 0);
    }
    Cheeko().speaker().SetVolume(volume_);
    Cheeko().speaker().Tone(1000, 40);
    Cheeko().log().Info("volume " + std::to_string(volume_));
    ShowVolOverlay();
  }

  void SetMuted(bool muted) {
    muted_ = muted;
    Cheeko().storage().PutInt("mute", muted_ ? 1 : 0);
    Cheeko().speaker().SetVolume(muted_ ? 0 : volume_);
    if (!muted_) Cheeko().speaker().Tone(1200, 40);  // audible confirmation
  }

  // ---- Mute icon + volume overlay -----------------------------------------

  Box MuteBox() { return Box{6, 2, 40, 30}; }

  bool HandleMuteTap(int x, int y) {
    if (!Hit(x, y, MuteBox())) return false;
    SetMuted(!muted_);
    DrawMuteIcon();
    return true;
  }

  // Small speaker glyph in the top-left corner; red slash when muted.
  void DrawMuteIcon() {
    const Box b = MuteBox();
    PFillRect(b.x, b.y, b.w, b.h, kBg);
    const int x = b.x + 8, y = b.y + 4;
    const uint32_t body = muted_ ? kHint : kMint;
    PFillRect(x, y + 8, 5, 8, body);  // speaker box
    for (int i = 0; i < 6; ++i) {     // cone widening to the right
      PFillRect(x + 5 + i, y + 7 - i, 1, 10 + 2 * i, body);
    }
    if (muted_) {
      PLine(b.x + 4, b.y + b.h - 4, b.x + b.w - 14, b.y + 2, kRedText);
      PLine(b.x + 5, b.y + b.h - 3, b.x + b.w - 13, b.y + 3, kRedText);
    } else {
      PFillRect(x + 14, y + 7, 2, 10, kMint);  // sound waves
      PFillRect(x + 17, y + 4, 2, 16, kMint);
    }
  }

  void ShowVolOverlay() {
    vol_overlay_until_ = now_ + 1500;
    const int w = 150, h = 34;
    const int x = (W() - w) / 2, y = H() - 64;
    PFillRect(x, y, w, h, kPanel);
    PRect(x, y, w, h, kPanelEdge);
    PText(x + 10, y + 10, "VOL", 2, kDim);
    PFillRect(x + 58, y + 12, 80, 10, kTickDim);
    if (volume_ > 0) PFillRect(x + 58, y + 12, 80 * volume_ / 100, 10, kMint);
  }

  void DrawCurrentScreen() {
    switch (screen_) {
      case Screen::Menu: DrawMenu(); break;
      case Screen::Custom: DrawCustom(); break;
      case Screen::Armed: DrawArmed(); break;
      case Screen::Run: DrawRunFull(); break;
      case Screen::Paused: DrawPaused(); break;
      case Screen::Alarm: break;  // repaints itself on every flash
    }
  }

  void PlayAlarmBurst() {
    auto& sp = Cheeko().speaker();
    if (in_focus_) {  // focus done -> rising "break time" chime
      sp.Tone(880, 90);
      sp.Tone(1109, 90);
      sp.Tone(1319, 150);
    } else {  // break done -> falling "back to work" call
      sp.Tone(1319, 90);
      sp.Tone(1046, 90);
      sp.Tone(784, 150);
    }
  }

  // ---- Formatting helpers -------------------------------------------------

  static std::string TwoDigits(int v) {
    return v < 10 ? "0" + std::to_string(v) : std::to_string(v);
  }

  static std::string FormatMSS(int s) {
    return TwoDigits(s / 60) + ":" + TwoDigits(s % 60);
  }

  // "25" for whole minutes, "15S" for sub-minute demo values.
  static std::string ShortLen(int s) {
    return s < 60 ? std::to_string(s) + "S" : std::to_string(s / 60);
  }

  static std::string BreakLine(int rest_s) {
    return rest_s < 60 ? "BREAK " + std::to_string(rest_s) + " SEC"
                       : "BREAK " + std::to_string(rest_s / 60) + " MIN";
  }

  // ---- Menu ---------------------------------------------------------------

  int MenuTop() { return Land() ? 32 : 46; }
  int MenuRowH() { return Land() ? 36 : 44; }
  int MenuGap() { return Land() ? 4 : 6; }
  int MenuX() { return Land() ? 20 : 14; }
  int MenuW() { return W() - 2 * MenuX(); }

  void EnterMenu() {
    screen_ = Screen::Menu;
    DrawMenu();
  }

  void DrawMenu() {
    Cheeko().display().Clear(kBg);
    PCenterText(Land() ? 6 : 14, "POMODORO", 2, kInk);
    PFillRect((W() - 94) / 2, Land() ? 24 : 32, 94, 3, kOrange);
    for (int i = 0; i < kMenuRows; ++i) {
      const int y = MenuTop() + i * (MenuRowH() + MenuGap());
      const bool custom = i == kPresetCount;
      PFillRect(MenuX(), y, MenuW(), MenuRowH(), kPanel);
      PRect(MenuX(), y, MenuW(), MenuRowH(), kPanelEdge);
      PFillRect(MenuX(), y, 4, MenuRowH(),
                custom ? Color::Cyan : kPresets[i].accent);
      const std::string name = custom ? "CUSTOM" : kPresets[i].name;
      const int focus_s = custom ? custom_focus_min_ * 60 : kPresets[i].focus_s;
      const int rest_s = custom ? custom_rest_min_ * 60 : kPresets[i].rest_s;
      const std::string pair = ShortLen(focus_s) + "/" + ShortLen(rest_s);
      const int text_y = y + (MenuRowH() - 14) / 2;
      PText(MenuX() + 16, text_y, name, 2, kInk);
      PText(MenuX() + MenuW() - 8 - TextW(pair, 2), text_y, pair, 2, kDim);
    }
    DrawMuteIcon();
  }

  void TouchMenu(int x, int y) {
    if (x < MenuX() || x >= MenuX() + MenuW()) return;
    for (int i = 0; i < kMenuRows; ++i) {
      const int row_y = MenuTop() + i * (MenuRowH() + MenuGap());
      if (y < row_y || y >= row_y + MenuRowH()) continue;
      Click();
      if (i == kPresetCount) {
        EnterCustom();
      } else {
        sel_name_ = kPresets[i].name;
        focus_s_ = kPresets[i].focus_s;
        rest_s_ = kPresets[i].rest_s;
        screen_ = Screen::Armed;
        DrawArmed();
      }
      return;
    }
  }

  // ---- Custom editor (also an armed screen: flip to start) ----------------
  // Portrait stacks the two adjuster rows; landscape puts them side by side.

  struct AdjRow {
    int minus_x, plus_x, y, box;  // +/- button squares
    int clear_x, clear_w;         // value area to wipe on change
    int cx;                       // value / label center
  };

  AdjRow CustomRow(bool focus_row) {
    if (!Land()) {
      return {24, 172, focus_row ? 70 : 150, 44, 72, 96, 120};
    }
    const int base = focus_row ? 12 : 156;
    return {base, base + 88, 60, 40, base + 40, 48, base + 64};
  }

  Box CustomBack() {
    return Land() ? Box{93, 170, 110, 32} : Box{65, 252, 110, 32};
  }

  void EnterCustom() {
    sel_name_ = "CUSTOM";
    focus_s_ = custom_focus_min_ * 60;
    rest_s_ = custom_rest_min_ * 60;
    screen_ = Screen::Custom;
    DrawCustom();
  }

  void DrawCustomValue(const AdjRow& r, int value) {
    PFillRect(r.clear_x, r.y, r.clear_w, r.box, kBg);
    const std::string text = std::to_string(value);
    PTextCentered(r.cx, r.y + (r.box - 21) / 2, text, 3, kInk);
  }

  void DrawAdjustRow(const AdjRow& r, int value) {
    const int gx = (r.box - 15) / 2, gy = (r.box - 21) / 2;
    PFillRect(r.minus_x, r.y, r.box, r.box, kPanel);
    PRect(r.minus_x, r.y, r.box, r.box, kPanelEdge);
    PText(r.minus_x + gx, r.y + gy, "-", 3, kInk);
    PFillRect(r.plus_x, r.y, r.box, r.box, kPanel);
    PRect(r.plus_x, r.y, r.box, r.box, kPanelEdge);
    PText(r.plus_x + gx, r.y + gy, "+", 3, kInk);
    DrawCustomValue(r, value);
  }

  void DrawCustom() {
    Cheeko().display().Clear(kBg);
    PCenterText(Land() ? 6 : 14, "CUSTOM", 2, kInk);
    PFillRect((W() - 70) / 2, Land() ? 24 : 32, 70, 3, Color::Cyan);
    const AdjRow focus = CustomRow(true), rest = CustomRow(false);
    if (Land()) {
      PTextCentered(focus.cx, 38, "FOCUS", 2, kOrange);
      PTextCentered(rest.cx, 38, "BREAK", 2, kMint);
      DrawAdjustRow(focus, custom_focus_min_);
      DrawAdjustRow(rest, custom_rest_min_);
      PCenterText(130, "ROTATE TO START", 2, kOrange);
    } else {
      PCenterText(50, "FOCUS MINUTES", 2, kOrange);
      DrawAdjustRow(focus, custom_focus_min_);
      PCenterText(130, "BREAK MINUTES", 2, kMint);
      DrawAdjustRow(rest, custom_rest_min_);
      PCenterText(212, "ROTATE TO START", 2, kOrange);
    }
    const Box back = CustomBack();
    PFillRect(back.x, back.y, back.w, back.h, kPanel);
    PRect(back.x, back.y, back.w, back.h, kPanelEdge);
    PCenterText(back.y + (back.h - 14) / 2, "BACK", 2, kDim);
    DrawMuteIcon();
  }

  void TouchCustom(int x, int y) {
    for (int row = 0; row < 2; ++row) {
      const AdjRow r = CustomRow(row == 0);
      const bool minus = Hit(x, y, Box{r.minus_x, r.y, r.box, r.box});
      const bool plus = Hit(x, y, Box{r.plus_x, r.y, r.box, r.box});
      if (!minus && !plus) continue;
      if (row == 0) {  // focus, 5-minute steps
        custom_focus_min_ += plus ? 5 : -5;
        if (custom_focus_min_ < 5) custom_focus_min_ = 5;
        if (custom_focus_min_ > 90) custom_focus_min_ = 90;
        Cheeko().storage().PutInt("cust_f", custom_focus_min_);
        focus_s_ = custom_focus_min_ * 60;
        DrawCustomValue(r, custom_focus_min_);
      } else {  // break, 1-minute steps
        custom_rest_min_ += plus ? 1 : -1;
        if (custom_rest_min_ < 1) custom_rest_min_ = 1;
        if (custom_rest_min_ > 30) custom_rest_min_ = 30;
        Cheeko().storage().PutInt("cust_r", custom_rest_min_);
        rest_s_ = custom_rest_min_ * 60;
        DrawCustomValue(r, custom_rest_min_);
      }
      Cheeko().speaker().Tone(plus ? 950 : 700, 20);
      return;
    }
    if (Hit(x, y, CustomBack())) {
      Click();
      EnterMenu();
    }
  }

  // ---- Armed: preset chosen, waiting for the 180-degree flip --------------

  void DrawArmed() {
    Cheeko().display().Clear(kBg);
    DrawRing(kTicks, kOrange);
    DrawTimeDigits(RingDigitsX(), RingDigitsY(), focus_s_, kInk);
    PTextCentered(RingCx(), RingCy() + 36, BreakLine(rest_s_), 2, kDim);
    if (Land()) {
      // Preset name up the side column, split at the space if two words.
      const size_t sp = sel_name_.find(' ');
      if (sp == std::string::npos) {
        PTextCentered(SideCx(), 52, sel_name_, 2, kOrange);
      } else {
        PTextCentered(SideCx(), 42, sel_name_.substr(0, sp), 2, kOrange);
        PTextCentered(SideCx(), 64, sel_name_.substr(sp + 1), 2, kOrange);
      }
      PTextCentered(SideCx(), 118, "ROTATE", 2, kOrange);
      PTextCentered(SideCx(), 138, "TO", 2, kOrange);
      PTextCentered(SideCx(), 158, "START", 2, kOrange);
      PTextCentered(SideCx(), 190, "TAP FOR", 2, kHint);
      PTextCentered(SideCx(), 210, "MENU", 2, kHint);
    } else {
      PCenterText(16, sel_name_, 2, kOrange);
      PCenterText(258, "ROTATE TO START", 2, kOrange);
      PCenterText(278, "TAP FOR MENU", 2, kHint);
    }
    DrawMuteIcon();
  }

  // ---- Running timer ------------------------------------------------------

  uint32_t RemainingMs() {
    return period_end_ms_ > now_ ? period_end_ms_ - now_ : 0;
  }

  void StartTimer() {
    session_ = 1;
    Cheeko().log().Info("pomodoro: started " + std::to_string(focus_s_) + "s/" +
                        std::to_string(rest_s_) + "s");
    StartPeriod(true);
  }

  // The running timer ticks like a fast clock: alternating tick/tock twice a
  // second. Square-wave Tone() sounds shrill, so the ticks are synthesized
  // once at startup as short damped sines — a soft wood-block sound. Silence
  // doubles as the paused signal.
  static void SynthTick(int16_t* out, float freq) {
    for (int n = 0; n < kTickSamples; ++n) {
      const float t = static_cast<float>(n) / kTickSampleRate;
      float env = std::exp(-t * 110.0f);                 // ~15ms audible body
      if (n < 16) env *= static_cast<float>(n) / 16.0f;  // 1ms anti-click ramp
      out[n] = static_cast<int16_t>(14000.0f * env *
                                    std::sin(6.28319f * freq * t));
    }
  }

  void PlayClockTick() {
    if (muted_) return;
    Cheeko().speaker().PlayPcm(tick_high_ ? tick_pcm_ : tock_pcm_,
                               kTickSamples, kTickSampleRate);
  }

  void StartPeriod(bool focus) {
    in_focus_ = focus;
    period_total_s_ = focus ? focus_s_ : rest_s_;
    period_end_ms_ = now_ + static_cast<uint32_t>(period_total_s_) * 1000u;
    screen_ = Screen::Run;
    DrawRunFull();
    next_tick_ms_ = now_;  // first clock tick fires immediately
  }

  void DrawRunFull() {
    Cheeko().display().Clear(kBg);
    const uint32_t accent = in_focus_ ? kOrange : kMint;
    const std::string label = in_focus_ ? "FOCUS" : "BREAK";
    const std::string tag = "#" + std::to_string(session_);
    if (Land()) {
      PTextCentered(SideCx(), 64, label, 2, accent);
      PTextCentered(SideCx(), 92, tag, 2, kDim);
      PTextCentered(SideCx(), 150, "ROTATE:", 2, kHint);
      PTextCentered(SideCx(), 170, "PAUSE", 2, kHint);
    } else {
      PCenterText(16, label, 2, accent);
      PText(224 - TextW(tag, 2), 16, tag, 2, kDim);
      PCenterText(268, "ROTATE TO PAUSE", 2, kHint);
    }
    const uint32_t rem = RemainingMs();
    last_shown_s_ = static_cast<int>((rem + 999) / 1000);
    last_lit_ = RingLit(rem);
    DrawRing(last_lit_, accent);
    DrawTimeDigits(RingDigitsX(), RingDigitsY(), last_shown_s_, kInk);
    DrawMuteIcon();
  }

  void TickRun() {
    if (now_ >= next_tick_ms_) {
      next_tick_ms_ = now_ + 500;  // two ticks per second, stopwatch pace
      tick_high_ = !tick_high_;
      PlayClockTick();
    }
    const uint32_t rem = RemainingMs();
    const int s = static_cast<int>((rem + 999) / 1000);
    if (s != last_shown_s_) {
      last_shown_s_ = s;
      DrawTimeDigits(RingDigitsX(), RingDigitsY(), s, kInk);
      const int lit = RingLit(rem);
      if (lit < last_lit_) {  // dim only the newly spent ticks
        for (int i = kTicks - last_lit_; i < kTicks - lit; ++i) {
          DrawTick(i, kTickDim);
        }
        last_lit_ = lit;
      }
    }
    if (rem == 0) EnterAlarm();
  }

  // ---- Pause / resume (quick 180 flip while running) ----------------------

  Box PausedReset() {
    return Land() ? Box{93, 170, 110, 32} : Box{65, 244, 110, 34};
  }

  void PauseTimer() {
    paused_rem_ms_ = RemainingMs();
    screen_ = Screen::Paused;
    Cheeko().speaker().Tone(500, 70);
    DrawPaused();
  }

  void ResumeTimer() {
    period_end_ms_ = now_ + paused_rem_ms_;
    screen_ = Screen::Run;
    DrawRunFull();
    next_tick_ms_ = now_;  // ticking resumes with the countdown
  }

  void DrawPaused() {
    Cheeko().display().Clear(kBg);
    const std::string line =
        std::string(in_focus_ ? "FOCUS" : "BREAK") + "  #" + std::to_string(session_);
    const int s = static_cast<int>((paused_rem_ms_ + 999) / 1000);
    const int dig_x = (W() - kDigSpanW) / 2;
    if (Land()) {
      PCenterText(10, "PAUSED", 2, Color::Amber);
      PCenterText(36, line, 2, kDim);
      DrawTimeDigits(dig_x, 70, s, kInkFaint);
      PCenterText(140, "ROTATE TO RESUME", 2, kMint);
    } else {
      PCenterText(16, "PAUSED", 2, Color::Amber);
      PCenterText(64, line, 2, kDim);
      DrawTimeDigits(dig_x, 124, s, kInkFaint);
      PCenterText(210, "ROTATE TO RESUME", 2, kMint);
    }
    const Box reset = PausedReset();
    PFillRect(reset.x, reset.y, reset.w, reset.h, kPanel);
    PRect(reset.x, reset.y, reset.w, reset.h, kRedEdge);
    PCenterText(reset.y + (reset.h - 14) / 2, "RESET", 2, kRedText);
    DrawMuteIcon();
  }

  void TouchPaused(int x, int y) {
    if (Hit(x, y, PausedReset())) {
      Cheeko().speaker().Tone(400, 60);
      EnterMenu();
    }
  }

  // ---- Alarm: end of every focus and break period -------------------------

  void EnterAlarm() {
    screen_ = Screen::Alarm;
    alarm_flash_on_ = false;
    next_flash_ms_ = 0;  // fire the first flash + burst immediately
    alarm_started_ms_ = now_;
    Cheeko().log().Info(in_focus_ ? "pomodoro: focus done" : "pomodoro: break done");
  }

  void TickAlarm() {
    // A knock on the case spikes the accelerometer away from steady 1g, so
    // the alarm can be slapped off like a physical clock — the capacitive
    // panel itself only feels fingers on the glass. Checked every tick (a
    // knock is a few ms long); short grace so the period-end moment itself
    // can't self-silence.
    if (now_ - alarm_started_ms_ > 500) {
      const MotionSample m = Cheeko().motion().Read();
      const float mag = std::sqrt(m.x * m.x + m.y * m.y + m.z * m.z);
      if (std::fabs(mag - 1.0f) > kKnockG) {
        SilenceAlarm();
        return;
      }
    }
    if (now_ < next_flash_ms_) return;
    next_flash_ms_ = now_ + 500;
    alarm_flash_on_ = !alarm_flash_on_;
    DrawAlarm();
    if (alarm_flash_on_) PlayAlarmBurst();  // one burst per flash cycle
  }

  void DrawAlarm() {
    const bool break_next = in_focus_;  // the period that just ended
    const uint32_t accent = break_next ? kMint : kOrange;
    const uint32_t bg = alarm_flash_on_ ? accent : kBg;
    const uint32_t fg = alarm_flash_on_ ? kFlashInk : accent;
    const uint32_t sub = alarm_flash_on_ ? kFlashInk : kDim;
    Cheeko().display().Clear(bg);
    const int next_s = break_next ? rest_s_ : focus_s_;
    const int y_big = Land() ? 60 : 88;
    const int y_next = Land() ? 110 : 140;
    const int y_tap = Land() ? 170 : 216;
    PCenterText(y_big, break_next ? "BREAK TIME" : "FOCUS TIME", 3, fg);
    PCenterText(y_next, "NEXT " + FormatMSS(next_s), 2, sub);
    PCenterText(y_tap, "TAP TO SILENCE", 2, sub);
  }

  void SilenceAlarm() {
    if (in_focus_) {
      StartPeriod(false);  // into the break
    } else {
      session_++;          // completed a full pomodoro; next focus round
      StartPeriod(true);
    }
  }

  // ---- State --------------------------------------------------------------

  Screen screen_ = Screen::Menu;
  uint32_t now_ = 0;

  int orient_ = 0;            // 0 / 90 / 180 / 270 degrees clockwise
  int orient_candidate_ = -1;
  uint32_t orient_since_ = 0;
  uint32_t last_motion_ms_ = 0;

  std::string sel_name_;
  int focus_s_ = 25 * 60;
  int rest_s_ = 5 * 60;
  int custom_focus_min_ = 25;
  int custom_rest_min_ = 5;
  int volume_ = 70;
  bool muted_ = false;
  uint32_t vol_overlay_until_ = 0;

  bool in_focus_ = true;
  int session_ = 1;
  int period_total_s_ = 0;
  uint32_t period_end_ms_ = 0;
  uint32_t paused_rem_ms_ = 0;
  int last_shown_s_ = -1;
  int last_lit_ = kTicks;
  uint32_t next_tick_ms_ = 0;
  bool tick_high_ = false;
  static constexpr int kTickSampleRate = 16000;  // matches the device I2S rate
  static constexpr int kTickSamples = 640;       // 40ms clip
  int16_t tick_pcm_[kTickSamples] = {0};
  int16_t tock_pcm_[kTickSamples] = {0};

  bool alarm_flash_on_ = false;
  uint32_t next_flash_ms_ = 0;
  uint32_t alarm_started_ms_ = 0;
};

CHEEKO_APP(PomodoroApp);
