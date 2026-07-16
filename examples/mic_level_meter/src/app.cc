#include "cheeko.h"

#include <algorithm>
#include <cmath>

using namespace cheeko;

class MicLevelMeterApp : public CheekoApp {
 public:
  void OnStart() override {
    Cheeko().display().Clear(0x000000);
    Cheeko().display().CenterText(16, "MIC LEVEL");
    Cheeko().mic().Start(24000);
  }

  void OnMicAudio(const AudioFrame& frame) override {
    if (frame.samples == nullptr || frame.sample_count == 0) {
      return;
    }

    double sum = 0.0;
    for (size_t i = 0; i < frame.sample_count; ++i) {
      const double sample = frame.samples[i] / 32768.0;
      sum += sample * sample;
    }
    const double rms = std::sqrt(sum / frame.sample_count);
    const int percent = std::clamp(static_cast<int>(rms * 400.0), 0, 100);
    Cheeko().display().CenterText(92, std::to_string(percent) + "%");
  }
};

CHEEKO_APP(MicLevelMeterApp);

