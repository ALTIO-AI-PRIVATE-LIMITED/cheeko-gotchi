#include "cheeko.h"

using namespace cheeko;

namespace {

struct MazeRect {
  int x;
  int y;
  int w;
  int h;
};

constexpr MazeRect kWalls[] = {
    {0, 0, 240, 36},  {0, 36, 8, 260},   {232, 36, 8, 260}, {0, 288, 240, 8},
    {8, 100, 152, 8}, {80, 168, 152, 8}, {8, 236, 168, 8},
};
constexpr int kWallCount = sizeof(kWalls) / sizeof(kWalls[0]);
constexpr MazeRect kGoal = {194, 254, 30, 28};

constexpr int kBallRadius = 6;
constexpr float kStartX = 24.0f;
constexpr float kStartY = 64.0f;
constexpr uint32_t kBackground = 0x101820;
constexpr uint32_t kWallColor = 0x2a3a4e;

}  // namespace

class TiltMazeApp : public CheekoApp {
 public:
  void OnStart() override {
    wins_ = Cheeko().storage().GetInt("wins", 0);
    ResetBall();
    DrawBoard();
  }

  void OnTick(uint32_t /*uptime_ms*/) override {
    const MotionSample m = Cheeko().motion().Read();
    // Read() reports g; the 0.004 gain from SKILL.md section 7 is tuned for
    // milli-g, and both axes are negated on this board in portrait.
    const float tilt_x = -m.x * 1000.0f;
    const float tilt_y = -m.y * 1000.0f;
    vel_x_ = Clamp(vel_x_ + tilt_x * 0.004f, -6.0f, 6.0f) * 0.985f;
    vel_y_ = Clamp(vel_y_ + tilt_y * 0.004f, -6.0f, 6.0f) * 0.985f;

    const int prev_x = static_cast<int>(x_);
    const int prev_y = static_cast<int>(y_);

    x_ += vel_x_;
    if (HitsWall()) {
      x_ -= vel_x_;
      vel_x_ = 0.0f;
    }
    y_ += vel_y_;
    if (HitsWall()) {
      y_ -= vel_y_;
      vel_y_ = 0.0f;
    }

    DrawBallStep(prev_x, prev_y);
    if (InGoal()) Win();
  }

  void OnShake() override {
    ResetBall();
    Cheeko().speaker().Tone(440, 60);
    DrawBoard();
  }

 private:
  void Win() {
    wins_++;
    Cheeko().storage().PutInt("wins", wins_);
    Cheeko().speaker().Tone(1320, 180);
    ResetBall();
    DrawBoard();
  }

  void ResetBall() {
    x_ = kStartX;
    y_ = kStartY;
    vel_x_ = 0.0f;
    vel_y_ = 0.0f;
  }

  bool HitsWall() const {
    const int bx = static_cast<int>(x_) - kBallRadius;
    const int by = static_cast<int>(y_) - kBallRadius;
    const int bs = kBallRadius * 2;
    for (int i = 0; i < kWallCount; ++i) {
      if (bx < kWalls[i].x + kWalls[i].w && bx + bs > kWalls[i].x &&
          by < kWalls[i].y + kWalls[i].h && by + bs > kWalls[i].y) {
        return true;
      }
    }
    return false;
  }

  bool InGoal() const {
    const int bx = static_cast<int>(x_);
    const int by = static_cast<int>(y_);
    return bx >= kGoal.x && bx < kGoal.x + kGoal.w && by >= kGoal.y &&
           by < kGoal.y + kGoal.h;
  }

  // Only the ball's previous bounding box is erased and repainted each tick;
  // a full Clear() per frame is visibly slow on real hardware.
  void DrawBallStep(int prev_x, int prev_y) {
    auto& display = Cheeko().display();
    const int ex = prev_x - kBallRadius - 1;
    const int ey = prev_y - kBallRadius - 1;
    const int es = kBallRadius * 2 + 2;
    display.FillRect(ex, ey, es, es, kBackground);
    RepaintRect(kGoal, ex, ey, es, Color::Mint);
    for (int i = 0; i < kWallCount; ++i) {
      RepaintRect(kWalls[i], ex, ey, es, kWallColor);
    }
    display.FillCircle(static_cast<int>(x_), static_cast<int>(y_), kBallRadius,
                       Color::Amber);
  }

  void RepaintRect(const MazeRect& rect, int ex, int ey, int es, uint32_t rgb) {
    const int x1 = rect.x > ex ? rect.x : ex;
    const int y1 = rect.y > ey ? rect.y : ey;
    const int x2 = rect.x + rect.w < ex + es ? rect.x + rect.w : ex + es;
    const int y2 = rect.y + rect.h < ey + es ? rect.y + rect.h : ey + es;
    if (x1 < x2 && y1 < y2) {
      Cheeko().display().FillRect(x1, y1, x2 - x1, y2 - y1, rgb);
    }
  }

  void DrawBoard() {
    auto& display = Cheeko().display();
    display.Clear(kBackground);
    for (int i = 0; i < kWallCount; ++i) {
      display.FillRect(kWalls[i].x, kWalls[i].y, kWalls[i].w, kWalls[i].h,
                       kWallColor);
    }
    display.FillRect(kGoal.x, kGoal.y, kGoal.w, kGoal.h, Color::Mint);
    display.Text(12, 10, "Tilt Maze");
    display.Text(140, 10, "wins: " + std::to_string(wins_));
    display.FillCircle(static_cast<int>(x_), static_cast<int>(y_), kBallRadius,
                       Color::Amber);
  }

  static float Clamp(float value, float lo, float hi) {
    return value < lo ? lo : value > hi ? hi : value;
  }

  float x_ = kStartX;
  float y_ = kStartY;
  float vel_x_ = 0.0f;
  float vel_y_ = 0.0f;
  int wins_ = 0;
};

CHEEKO_APP(TiltMazeApp);
