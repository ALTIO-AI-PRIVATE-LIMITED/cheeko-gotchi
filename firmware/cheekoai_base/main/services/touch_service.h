#pragma once

#include <cstdint>

namespace cheekoai {

struct TouchPoint {
  int16_t x = 0;
  int16_t y = 0;
  bool pressed = false;
};

class TouchService {
 public:
  void Start();
  void Poll();
  void Stop();
  TouchPoint latest_point() const { return latest_point_; }

 private:
  TouchPoint latest_point_;
};

}  // namespace cheekoai

