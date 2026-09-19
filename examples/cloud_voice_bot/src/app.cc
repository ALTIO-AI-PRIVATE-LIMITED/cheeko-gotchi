#include "cheeko.h"

using namespace cheeko;

class CloudVoiceBotApp : public CheekoApp {
 public:
  void OnStart() override {
    Cheeko().display().CenterText(36, "CONNECTING");
    Cheeko().wifi().Connect();
    Cheeko().cloud().Connect();
    Cheeko().display().CenterText(92, "TAP TO TALK");
  }

  void OnTouch(const TouchEvent& event) override {
    if (event.pressed) {
      Cheeko().display().CenterText(92, "LISTENING");
      Cheeko().cloud().StartVoiceSession();
    }
  }

  void OnCloudText(const std::string& text) override {
    Cheeko().display().CenterText(120, text);
  }
};

CHEEKO_APP(CloudVoiceBotApp);

