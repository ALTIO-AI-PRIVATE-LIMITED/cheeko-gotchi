#include "cheeko.h"

using namespace cheeko;

class HelloDisplayApp : public CheekoApp {
 public:
  void OnStart() override {
    Cheeko().display().Clear(0x101820);
    Cheeko().display().CenterText(72, "Hello Cheeko");
    Cheeko().speaker().Tone(880, 80);
  }
};

CHEEKO_APP(HelloDisplayApp);

