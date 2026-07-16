#include "cheeko.h"

using namespace cheeko;

class WeatherNodeApp : public CheekoApp {
 public:
  void OnStart() override {
    Cheeko().wifi().Connect();
    Cheeko().cloud().Connect();
    Cheeko().display().Clear(0x071018);
    Cheeko().display().CenterText(40, "Weather Node");
    FetchWeather(0);
  }

  void OnTick(uint32_t uptime_ms) override {
    if (uptime_ms - last_fetch_ms_ > 15 * 60 * 1000) {
      FetchWeather(uptime_ms);
    }
  }

  void OnCloudText(const std::string& text) override {
    Cheeko().display().Clear(0x071018);
    Cheeko().display().CenterText(36, "Weather");
    Cheeko().display().CenterText(96, text);
    Cheeko().display().CenterText(180, "tap for voice");
  }

  void OnTouch(const TouchEvent& event) override {
    if (event.pressed) {
      Cheeko().cloud().StartVoiceSession();
    }
  }

 private:
  void FetchWeather(uint32_t uptime_ms) {
    last_fetch_ms_ = uptime_ms;
    Cheeko().cloud().GetJson("cheeko://weather/current?units=metric");
    Cheeko().display().CenterText(126, "syncing forecast");
  }

  uint32_t last_fetch_ms_ = 0;
};

CHEEKO_APP(WeatherNodeApp);
