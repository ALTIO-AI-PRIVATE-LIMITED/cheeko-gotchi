#include <cmath>
#include <cstdlib>
#include <string>

#include "cheeko.h"

#include "watch_font.h"

using namespace cheeko;

namespace {

// Palette — matches the pomodoro app's dark dashboard look.
constexpr uint32_t kBg = 0x0b0c10;
constexpr uint32_t kInk = 0xf2f5f9;
constexpr uint32_t kOrange = 0xffa03c;
constexpr uint32_t kMint = 0x55e6b6;
constexpr uint32_t kRed = 0xff5b5b;
constexpr uint32_t kDim = 0x8b95a7;
constexpr uint32_t kHint = 0x596273;
constexpr uint32_t kPanel = 0x151922;
constexpr uint32_t kPanelEdge = 0x2b3547;
constexpr uint32_t kTickDim = 0x2a2e38;

// Tick ring shared with the pomodoro app's aesthetic.
constexpr int kRingCx = 120;
constexpr int kRingCy = 148;
constexpr int kRingInner = 88;
constexpr int kRingOuter = 101;
constexpr int kTicks = 60;

// Must match NTFY_TOPIC in host/claude_watch_host.py. Pick your own long
// random name — anyone who knows the topic can read these counters.
const char* const kNtfyTopic = "cheeko-claude-watch-x9m4rq72";

// Polling is deliberately slow: ntfy.sh rate-limits per visitor IP, and
// behind home NAT the device and the pushing laptop count as one visitor.
constexpr uint32_t kPollIntervalMs = 20000;
constexpr uint32_t kStaleAfterMs = 150000;  // no fresh payload -> OFFLINE

// Orientation (portrait + upside-down): gravity sign on the screen's Y axis,
// with hysteresis + debounce, same approach as the pomodoro app.
constexpr float kFlipThresholdG = 0.35f;
constexpr uint32_t kFlipDebounceMs = 250;

// Payload pushed by the host: 15 '|'-separated fields, "CW1" first. See
// host/claude_watch_host.py build_payload() for the field list.
constexpr int kFieldCount = 15;

// On-device Wi-Fi setup. The keyboard has three pages of 30 characters laid
// out on a 6x5 grid, plus a control row (page / space / delete / ok / cancel).
const char* const kKeyPages[3] = {
    "abcdefghijklmnopqrstuvwxyz.-_@",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ.-_@",
    "0123456789!@#$%^&*()-_=+.,?/:;",
};
const char* const kKeyPageLabels[3] = {"ABC", "123", "abc"};  // next page
constexpr int kMaxNets = 12;
constexpr int kMaxPassword = 63;  // WPA2 passphrase limit

struct Box {
  int x, y, w, h;
};

bool Hit(int x, int y, const Box& b) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

// The About screen's WIFI SETUP button.
constexpr Box kSetupBox = {24, 250, 192, 34};

struct Stats {
  bool valid = false;
  std::string plan;      // rate-limit tier label
  int window_pct = 0;    // elapsed % of the rolling 5h window
  int budget_pct = -1;   // tokens vs configured budget, -1 = not configured
  long window_k = 0;     // tokens this window, in thousands
  int reset_min = -1;    // minutes until the window resets, -1 = idle
  long ctx_k = 0;        // current session context, in thousands
  int ctx_pct = 0;       // context as % of the context window
  std::string model;
  long today_in_k = 0;
  long today_out_k = 0;
  long today_cw_k = 0;   // cache writes
  long today_cr_k = 0;   // cache reads
  long week_k = 0;       // all tokens, last 7 days
  std::string billing;
};

}  // namespace

class ClaudeWatchApp : public CheekoApp {
 public:
  void OnStart() override {
    Cheeko().wifi().Connect();
    DrawScreen();
  }

  void OnTick(uint32_t uptime_ms) override {
    now_ = uptime_ms;
    if (now_ - last_motion_ms_ >= 40) {
      last_motion_ms_ = now_;
      PollOrientation();
    }
    if (mode_ == Mode::WifiJoin) {
      TickJoin();
      return;
    }
    if (mode_ != Mode::Watch) return;  // setup screens are static
    if (now_ >= poll_next_ms_) {
      poll_next_ms_ = now_ + kPollIntervalMs;
      if (Cheeko().wifi().IsConnected()) {
        Cheeko().cloud().GetJson(std::string("https://ntfy.sh/") + kNtfyTopic +
                                 "/json?poll=1&since=90s");
      }
    }
    // Redraw on state transitions the user can see: data turning stale and
    // the reset countdown crossing a minute boundary.
    const bool stale = IsStale();
    if (stale != shown_stale_) {
      shown_stale_ = stale;
      DrawScreen();
    } else if (screen_ == 0 && stats_.valid && ShownResetMin() != shown_reset_min_) {
      DrawScreen();
    }
  }

  void OnCloudText(const std::string& text) override {
    // Accept either a raw payload (simulator cloud panel) or ntfy's JSON
    // polling format, where the payload sits in the newest "message" field.
    std::string payload;
    if (text.rfind("CW1|", 0) == 0) {
      payload = text;
    } else {
      const size_t last = text.rfind("\"message\":\"CW1|");
      if (last == std::string::npos) return;
      const size_t start = last + 11;
      const size_t end = text.find('"', start);
      if (end == std::string::npos) return;
      payload = text.substr(start, end - start);
    }
    if (ParsePayload(payload)) {
      received_ms_ = now_;
      shown_stale_ = false;
      MaybeAlert();
      if (mode_ == Mode::Watch) DrawScreen();
    }
  }

  void OnTouch(const TouchEvent& event) override {
    if (!event.pressed) return;
    int x = event.x, y = event.y;
    if (flipped_) {
      x = W() - 1 - x;
      y = H() - 1 - y;
    }
    switch (mode_) {
      case Mode::Watch: TouchWatch(x, y); break;
      case Mode::WifiList: TouchWifiList(x, y); break;
      case Mode::WifiKeys: TouchKeyboard(x, y); break;
      case Mode::WifiJoin: break;  // busy joining
      case Mode::WifiFailed:
        Cheeko().speaker().Tone(1200, 25);
        mode_ = Mode::WifiList;
        DrawWifiList();
        break;
    }
  }

 private:
  // ---- Rotation-aware drawing (portrait + 180) ----------------------------

  int W() { return Cheeko().display().Width(); }
  int H() { return Cheeko().display().Height(); }

  void PFillRect(int x, int y, int w, int h, uint32_t rgb) {
    if (flipped_) {
      x = W() - x - w;
      y = H() - y - h;
    }
    Cheeko().display().FillRect(x, y, w, h, rgb);
  }

  void PRect(int x, int y, int w, int h, uint32_t rgb) {
    if (flipped_) {
      x = W() - x - w;
      y = H() - y - h;
    }
    Cheeko().display().Rect(x, y, w, h, rgb);
  }

  void PLine(int x1, int y1, int x2, int y2, uint32_t rgb) {
    if (flipped_) {
      x1 = W() - 1 - x1;
      y1 = H() - 1 - y1;
      x2 = W() - 1 - x2;
      y2 = H() - 1 - y2;
    }
    Cheeko().display().Line(x1, y1, x2, y2, rgb);
  }

  static int TextW(const std::string& text, int scale) {
    return text.empty() ? 0 : (static_cast<int>(text.size()) * 6 - 1) * scale;
  }

  void PText(int x, int y, const std::string& text, int scale, uint32_t rgb) {
    for (size_t i = 0; i < text.size(); ++i) {
      const uint8_t* glyph = watchfont::Glyph(text[i]);
      const int gx = x + static_cast<int>(i) * 6 * scale;
      for (int col = 0; col < 5; ++col) {
        const uint8_t bits = glyph[col];
        int row = 0;
        while (row < 7) {
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

  void PCenterText(int y, const std::string& text, int scale, uint32_t rgb) {
    PText((W() - TextW(text, scale)) / 2, y, text, scale, rgb);
  }

  void PollOrientation() {
    const float gy = Cheeko().motion().Read().y;
    int candidate = -1;
    if (gy > kFlipThresholdG) candidate = 0;
    else if (gy < -kFlipThresholdG) candidate = 1;
    if (candidate < 0 || candidate == (flipped_ ? 1 : 0)) {
      flip_candidate_ = -1;
      return;
    }
    if (candidate != flip_candidate_) {
      flip_candidate_ = candidate;
      flip_since_ = now_;
      return;
    }
    if (now_ - flip_since_ < kFlipDebounceMs) return;
    flip_candidate_ = -1;
    flipped_ = (candidate == 1);
    DrawCurrent();
  }

  void DrawCurrent() {
    switch (mode_) {
      case Mode::Watch: DrawScreen(); break;
      case Mode::WifiList: DrawWifiList(); break;
      case Mode::WifiKeys: DrawKeyboard(); break;
      case Mode::WifiJoin: break;    // static screens; leave mid-join alone
      case Mode::WifiFailed: break;
    }
  }

  // ---- Ring gauge ---------------------------------------------------------

  void DrawTick(int i, uint32_t rgb) {
    const float a = static_cast<float>(i) * 0.10472f - 1.5708f;  // 6 deg steps
    const float ca = std::cos(a), sa = std::sin(a);
    for (int o = -1; o <= 1; ++o) {
      const float px = -sa * o, py = ca * o;
      PLine(static_cast<int>(kRingCx + ca * kRingInner + px + 0.5f),
            static_cast<int>(kRingCy + sa * kRingInner + py + 0.5f),
            static_cast<int>(kRingCx + ca * kRingOuter + px + 0.5f),
            static_cast<int>(kRingCy + sa * kRingOuter + py + 0.5f), rgb);
    }
  }

  // Fills clockwise from 12 o'clock as pct grows.
  void DrawRingPct(int pct, uint32_t rgb) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    const int lit = pct * kTicks / 100;
    for (int i = 0; i < kTicks; ++i) DrawTick(i, i < lit ? rgb : kTickDim);
  }

  static uint32_t LoadColor(int pct) {
    if (pct >= 85) return kRed;
    if (pct >= 60) return Color::Amber;
    return kMint;
  }

  // ---- Payload ------------------------------------------------------------

  bool ParsePayload(const std::string& payload) {
    std::string fields[kFieldCount + 1];
    int count = 0;
    std::string current;
    for (size_t i = 0; i <= payload.size(); ++i) {
      const char c = i < payload.size() ? payload[i] : '|';
      if (c == '|') {
        if (count <= kFieldCount) fields[count] = current;
        ++count;
        current.clear();
      } else {
        current += c;
      }
    }
    if (count < kFieldCount || fields[0] != "CW1") return false;
    Stats s;
    s.valid = true;
    s.plan = fields[1];
    s.window_pct = std::atoi(fields[2].c_str());
    s.budget_pct = std::atoi(fields[3].c_str());
    s.window_k = std::atol(fields[4].c_str());
    s.reset_min = std::atoi(fields[5].c_str());
    s.ctx_k = std::atol(fields[6].c_str());
    s.ctx_pct = std::atoi(fields[7].c_str());
    s.model = fields[8];
    s.today_in_k = std::atol(fields[9].c_str());
    s.today_out_k = std::atol(fields[10].c_str());
    s.today_cw_k = std::atol(fields[11].c_str());
    s.today_cr_k = std::atol(fields[12].c_str());
    s.week_k = std::atol(fields[13].c_str());
    s.billing = fields[14];
    stats_ = s;
    return true;
  }

  // Chirp once when the context (or configured budget) crosses 90%.
  void MaybeAlert() {
    const int pct = stats_.budget_pct >= 0 ? stats_.budget_pct : stats_.ctx_pct;
    if (pct >= 90 && !alerted_90_) {
      alerted_90_ = true;
      Cheeko().speaker().Tone(1319, 90);
      Cheeko().speaker().Tone(1319, 90);
    } else if (pct < 80) {
      alerted_90_ = false;  // re-arm after it drops well below
    }
  }

  bool IsStale() {
    return received_ms_ == 0 || now_ - received_ms_ > kStaleAfterMs;
  }

  // Countdown adjusted by time elapsed since the payload arrived.
  int ShownResetMin() {
    if (!stats_.valid || stats_.reset_min < 0) return -1;
    const int elapsed_min = static_cast<int>((now_ - received_ms_) / 60000u);
    const int remaining = stats_.reset_min - elapsed_min;
    return remaining > 0 ? remaining : 0;
  }

  // ---- Formatting ---------------------------------------------------------

  // Token counts arrive in thousands; render "412K", "1.4M", "18M", "1.2B".
  static std::string FormatK(long k) {
    if (k < 0) k = 0;
    if (k < 1000) return std::to_string(k) + "K";
    if (k < 10000) {
      return std::to_string(k / 1000) + "." + std::to_string((k / 100) % 10) + "M";
    }
    if (k < 1000000) return std::to_string(k / 1000) + "M";
    return std::to_string(k / 1000000) + "." +
           std::to_string((k / 100000) % 10) + "B";
  }

  static std::string FormatHM(int minutes) {
    return std::to_string(minutes / 60) + ":" +
           (minutes % 60 < 10 ? "0" : "") + std::to_string(minutes % 60);
  }

  // ---- Screens ------------------------------------------------------------

  void DrawStatusChip() {
    const bool stale = IsStale();
    const std::string label = stale ? "OFF" : "LIVE";
    const uint32_t color = stale ? kRed : kMint;
    PFillRect(172, 6, 54, 20, kPanel);
    PRect(172, 6, 54, 20, kPanelEdge);
    PText(199 - TextW(label, 2) / 2, 9, label, 2, color);
  }

  void DrawFooter(const std::string& text) {
    PCenterText(278, text, 2, kHint);
  }

  void DrawScreen() {
    if (!stats_.valid) {
      DrawWaiting();
      return;
    }
    switch (screen_) {
      case 0: DrawWindow(); break;
      case 1: DrawContext(); break;
      case 2: DrawTotals(); break;
      default: DrawAbout(); break;
    }
  }

  void DrawWaiting() {
    Cheeko().display().Clear(kBg);
    PCenterText(84, "CLAUDE", 3, kOrange);
    PCenterText(114, "WATCH", 3, kOrange);
    PCenterText(168, "WAITING FOR HOST", 2, kDim);
    PCenterText(192, "RUN CLAUDE WATCH HOST PY", 1, kHint);
    PCenterText(216, Cheeko().wifi().IsConnected() ? "WIFI OK" : "NO WIFI", 2,
                Cheeko().wifi().IsConnected() ? kMint : kRed);
    PCenterText(252, "TAP FOR WIFI SETUP", 2, kMint);
  }

  void DrawWindow() {
    Cheeko().display().Clear(kBg);
    PText(14, 9, stats_.plan, 2, Color::Amber);
    DrawStatusChip();
    const bool idle = stats_.reset_min < 0;
    const int gauge_pct = stats_.budget_pct >= 0 ? stats_.budget_pct
                                                 : stats_.window_pct;
    DrawRingPct(idle ? 0 : gauge_pct,
                stats_.budget_pct >= 0 ? LoadColor(stats_.budget_pct) : kOrange);
    if (idle) {
      PCenterText(134, "IDLE", 4, kDim);
      PCenterText(176, "NO ACTIVE WINDOW", 2, kHint);
    } else {
      const std::string tokens = FormatK(stats_.window_k);
      PText((W() - TextW(tokens, 4)) / 2, 112, tokens, 4, kInk);
      shown_reset_min_ = ShownResetMin();
      PCenterText(152, "THIS 5H WINDOW", 2, kDim);
      PCenterText(178, "RESETS " + FormatHM(shown_reset_min_), 2, kOrange);
    }
    PCenterText(258, "TODAY " + FormatK(TodayTotalK()) + " TOK", 2, kDim);
    DrawFooter("TAP FOR MORE");
  }

  void DrawContext() {
    Cheeko().display().Clear(kBg);
    PText(14, 9, "CONTEXT", 2, kMint);
    DrawStatusChip();
    DrawRingPct(stats_.ctx_pct, LoadColor(stats_.ctx_pct));
    const std::string pct = std::to_string(stats_.ctx_pct) + "%";
    PText((W() - TextW(pct, 5)) / 2, 108, pct, 5, kInk);
    PCenterText(160, FormatK(stats_.ctx_k) + " TOKENS", 2, kDim);
    PCenterText(184, stats_.model, 2, kHint);
    PCenterText(258, "CURRENT SESSION", 2, kDim);
    DrawFooter("TAP FOR MORE");
  }

  void DrawTotalRow(int y, const std::string& label, long k) {
    PText(24, y, label, 2, kDim);
    const std::string value = FormatK(k);
    PText(216 - TextW(value, 2), y, value, 2, kInk);
  }

  void DrawTotals() {
    Cheeko().display().Clear(kBg);
    PText(14, 9, "TOKENS", 2, kOrange);
    DrawStatusChip();
    PText(24, 44, "TODAY", 2, kInk);
    PFillRect(24, 62, 192, 2, kPanelEdge);
    DrawTotalRow(74, "INPUT", stats_.today_in_k);
    DrawTotalRow(98, "OUTPUT", stats_.today_out_k);
    DrawTotalRow(122, "CACHE WRITE", stats_.today_cw_k);
    DrawTotalRow(146, "CACHE READ", stats_.today_cr_k);
    PFillRect(24, 172, 192, 2, kPanelEdge);
    DrawTotalRow(184, "ALL TODAY", TodayTotalK());
    PText(24, 222, "LAST 7 DAYS", 2, kInk);
    PFillRect(24, 240, 192, 2, kPanelEdge);
    DrawTotalRow(252, "ALL", stats_.week_k);
    DrawFooter("TAP FOR MORE");
  }

  void DrawAboutRow(int y, const std::string& label, const std::string& value,
                    uint32_t color) {
    PText(24, y, label, 2, kDim);
    PText(216 - TextW(value, 2), y, value, 2, color);
  }

  void DrawAbout() {
    Cheeko().display().Clear(kBg);
    PText(14, 9, "CLAUDE WATCH", 2, kInk);
    DrawStatusChip();
    DrawAboutRow(56, "PLAN", stats_.plan, Color::Amber);
    DrawAboutRow(84, "VIA", stats_.billing, kInk);
    DrawAboutRow(112, "MODEL", stats_.model, kInk);
    DrawAboutRow(140, "WIFI", Cheeko().wifi().IsConnected() ? "OK" : "DOWN",
                 Cheeko().wifi().IsConnected() ? kMint : kRed);
    DrawAboutRow(168, "FEED", IsStale() ? "STALE" : "LIVE",
                 IsStale() ? kRed : kMint);
    PCenterText(206, "COUNTS ONLY - NO CONTENT", 1, kHint);
    PCenterText(224, "WINDOW + RESET ARE INFERRED", 1, kHint);
    PFillRect(kSetupBox.x, kSetupBox.y, kSetupBox.w, kSetupBox.h, kPanel);
    PRect(kSetupBox.x, kSetupBox.y, kSetupBox.w, kSetupBox.h, kMint);
    PCenterText(kSetupBox.y + 10, "WIFI SETUP", 2, kMint);
  }

  long TodayTotalK() {
    return stats_.today_in_k + stats_.today_out_k + stats_.today_cw_k +
           stats_.today_cr_k;
  }

  // ---- Watch-mode touch ---------------------------------------------------

  void TouchWatch(int x, int y) {
    if (!stats_.valid) {
      StartWifiSetup();
      return;
    }
    if (screen_ == 3 && Hit(x, y, kSetupBox)) {
      StartWifiSetup();
      return;
    }
    screen_ = (screen_ + 1) % 4;
    Cheeko().speaker().Tone(1200, 25);
    DrawScreen();
  }

  // ---- Wi-Fi setup: scan + list -------------------------------------------

  void StartWifiSetup() {
    Cheeko().speaker().Tone(1200, 25);
    mode_ = Mode::WifiList;
    Cheeko().display().Clear(kBg);
    PCenterText(126, "SCANNING", 3, kMint);
    PCenterText(162, "2.4GHZ NETWORKS", 2, kDim);
    RunScan();  // blocks a few seconds on-device
    DrawWifiList();
  }

  void RunScan() {
    WifiNetwork raw[kMaxNets];
    const int found = Cheeko().wifi().Scan(raw, kMaxNets);
    net_count_ = 0;
    for (int i = 0; i < found; ++i) {  // dedup by SSID, keep the strongest AP
      int existing = -1;
      for (int j = 0; j < net_count_; ++j) {
        if (nets_[j].ssid == raw[i].ssid) existing = j;
      }
      if (existing >= 0) {
        if (raw[i].rssi > nets_[existing].rssi) nets_[existing] = raw[i];
      } else if (net_count_ < kMaxNets) {
        nets_[net_count_++] = raw[i];
      }
    }
    for (int i = 1; i < net_count_; ++i) {  // strongest first
      const WifiNetwork current = nets_[i];
      int j = i - 1;
      while (j >= 0 && nets_[j].rssi < current.rssi) {
        nets_[j + 1] = nets_[j];
        --j;
      }
      nets_[j + 1] = current;
    }
  }

  void DrawSignalBars(int x0, int base_y, int rssi) {
    const int strength = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
    for (int b = 0; b < 4; ++b) {
      const int h = 5 + b * 4;
      PFillRect(x0 + b * 6, base_y - h, 4, h, b < strength ? kMint : kTickDim);
    }
  }

  void DrawLock(int x, int y) {  // tiny padlock: shackle over body
    PRect(x + 2, y, 6, 5, kDim);
    PFillRect(x, y + 4, 10, 8, kDim);
  }

  void DrawWifiList() {
    Cheeko().display().Clear(kBg);
    PCenterText(10, "WIFI SETUP", 2, kInk);
    PFillRect((W() - 118) / 2, 28, 118, 3, kMint);
    if (net_count_ == 0) PCenterText(126, "NO NETWORKS FOUND", 2, kDim);
    const int rows = net_count_ < 6 ? net_count_ : 6;
    for (int i = 0; i < rows; ++i) {
      const int y = 42 + i * 34;
      PFillRect(14, y, 212, 30, kPanel);
      PRect(14, y, 212, 30, kPanelEdge);
      std::string name = nets_[i].ssid;
      if (name.size() > 12) name = name.substr(0, 12);
      PText(22, y + 8, name, 2, kInk);
      if (nets_[i].secured) DrawLock(172, y + 9);
      DrawSignalBars(192, y + 24, nets_[i].rssi);
    }
    PFillRect(14, 252, 100, 32, kPanel);
    PRect(14, 252, 100, 32, kPanelEdge);
    PText(64 - TextW("RESCAN", 2) / 2, 261, "RESCAN", 2, kMint);
    PFillRect(126, 252, 100, 32, kPanel);
    PRect(126, 252, 100, 32, kPanelEdge);
    PText(176 - TextW("BACK", 2) / 2, 261, "BACK", 2, kDim);
  }

  void TouchWifiList(int x, int y) {
    const int rows = net_count_ < 6 ? net_count_ : 6;
    for (int i = 0; i < rows; ++i) {
      if (!Hit(x, y, Box{14, 42 + i * 34, 212, 30})) continue;
      Cheeko().speaker().Tone(1200, 25);
      wifi_ssid_ = nets_[i].ssid;
      if (!nets_[i].secured) {  // open network: no password to type
        BeginJoin("");
        return;
      }
      wifi_pass_.clear();
      key_page_ = 0;
      mode_ = Mode::WifiKeys;
      DrawKeyboard();
      return;
    }
    if (Hit(x, y, Box{14, 252, 100, 32})) {
      StartWifiSetup();  // rescan
    } else if (Hit(x, y, Box{126, 252, 100, 32})) {
      Cheeko().speaker().Tone(1200, 25);
      LeaveSetup();
    }
  }

  // Scanning drops any existing connection, so rejoin with the stored
  // credentials (returns immediately when none are stored).
  void LeaveSetup() {
    if (!Cheeko().wifi().IsConnected()) {
      Cheeko().display().Clear(kBg);
      PCenterText(140, "RECONNECTING", 2, kDim);
      Cheeko().wifi().Connect();
    }
    mode_ = Mode::Watch;
    DrawScreen();
  }

  // ---- Wi-Fi setup: password keyboard -------------------------------------

  void DrawPasswordField() {
    PFillRect(13, 29, 214, 26, kPanel);
    std::string shown = wifi_pass_ + "_";
    if (shown.size() > 17) shown = shown.substr(shown.size() - 17);
    PText(18, 35, shown, 2, kInk);
  }

  void DrawKeyButton(int slot, const std::string& label, uint32_t color) {
    const int x = slot * 48;
    PFillRect(x + 1, 234, 46, 34, kPanel);
    PRect(x + 1, 234, 46, 34, kPanelEdge);
    PText(x + 24 - TextW(label, 2) / 2, 244, label, 2, color);
  }

  void DrawKeyboard() {
    Cheeko().display().Clear(kBg);
    std::string title = wifi_ssid_;
    if (title.size() > 18) title = title.substr(0, 18);
    PCenterText(6, title, 2, kMint);
    PRect(12, 28, 216, 28, kPanelEdge);
    DrawPasswordField();
    const char* page = kKeyPages[key_page_];
    for (int i = 0; i < 30; ++i) {
      const int kx = (i % 6) * 40, ky = 64 + (i / 6) * 33;
      PRect(kx + 1, ky + 1, 38, 31, kPanelEdge);
      PText(kx + 15, ky + 9, std::string(1, page[i]), 2, kInk);
    }
    DrawKeyButton(0, kKeyPageLabels[key_page_], kMint);
    DrawKeyButton(1, "SPC", kDim);
    DrawKeyButton(2, "DEL", kOrange);
    DrawKeyButton(3, "OK", kMint);
    DrawKeyButton(4, "X", kRed);
    PCenterText(274, "OK JOINS - X GOES BACK", 1, kHint);
  }

  void TouchKeyboard(int x, int y) {
    if (y >= 64 && y < 229 && x < 240) {  // character grid
      const int idx = ((y - 64) / 33) * 6 + x / 40;
      if (idx >= 0 && idx < 30 && wifi_pass_.size() < kMaxPassword) {
        wifi_pass_ += kKeyPages[key_page_][idx];
        Cheeko().speaker().Tone(1000, 15);
        DrawPasswordField();
      }
      return;
    }
    if (y < 234 || y >= 268) return;  // control row
    const int slot = x / 48;
    Cheeko().speaker().Tone(900, 15);
    if (slot == 0) {
      key_page_ = (key_page_ + 1) % 3;
      DrawKeyboard();
    } else if (slot == 1) {
      if (wifi_pass_.size() < kMaxPassword) {
        wifi_pass_ += ' ';
        DrawPasswordField();
      }
    } else if (slot == 2) {
      if (!wifi_pass_.empty()) wifi_pass_.pop_back();
      DrawPasswordField();
    } else if (slot == 3) {
      if (!wifi_pass_.empty()) BeginJoin(wifi_pass_);
    } else {
      mode_ = Mode::WifiList;
      DrawWifiList();
    }
  }

  // ---- Wi-Fi setup: join --------------------------------------------------

  void BeginJoin(const std::string& password) {
    mode_ = Mode::WifiJoin;
    Cheeko().display().Clear(kBg);
    PCenterText(118, "JOINING", 3, kMint);
    std::string name = wifi_ssid_;
    if (name.size() > 18) name = name.substr(0, 18);
    PCenterText(156, name, 2, kDim);
    Cheeko().wifi().SetCredentials(wifi_ssid_, password);  // blocks up to ~8s
    join_deadline_ = now_ + 6000;
  }

  void TickJoin() {
    if (Cheeko().wifi().IsConnected()) {
      Cheeko().speaker().Tone(880, 70);
      Cheeko().speaker().Tone(1319, 110);
      mode_ = Mode::Watch;
      poll_next_ms_ = 0;  // fetch fresh data right away
      DrawScreen();
      return;
    }
    if (now_ < join_deadline_) return;
    mode_ = Mode::WifiFailed;
    Cheeko().speaker().Tone(240, 200);
    Cheeko().display().Clear(kBg);
    PCenterText(110, "JOIN FAILED", 3, kRed);
    PCenterText(150, "CHECK PASSWORD", 2, kDim);
    PCenterText(174, "TAP TO TRY AGAIN", 2, kHint);
  }

  // ---- State --------------------------------------------------------------

  enum class Mode { Watch, WifiList, WifiKeys, WifiJoin, WifiFailed };
  Mode mode_ = Mode::Watch;
  WifiNetwork nets_[kMaxNets];
  int net_count_ = 0;
  int key_page_ = 0;
  std::string wifi_ssid_;
  std::string wifi_pass_;
  uint32_t join_deadline_ = 0;


  uint32_t now_ = 0;
  uint32_t poll_next_ms_ = 0;
  uint32_t received_ms_ = 0;
  uint32_t last_motion_ms_ = 0;

  bool flipped_ = false;
  int flip_candidate_ = -1;
  uint32_t flip_since_ = 0;

  int screen_ = 0;
  bool shown_stale_ = true;
  int shown_reset_min_ = -1;
  bool alerted_90_ = false;

  Stats stats_;
};

CHEEKO_APP(ClaudeWatchApp);
