#pragma once

#include <cstdint>

namespace cheekoai {

class AudioService {
 public:
  void Initialize();
  void StartMicrophone(uint32_t sample_rate_hz);
  void PlayTone(uint32_t frequency_hz, uint32_t duration_ms);
  void StopAll();
};

}  // namespace cheekoai

