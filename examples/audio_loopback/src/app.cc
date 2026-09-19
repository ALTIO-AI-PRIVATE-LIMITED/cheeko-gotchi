#include "cheeko.h"

using namespace cheeko;

class AudioLoopbackApp : public CheekoApp {
 public:
  void OnStart() override {
    Cheeko().speaker().SetVolume(20);
    Cheeko().mic().Start(24000);
    Cheeko().display().CenterText(42, "AUDIO LOOPBACK");
  }

  void OnMicAudio(const AudioFrame& frame) override {
    // Runtime TODO: expose a safe PCM playback path for advanced audio apps.
    (void)frame;
  }
};

CHEEKO_APP(AudioLoopbackApp);

