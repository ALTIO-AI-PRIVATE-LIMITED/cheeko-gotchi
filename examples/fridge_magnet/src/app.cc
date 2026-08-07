#include "cheeko.h"

using namespace cheeko;

class FridgeMagnetApp : public CheekoApp {
 public:
  void OnStart() override {
    count_ = Cheeko().storage().GetInt("count", 0);
    if (count_ > kMaxNotes) count_ = kMaxNotes;
    for (int i = 0; i < count_; ++i) {
      notes_[i] = Cheeko().storage().GetString("note" + std::to_string(i), "");
    }
    canned_ = Cheeko().storage().GetInt("canned", 0);
    DrawBoard();
  }

  void OnTick(uint32_t uptime_ms) override {
    uptime_ms_ = uptime_ms;
    if (uptime_ms / 60000 != last_minute_) {
      last_minute_ = uptime_ms / 60000;
      DrawHeader();
    }
  }

  void OnTouch(const TouchEvent& event) override {
    if (!event.pressed) return;
    if (event.y >= kAddZoneY) {
      AddNote();
      return;
    }
    const int slot = (event.y - kListTop) / kRowHeight;
    if (slot >= 0 && slot < count_) RemoveNote(slot);
  }

  void OnShake() override {
    if (count_ == 0) return;
    count_ = 0;
    SaveNotes();
    Cheeko().speaker().Tone(392, 160);
    DrawBoard();
  }

 private:
  static constexpr int kMaxNotes = 6;
  static constexpr int kListTop = 58;
  static constexpr int kRowHeight = 23;
  static constexpr int kAddZoneY = 198;
  static constexpr int kCannedCount = 6;

  void AddNote() {
    static const char* const kCanned[kCannedCount] = {
        "milk", "eggs", "bread", "butter", "apples", "coffee"};
    if (count_ >= kMaxNotes) {
      Cheeko().speaker().Tone(220, 90);
      return;
    }
    notes_[count_++] = kCanned[canned_ % kCannedCount];
    canned_ = (canned_ + 1) % kCannedCount;
    Cheeko().storage().PutInt("canned", canned_);
    SaveNotes();
    Cheeko().speaker().Tone(880, 60);
    DrawBoard();
  }

  void RemoveNote(int slot) {
    for (int i = slot; i + 1 < count_; ++i) notes_[i] = notes_[i + 1];
    count_--;
    SaveNotes();
    Cheeko().speaker().Tone(660, 50);
    DrawBoard();
  }

  void SaveNotes() {
    Cheeko().storage().PutInt("count", count_);
    for (int i = 0; i < count_; ++i) {
      Cheeko().storage().PutString("note" + std::to_string(i), notes_[i]);
    }
    for (int i = count_; i < kMaxNotes; ++i) {
      Cheeko().storage().Remove("note" + std::to_string(i));
    }
  }

  void DrawBoard() {
    auto& display = Cheeko().display();
    display.Clear(0x101820);
    DrawHeader();
    if (count_ == 0) {
      display.CenterText(118, "board clear!");
    }
    for (int i = 0; i < count_; ++i) {
      DrawNote(i);
    }
    display.FillRect(0, kAddZoneY, 240, 296 - kAddZoneY, 0x0d1622);
    display.FillCircle(36, 246, 16, Color::Mint);
    display.FillCircle(204, 246, 16, Color::Mint);
    display.Rect(36, 230, 168, 33, Color::Mint);
    display.CenterText(240, "tap: add note");
    display.CenterText(272, "shake: clear all");
  }

  void DrawHeader() {
    auto& display = Cheeko().display();
    display.FillRect(0, 0, 240, kListTop - 8, 0x101820);
    display.FillCircle(28, 24, 14, Color::Pink);
    display.FillCircle(28, 24, 6, 0x101820);
    const uint32_t mins = uptime_ms_ / 60000;
    display.Text(52, 16,
                 "Fridge  " + std::to_string(mins / 60) + ":" + TwoDigits(mins % 60));
    display.Line(12, kListTop - 10, 228, kListTop - 10, Color::Amber);
  }

  void DrawNote(int index) {
    auto& display = Cheeko().display();
    const int y = kListTop + index * kRowHeight;
    const uint32_t magnet = index % 2 == 0 ? Color::Mint : Color::Amber;
    display.FillCircle(18, y + 11, 7, magnet);
    display.FillCircle(18, y + 11, 3, 0x101820);
    display.Text(34, y + 4, notes_[index]);
    display.Line(34, y + kRowHeight - 4, 210, y + kRowHeight - 4, 0x223244);
  }

  static std::string TwoDigits(uint32_t value) {
    return value < 10 ? "0" + std::to_string(value) : std::to_string(value);
  }

  std::string notes_[kMaxNotes];
  int count_ = 0;
  int canned_ = 0;
  uint32_t uptime_ms_ = 0;
  uint32_t last_minute_ = 0;
};

CHEEKO_APP(FridgeMagnetApp);
