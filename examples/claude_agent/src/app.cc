#include "cheeko.h"

using namespace cheeko;

namespace {

constexpr int kQuestionCount = 4;
const char* const kClaudeQuestions[kQuestionCount] = {
    "Claude, tell me one surprising science fact.",
    "Claude, write a haiku about a tiny robot.",
    "Claude, give me a two-line pep talk.",
    "Claude, what should I learn today?",
};

}  // namespace

class ClaudeAgentApp : public CheekoApp {
 public:
  void OnStart() override {
    Cheeko().wifi().Connect();
    Cheeko().cloud().Connect();
    Cheeko().display().Clear(0x101820);
    DrawFace(false);
    DrawReply();
    DrawPrompt("TAP TO ASK");
  }

  void OnTick(uint32_t uptime_ms) override {
    uptime_ms_ = uptime_ms;
    const bool talking = line_count_ > 0 && uptime_ms - reply_at_ms_ < 3000;
    const bool mouth_open = talking && ((uptime_ms / 180) % 2 == 0);
    if (mouth_open != mouth_open_) {
      mouth_open_ = mouth_open;
      DrawFace(mouth_open);
    }
  }

  void OnTouch(const TouchEvent& event) override {
    if (!event.pressed || waiting_) return;
    waiting_ = true;
    line_count_ = 0;
    scroll_ = 0;
    Cheeko().cloud().Connect();
    Cheeko().cloud().SendText(kClaudeQuestions[question_index_]);
    question_index_ = (question_index_ + 1) % kQuestionCount;
    DrawReply();
    DrawPrompt("Claude is thinking...");
  }

  void OnCloudText(const std::string& text) override {
    waiting_ = false;
    reply_at_ms_ = uptime_ms_;
    WrapReply(text);
    scroll_ = 0;
    DrawReply();
    DrawPrompt("TAP TO ASK");
  }

  void OnButton(const ButtonEvent& event) override {
    if (!event.pressed) return;
    if (event.button == ButtonEvent::Button::VolumeUp && scroll_ > 0) {
      scroll_--;
      DrawReply();
    }
    if (event.button == ButtonEvent::Button::VolumeDown &&
        scroll_ + kVisibleLines < line_count_) {
      scroll_++;
      DrawReply();
    }
  }

 private:
  // 19 chars at the 12px glyph advance fills the 228px text column.
  static constexpr size_t kCharsPerLine = 19;
  static constexpr int kTextX = 6;
  static constexpr int kLineHeight = 18;
  static constexpr int kVisibleLines = 7;
  static constexpr int kMaxLines = 24;
  static constexpr int kReplyTop = 126;

  void WrapReply(const std::string& text) {
    line_count_ = 0;
    std::string line;
    std::string word;
    for (size_t i = 0; i <= text.size(); ++i) {
      const char c = i < text.size() ? text[i] : ' ';
      if (c != ' ' && c != '\n') {
        word += c;
        continue;
      }
      if (word.empty()) continue;
      if (!line.empty() && line.size() + 1 + word.size() > kCharsPerLine) {
        PushLine(line);
        line.clear();
      }
      while (word.size() > kCharsPerLine) {
        PushLine(word.substr(0, kCharsPerLine));
        word = word.substr(kCharsPerLine);
      }
      if (!line.empty()) line += ' ';
      line += word;
      word.clear();
    }
    if (!line.empty()) PushLine(line);
  }

  void PushLine(const std::string& line) {
    if (line_count_ < kMaxLines) lines_[line_count_++] = line;
  }

  void DrawFace(bool mouth_open) {
    auto& display = Cheeko().display();
    display.FillRect(0, 0, 240, kReplyTop - 6, 0x101820);
    display.FillCircle(120, 58, 40, Color::Orange);
    display.FillCircle(104, 50, 6, Color::Black);
    display.FillCircle(136, 50, 6, Color::Black);
    if (mouth_open) {
      display.FillCircle(120, 72, 8, Color::Black);
    } else {
      display.Line(110, 72, 130, 72, Color::Black);
    }
    display.CenterText(104, "Claude Agent");
  }

  void DrawReply() {
    auto& display = Cheeko().display();
    display.FillRect(0, kReplyTop - 4, 240, kVisibleLines * kLineHeight + 8,
                     0x101820);
    if (line_count_ == 0) {
      display.CenterText(kReplyTop + 36, waiting_ ? "..." : "ask me anything");
      return;
    }
    for (int i = 0; i < kVisibleLines && scroll_ + i < line_count_; ++i) {
      display.Text(kTextX, kReplyTop + i * kLineHeight, lines_[scroll_ + i]);
    }
  }

  void DrawPrompt(const std::string& text) {
    auto& display = Cheeko().display();
    display.FillRect(0, 266, 240, 30, 0x101820);
    display.CenterText(272, text);
    if (line_count_ > kVisibleLines) {
      display.Text(180, 254, "vol:^v");
    }
  }

  std::string lines_[kMaxLines];
  int line_count_ = 0;
  int scroll_ = 0;
  int question_index_ = 0;
  bool waiting_ = false;
  bool mouth_open_ = false;
  uint32_t uptime_ms_ = 0;
  uint32_t reply_at_ms_ = 0;
};

CHEEKO_APP(ClaudeAgentApp);
