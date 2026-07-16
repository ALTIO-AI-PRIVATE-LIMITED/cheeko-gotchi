#include "services/audio_service.h"

namespace cheekoai {

void AudioService::Initialize() {
  // TODO(board): Initialize codec, I2S clocks, gain, and speaker amp.
}

void AudioService::StartMicrophone(uint32_t sample_rate_hz) {
  (void)sample_rate_hz;
  // TODO(board): Start DMA-backed microphone capture.
}

void AudioService::PlayTone(uint32_t frequency_hz, uint32_t duration_ms) {
  (void)frequency_hz;
  (void)duration_ms;
  // TODO(audio): Route generated PCM to speaker output.
}

void AudioService::StopAll() {
  // TODO(board): Stop I2S streams and mute amplifier.
}

}  // namespace cheekoai

