#include <cmath>

#include "cheeko.h"

#include "boot_voice_pcm.h"
#include "emblem_img.h"
#include "emblem_small_img.h"
#include "webitup_pcm.h"

using namespace cheeko;

namespace {

constexpr uint32_t kRed = 0xc41e2f;
constexpr uint32_t kRedDeep = 0x8f1424;
constexpr uint32_t kWebLine = 0x5c0d1a;
constexpr uint32_t kNavy = 0x0e1420;
constexpr uint32_t kPanel = 0x182233;
constexpr uint32_t kPanelEdge = 0x2c3a52;

constexpr int kQuoteCount = 6;
const char* const kQuotes[kQuoteCount] = {
    "With great power comes great responsibility.",
    "Thwip!",
    "Your friendly neighborhood desk buddy.",
    "Anyone can wear the mask.",
    "Just a kid from Queens... running at 240 MHz.",
    "Web fluid at 87 percent.",
};

const char* const kModeNames[4] = {"SPIDEY", "QUOTES", "THWIP SHOT", "WALL CLIMB"};

// Phone notifications arrive via this public ntfy.sh topic (the phone POSTs,
// the device polls). Pick your own long random name — anyone who knows the
// topic can read and post to it.
const char* const kNtfyTopic = "cheeko-gotchi-spidey-x7k4m9";

}  // namespace

class SpideyPetApp : public CheekoApp {
 public:
  void OnStart() override {
    DrawBootScreen();
    Cheeko().speaker().PlayPcm(kBOOT_VOICE_PCM, kBOOT_VOICE_LEN, kBOOT_VOICE_RATE);
    thwip_best_ = Cheeko().storage().GetInt("thwip_best", 0);
    climb_best_ = Cheeko().storage().GetInt("climb_best", 0);
    Cheeko().wifi().Connect();
    if (Cheeko().wifi().IsConnected()) {
      Cheeko().speaker().PlayPcm(kWEBITUP_PCM, kWEBITUP_LEN, kWEBITUP_RATE);
    }
    booting_ = true;
  }

  void OnTick(uint32_t uptime_ms) override {
    now_ = uptime_ms;
    if (booting_) {
      DrawBootProgress();
      if (now_ > 1700) {
        booting_ = false;
        EnterMode(Mode::Idle);
      }
      return;
    }
    if (now_ > poll_next_ms_) {
      poll_next_ms_ = now_ + 8000;
      if (Cheeko().wifi().IsConnected()) {
        Cheeko().cloud().GetJson(std::string("https://ntfy.sh/") + kNtfyTopic +
                                 "/json?poll=1&since=9s");
      }
    }
    if (focused_until_ != 0) {
      if (now_ > focused_until_) {
        focused_until_ = 0;
        EnterMode(Mode::Idle);
      }
      return;
    }
    switch (mode_) {
      case Mode::Idle: break;
      case Mode::Quotes: break;
      case Mode::Thwip: TickThwip(); break;
      case Mode::Climb: TickClimb(); break;
    }
  }

  // Extract a JSON string field's value, handling the common escapes. Good
  // enough for ntfy's single-line event objects; not a general JSON parser.
  static std::string JsonField(const std::string& line, const char* key) {
    const std::string needle = std::string("\"") + key + "\":\"";
    const size_t start = line.find(needle);
    if (start == std::string::npos) return "";
    std::string out;
    for (size_t i = start + needle.size(); i < line.size(); ++i) {
      const char c = line[i];
      if (c == '"') break;
      if (c == '\\' && i + 1 < line.size()) {
        const char esc = line[++i];
        if (esc == 'n' || esc == 't') out += ' ';
        else if (esc == 'u') { i += 4; out += '?'; }
        else out += esc;  // covers \" \\ \/
      } else {
        out += c;
      }
    }
    return out;
  }

  void OnCloudText(const std::string& text) override {
    if (text.empty()) return;
    std::string note;
    // A poll body is one JSON object per line; alert on the NEWEST message
    // event. Phone notification forwarders often put the text in the ntfy
    // "title" and send no body — ntfy then substitutes the placeholder
    // "triggered" as the message — so read both fields and prefer real
    // content over the placeholder.
    const size_t last_event = text.rfind("\"event\":\"message\"");
    if (last_event != std::string::npos) {
      size_t line_start = text.rfind('\n', last_event);
      line_start = line_start == std::string::npos ? 0 : line_start + 1;
      size_t line_end = text.find('\n', last_event);
      if (line_end == std::string::npos) line_end = text.size();
      const std::string line = text.substr(line_start, line_end - line_start);
      std::string message = JsonField(line, "message");
      const std::string title = JsonField(line, "title");
      if (message == "triggered" && !title.empty()) message.clear();
      note = title.empty() ? message
             : message.empty() ? title
                               : title + ": " + message;
    } else if (text[0] != '{' && text[0] != '[' && text.size() < 200) {
      note = text;  // simulator cloud panel / plain-text pushes
    }
    if (!note.empty()) FocusAlert(note);
  }

  void OnTouch(const TouchEvent& event) override {
    if (booting_) return;
    if (focused_until_ != 0) {
      if (event.pressed) {
        focused_until_ = 0;
        EnterMode(Mode::Idle);
      }
      return;
    }
    switch (mode_) {
      case Mode::Idle: break;
      case Mode::Quotes: if (event.pressed) NextQuote(); break;
      case Mode::Thwip: if (event.pressed) TouchThwip(event); break;
      case Mode::Climb: TouchClimb(event); break;
    }
  }

  void OnButton(const ButtonEvent& event) override {
    if (!event.pressed || booting_) return;
    if (event.button == ButtonEvent::Button::VolumeUp) {
      EnterMode(static_cast<Mode>((static_cast<int>(mode_) + 1) % 4));
    } else if (event.button == ButtonEvent::Button::VolumeDown) {
      EnterMode(static_cast<Mode>((static_cast<int>(mode_) + 3) % 4));
    }
  }

 private:
  enum class Mode { Idle, Quotes, Thwip, Climb };

  // ---- Shared chrome ------------------------------------------------------

  int W() { return Cheeko().display().Width(); }
  int H() { return Cheeko().display().Height(); }

  int Rand(int range) {
    rng_ = rng_ * 1664525u + 1013904223u + now_;
    return static_cast<int>((rng_ >> 16) % static_cast<uint32_t>(range));
  }

  void Thwip() {
    Cheeko().speaker().Tone(1680, 35);
    Cheeko().speaker().Tone(1120, 45);
    Cheeko().speaker().Tone(760, 60);
  }

  // Top status band: mode name left, context right, amber keyline under it.
  void DrawTopBar(const std::string& right) {
    auto& d = Cheeko().display();
    d.FillRect(0, 0, W(), 26, kNavy);
    d.FillRect(0, 26, W(), 2, Color::Amber);
    d.Text(6, 6, kModeNames[static_cast<int>(mode_)]);
    if (!right.empty()) {
      d.Text(W() - 6 - 12 * static_cast<int>(right.size()), 6, right);
    }
  }

  // The wall-climb spider sprite: body, head, and four legs per side.
  void DrawSpider(int x, int y, uint32_t rgb) {
    auto& d = Cheeko().display();
    d.FillCircle(x, y, 5, rgb);
    d.FillCircle(x, y - 6, 3, rgb);
    for (int s = -1; s <= 1; s += 2) {
      d.Line(x + s * 3, y - 4, x + s * 12, y - 10, rgb);
      d.Line(x + s * 4, y - 1, x + s * 14, y - 3, rgb);
      d.Line(x + s * 4, y + 2, x + s * 14, y + 5, rgb);
      d.Line(x + s * 3, y + 4, x + s * 11, y + 11, rgb);
    }
  }

  void DrawBootScreen() {
    auto& d = Cheeko().display();
    d.Clear(kNavy);
    DrawEmblemSmall(d, W() / 2 - kEMBLEM_SMALL_W / 2, H() / 2 - 100);
    d.CenterText(H() / 2 + 24, "SPIDEY PET");
    d.Rect(W() / 2 - 62, H() / 2 + 50, 124, 12, kPanelEdge);
  }

  void DrawBootProgress() {
    const int fill = static_cast<int>(120.0f * (now_ > 1700 ? 1700 : now_) / 1700.0f);
    Cheeko().display().FillRect(W() / 2 - 60, H() / 2 + 52, fill, 8, kRed);
  }

  int WrapText(const std::string& body, int chars_per_line, std::string* lines,
               int max_lines) {
    int count = 0;
    std::string line, word;
    for (size_t i = 0; i <= body.size(); ++i) {
      const char c = i < body.size() ? body[i] : ' ';
      if (c != ' ' && c != '\n') { word += c; continue; }
      if (word.empty()) continue;
      if (!line.empty() &&
          line.size() + 1 + word.size() > static_cast<size_t>(chars_per_line)) {
        if (count < max_lines) lines[count++] = line;
        line.clear();
      }
      if (!line.empty()) line += ' ';
      line += word;
      word.clear();
    }
    if (!line.empty() && count < max_lines) lines[count++] = line;
    return count;
  }

  // Focused mode: spidey-sense with lightning, triggered by a phone
  // notification (or anything typed into the simulator's cloud panel).
  void DrawBolts(int cx, int cy, int radius) {
    auto& d = Cheeko().display();
    for (int i = 0; i < 6; ++i) {
      const float a = 0.5236f + static_cast<float>(i) * 6.2832f / 6.0f;
      float x = cx + std::cos(a) * radius;
      float y = cy + std::sin(a) * radius;
      const float ux = std::cos(a), uy = std::sin(a);
      float sign = 1.0f;
      for (int seg = 0; seg < 3; ++seg) {
        const float nx = x + ux * 15.0f - uy * 10.0f * sign;
        const float ny = y + uy * 15.0f + ux * 10.0f * sign;
        d.Line(static_cast<int>(x), static_cast<int>(y), static_cast<int>(nx),
               static_cast<int>(ny), Color::Amber);
        x = nx; y = ny; sign = -sign;
      }
      d.FillCircle(static_cast<int>(x), static_cast<int>(y), 2, Color::Ink);
    }
  }

  void FocusAlert(const std::string& note) {
    focused_until_ = now_ + 9000;
    auto& d = Cheeko().display();
    d.Clear(kNavy);
    const int cy = H() / 2 - 72;
    DrawEmblemSmall(d, W() / 2 - kEMBLEM_SMALL_W / 2, cy - kEMBLEM_SMALL_H / 2);
    DrawBolts(W() / 2, cy, 64);
    const int card_y = H() / 2 + 8;
    std::string lines[6];
    int max_lines = (H() - card_y - 14) / 20;
    if (max_lines > 6) max_lines = 6;
    const int count = WrapText(note, (W() - 56) / 12, lines, max_lines);
    const int card_h = 26 + count * 20;
    d.FillRect(12, card_y, W() - 24, card_h, kPanel);
    d.Rect(12, card_y, W() - 24, card_h, kPanelEdge);
    d.FillRect(12, card_y, 4, card_h, Color::Amber);
    for (int i = 0; i < count; ++i) d.Text(28, card_y + 15 + i * 20, lines[i]);
    Cheeko().speaker().Tone(2080, 80);
    Cheeko().speaker().Tone(1560, 80);
    Cheeko().speaker().Tone(2080, 140);
  }

  void EnterMode(Mode mode) {
    mode_ = mode;
    switch (mode_) {
      case Mode::Idle:
        DrawIdle();
        break;
      case Mode::Quotes:
        quote_index_ = -1;
        DrawQuoteScreen("...");
        break;
      case Mode::Thwip:
        StartThwipRound();
        break;
      case Mode::Climb:
        StartClimb();
        break;
    }
  }

  // ---- Idle: the emblem ---------------------------------------------------

  void DrawIdle() {
    auto& d = Cheeko().display();
    d.Clear(kNavy);
    DrawEmblem(d, W() / 2 - kEMBLEM_W / 2, (H() - kEMBLEM_H) / 2);
  }

  // ---- Quotes -------------------------------------------------------------

  void DrawQuoteScreen(const std::string& body) {
    auto& d = Cheeko().display();
    d.Clear(kNavy);
    DrawEmblemSmall(d, W() / 2 - kEMBLEM_SMALL_W / 2, 12);

    // Speech bubble with a tail pointing back up at the emblem.
    const int card_x = 12, card_w = W() - 24;
    const int card_y = 12 + kEMBLEM_SMALL_H + 22;
    std::string lines[6];
    int max_lines = (H() - card_y - 14) / 20;
    if (max_lines > 6) max_lines = 6;
    const int line_count = WrapText(body, (card_w - 28) / 12, lines, max_lines);
    const int card_h = 26 + line_count * 20;
    d.Line(W() / 2 + 16, card_y - 18, W() / 2 + 38, card_y + 2, kPanelEdge);
    d.Line(W() / 2 + 24, card_y - 22, W() / 2 + 50, card_y + 2, kPanelEdge);
    d.FillRect(card_x, card_y, card_w, card_h, kPanel);
    d.Rect(card_x, card_y, card_w, card_h, kPanelEdge);
    d.FillRect(card_x, card_y, 4, card_h, Color::Amber);
    for (int i = 0; i < line_count; ++i) {
      d.Text(card_x + 16, card_y + 15 + i * 20, lines[i]);
    }
  }

  void NextQuote() {
    quote_index_ = (quote_index_ + 1) % kQuoteCount;
    const std::string quote = kQuotes[quote_index_];
    Thwip();
    Cheeko().speaker().Play("say:" + quote);
    DrawQuoteScreen(quote);
  }

  // ---- Thwip Shot ---------------------------------------------------------

  void DrawSkyline() {
    auto& d = Cheeko().display();
    const int base = H() - 30;
    int x = 0;
    int i = 0;
    while (x < W()) {
      const int bw = 26 + ((i * 13) % 22);
      const int bh = 26 + ((i * 29) % 44);
      d.FillRect(x, base - bh, bw - 4, bh, kPanel);
      for (int wy = base - bh + 6; wy < base - 6; wy += 12) {
        for (int wx = x + 4; wx < x + bw - 8; wx += 10) {
          d.FillRect(wx, wy, 3, 4, ((wx + wy + i) % 3) ? kPanelEdge : Color::Amber);
        }
      }
      x += bw;
      ++i;
    }
  }

  void StartThwipRound() {
    thwip_score_ = 0;
    thwip_round_end_ = now_ + 30000;
    auto& d = Cheeko().display();
    d.Clear(kNavy);
    DrawSkyline();
    SpawnTarget();
    DrawThwipHud();
  }

  void DrawDrone(int x, int y) {
    auto& d = Cheeko().display();
    d.Circle(x, y, 15, Color::Cyan);
    d.FillCircle(x, y, 6, Color::Cyan);
    d.FillCircle(x, y, 2, kNavy);
    d.Line(x - 15, y, x - 21, y - 6, Color::Cyan);
    d.Line(x + 15, y, x + 21, y - 6, Color::Cyan);
    d.FillRect(x - 24, y - 8, 7, 2, Color::Cyan);
    d.FillRect(x + 18, y - 8, 7, 2, Color::Cyan);
  }

  void SpawnTarget() {
    target_x_ = 34 + Rand(W() - 68);
    target_y_ = 56 + Rand(H() - 150);
    target_expires_ = now_ + 1900;
    DrawDrone(target_x_, target_y_);
  }

  void EraseTarget() {
    auto& d = Cheeko().display();
    d.FillRect(target_x_ - 26, target_y_ - 18, 52, 36, kNavy);
    if (target_y_ + 18 > H() - 74) DrawSkyline();  // repair clipped rooftops
  }

  void DrawThwipHud() {
    const int remaining =
        thwip_round_end_ > now_
            ? static_cast<int>((thwip_round_end_ - now_) / 1000)
            : 0;
    auto& d = Cheeko().display();
    DrawTopBar(std::to_string(remaining) + "s");
    d.Text(W() / 2 - 30, 6, "x" + std::to_string(thwip_score_));
  }

  void TickThwip() {
    if (thwip_round_end_ == 0) return;
    if (now_ > thwip_round_end_) {
      thwip_round_end_ = 0;
      if (thwip_score_ > thwip_best_) {
        thwip_best_ = thwip_score_;
        Cheeko().storage().PutInt("thwip_best", thwip_best_);
      }
      auto& d = Cheeko().display();
      d.Clear(kNavy);
      DrawTopBar("");
      const int cy = H() / 2;
      d.FillRect(24, cy - 56, W() - 48, 104, kPanel);
      d.Rect(24, cy - 56, W() - 48, 104, kPanelEdge);
      d.FillRect(24, cy - 56, W() - 48, 24, kRedDeep);
      d.CenterText(cy - 50, "ROUND OVER");
      d.CenterText(cy - 16, "hits  " + std::to_string(thwip_score_));
      d.CenterText(cy + 12, "best  " + std::to_string(thwip_best_));
      return;
    }
    if (now_ > target_expires_) {
      EraseTarget();
      SpawnTarget();
    }
    if (now_ - thwip_hud_ms_ > 500) {
      thwip_hud_ms_ = now_;
      DrawThwipHud();
    }
  }

  void TouchThwip(const TouchEvent& event) {
    if (thwip_round_end_ == 0) {
      StartThwipRound();
      return;
    }
    const int dx = event.x - target_x_, dy = event.y - target_y_;
    if (dx * dx + dy * dy <= 26 * 26) {
      auto& d = Cheeko().display();
      d.Line(W() / 2, H() - 30, target_x_, target_y_, Color::Ink);
      for (int i = 0; i < 8; ++i) {
        const float a = static_cast<float>(i) * 6.2832f / 8.0f;
        d.Line(target_x_, target_y_,
               target_x_ + static_cast<int>(std::cos(a) * 14),
               target_y_ + static_cast<int>(std::sin(a) * 14), Color::White);
      }
      Cheeko().speaker().Tone(1680, 45);
      thwip_score_++;
      EraseTarget();
      d.FillRect(0, 34, W(), H() - 34 - 74, kNavy);  // clear the web streak
      DrawSkyline();
      SpawnTarget();
      DrawThwipHud();
    } else {
      Cheeko().speaker().Tone(320, 50);
    }
  }

  // ---- Wall Climb: tilt to dodge falling debris ---------------------------

  static constexpr int kHazards = 4;

  // Deterministic building facade so partial erases can repaint exactly what
  // was under a sprite: window grid, lit windows, and ledge lines.
  void RepairFacade(int rx, int ry, int rw, int rh) {
    auto& d = Cheeko().display();
    if (ry < 28) { rh -= 28 - ry; ry = 28; }
    if (rw <= 0 || rh <= 0) return;
    d.FillRect(rx, ry, rw, rh, kNavy);
    for (int ledge = 28 + 40; ledge < H(); ledge += 52) {
      if (ledge >= ry && ledge < ry + rh) d.FillRect(rx, ledge, rw, 1, kPanelEdge);
    }
    for (int col = 16; col < W(); col += 40) {
      if (col + 16 <= rx || col >= rx + rw) continue;
      for (int row = 44; row < H() - 20; row += 52) {
        if (row + 22 <= ry || row >= ry + rh) continue;
        const bool lit = ((col / 40) * 7 + (row / 52) * 13) % 5 == 0;
        const int wx = col < rx ? rx : col;
        const int wy = row < ry ? ry : row;
        const int ww = (col + 16 < rx + rw ? col + 16 : rx + rw) - wx;
        const int wh = (row + 22 < ry + rh ? row + 22 : ry + rh) - wy;
        if (ww > 0 && wh > 0) d.FillRect(wx, wy, ww, wh, lit ? 0x6b5a20 : kPanel);
      }
    }
  }

  void DrawClimbHud() {
    DrawTopBar(std::to_string(static_cast<int>(climb_height_)) + "m");
  }

  void StartClimb() {
    climb_over_ = false;
    climb_height_ = 0.0f;
    climb_x_ = W() / 2.0f;
    climb_vx_ = 0.0f;
    climb_touch_on_ = false;
    climb_last_ms_ = now_;
    climb_spawn_ms_ = now_ + 700;
    climb_started_ms_ = now_;
    for (int i = 0; i < kHazards; ++i) hz_on_[i] = false;
    RepairFacade(0, 28, W(), H() - 28);
    DrawSpider(static_cast<int>(climb_x_), SpiderY(), Color::Ink);
    DrawClimbHud();
  }

  int SpiderY() { return H() - 88; }

  void DrawHazard(int x, int y) {
    auto& d = Cheeko().display();
    d.FillRect(x, y + 2, 16, 8, kRedDeep);
    d.FillRect(x, y, 16, 2, Color::Amber);
  }

  void ClimbGameOver() {
    climb_over_ = true;
    const int height = static_cast<int>(climb_height_);
    if (height > climb_best_) {
      climb_best_ = height;
      Cheeko().storage().PutInt("climb_best", climb_best_);
    }
    Cheeko().speaker().Tone(240, 220);
    auto& d = Cheeko().display();
    const int cy = H() / 2;
    d.FillRect(24, cy - 56, W() - 48, 104, kPanel);
    d.Rect(24, cy - 56, W() - 48, 104, kPanelEdge);
    d.FillRect(24, cy - 56, W() - 48, 24, kRedDeep);
    d.CenterText(cy - 50, "SPLAT");
    d.CenterText(cy - 16, std::to_string(height) + "m");
    d.CenterText(cy + 12, "best  " + std::to_string(climb_best_) + "m");
  }

  void TickClimb() {
    if (climb_over_) return;
    uint32_t dt = now_ - climb_last_ms_;
    if (dt == 0) return;
    if (dt > 50) dt = 50;
    climb_last_ms_ = now_;
    const float elapsed_s = (now_ - climb_started_ms_) / 1000.0f;

    // Steering: physical tilt, or touch-and-hold toward the finger.
    if (climb_touch_on_) {
      climb_vx_ = (climb_touch_x_ > climb_x_ + 4)   ? 0.22f
                  : (climb_touch_x_ < climb_x_ - 4) ? -0.22f
                                                    : 0.0f;
    } else {
      climb_vx_ += Cheeko().motion().Read().x * 0.0011f * dt;
      climb_vx_ *= std::pow(0.994f, static_cast<float>(dt));
      if (climb_vx_ > 0.30f) climb_vx_ = 0.30f;
      if (climb_vx_ < -0.30f) climb_vx_ = -0.30f;
    }
    const int old_x = static_cast<int>(climb_x_);
    climb_x_ += climb_vx_ * dt;
    if (climb_x_ < 18.0f) { climb_x_ = 18.0f; climb_vx_ = 0.0f; }
    if (climb_x_ > W() - 18.0f) { climb_x_ = W() - 18.0f; climb_vx_ = 0.0f; }
    if (static_cast<int>(climb_x_) != old_x) {
      RepairFacade(old_x - 17, SpiderY() - 14, 34, 28);
      DrawSpider(static_cast<int>(climb_x_), SpiderY(), Color::Ink);
    }

    // Hazards fall, ramping with elapsed time.
    const float fall = (0.10f + elapsed_s * 0.004f) * dt;
    for (int i = 0; i < kHazards; ++i) {
      if (!hz_on_[i]) continue;
      const int old_y = static_cast<int>(hz_y_[i]);
      hz_y_[i] += fall;
      const int new_y = static_cast<int>(hz_y_[i]);
      if (new_y > H()) {
        hz_on_[i] = false;
        RepairFacade(hz_x_[i], old_y, 16, 12);
        continue;
      }
      if (new_y != old_y) {
        RepairFacade(hz_x_[i], old_y, 16, new_y - old_y < 12 ? 12 : new_y - old_y + 12);
        DrawHazard(hz_x_[i], new_y);
      }
      if (std::abs(hz_x_[i] + 8 - static_cast<int>(climb_x_)) < 20 &&
          std::abs(new_y + 5 - SpiderY()) < 16) {
        ClimbGameOver();
        return;
      }
    }
    if (now_ > climb_spawn_ms_) {
      const int interval = 950 - static_cast<int>(elapsed_s * 14.0f);
      climb_spawn_ms_ = now_ + (interval < 420 ? 420 : interval);
      for (int i = 0; i < kHazards; ++i) {
        if (hz_on_[i]) continue;
        hz_on_[i] = true;
        hz_x_[i] = 12 + Rand(W() - 40);
        hz_y_[i] = 30.0f;
        break;
      }
    }

    climb_height_ += dt * 0.0035f;
    if (now_ - climb_hud_ms_ > 400) {
      climb_hud_ms_ = now_;
      DrawClimbHud();
    }
  }

  void TouchClimb(const TouchEvent& event) {
    if (climb_over_) {
      if (event.pressed) StartClimb();
      return;
    }
    climb_touch_on_ = event.pressed;
    climb_touch_x_ = event.x;
  }

  Mode mode_ = Mode::Idle;
  bool booting_ = true;
  uint32_t now_ = 0;
  uint32_t rng_ = 2026;
  uint32_t focused_until_ = 0;
  uint32_t poll_next_ms_ = 0;
  int quote_index_ = -1;
  int thwip_score_ = 0;
  int thwip_best_ = 0;
  uint32_t thwip_round_end_ = 0;
  uint32_t thwip_hud_ms_ = 0;
  int target_x_ = 0;
  int target_y_ = 0;
  uint32_t target_expires_ = 0;
  bool climb_over_ = false;
  float climb_height_ = 0.0f;
  float climb_x_ = 0.0f;
  float climb_vx_ = 0.0f;
  int climb_best_ = 0;
  bool climb_touch_on_ = false;
  int climb_touch_x_ = 0;
  uint32_t climb_last_ms_ = 0;
  uint32_t climb_spawn_ms_ = 0;
  uint32_t climb_started_ms_ = 0;
  uint32_t climb_hud_ms_ = 0;
  int hz_x_[kHazards] = {0};
  float hz_y_[kHazards] = {0};
  bool hz_on_[kHazards] = {false};
};

CHEEKO_APP(SpideyPetApp);
