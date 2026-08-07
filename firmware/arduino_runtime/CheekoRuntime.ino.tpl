// Cheeko Gotchi runtime sketch — assembled by the Cheeko CLI, do not edit.
//
// Template source: firmware/arduino_runtime/CheekoRuntime.ino.tpl
// The CLI copies this file to build/<app>/<app>.ino next to cheeko.h,
// cheeko_font.h, cheeko_hw.h/.cpp, cheeko_runtime.cpp and the app source,
// then compiles the folder as one Arduino sketch (FQBN in README.md).

#include "cheeko.h"
#include "cheeko_hw.h"

// Emitted by the app's CHEEKO_APP(AppClass) registration macro.
extern "C" cheeko::CheekoApp* CreateCheekoApp();

// Runtime entry points (implemented in cheeko_runtime.cpp).
namespace cheeko_rt {
void RuntimeInit();
void RuntimeAttachApp(cheeko::CheekoApp* app);
void RuntimePoll();
}

void setup() {
  // SAFETY FIRST (SKILL.md Gotcha 8): GPIO 2 is the POWER_OFF latch — left
  // floating or driven HIGH, the board powers itself off mid-boot. This must
  // be the very first thing that runs.
  pinMode(PIN_POWER_OFF, OUTPUT);
  digitalWrite(PIN_POWER_OFF, LOW);

  // Serial, SPI/I2C buses, display, buttons, audio, accelerometer, NVS.
  cheeko_rt::RuntimeInit();

  // Create and start the app.
  cheeko::CheekoApp* app = CreateCheekoApp();
  cheeko_rt::RuntimeAttachApp(app);
  app->OnStart();
}

void loop() {
  // Polls touch (CST810), buttons, and motion (LIS2DH12); dispatches
  // OnTouch/OnButton/OnShake edges; then calls the app's OnTick(millis()).
  cheeko_rt::RuntimePoll();
  delay(5);  // ~5-10ms cadence keeps touch responsive (SKILL.md section 5)
}
