#include "cheeko.h"

using namespace cheeko;

class MarketPulseApp : public CheekoApp {
 public:
  void OnStart() override {
    Cheeko().wifi().Connect();
    Cheeko().cloud().Connect();
    Cheeko().display().Clear(0x06090e);
    Cheeko().display().CenterText(32, "Market Pulse");
    Refresh();
  }

  void OnTick(uint32_t uptime_ms) override {
    if (uptime_ms - last_refresh_ms_ > 60 * 1000) {
      Refresh();
      last_refresh_ms_ = uptime_ms;
    }
  }

  void OnCloudText(const std::string& text) override {
    Cheeko().display().Clear(0x06090e);
    Cheeko().display().CenterText(42, "Watchlist");
    Cheeko().display().Text(18, 94, text);
    Cheeko().display().CenterText(250, "tap: explain move");
  }

  void OnTouch(const TouchEvent& event) override {
    if (event.pressed) {
      Cheeko().cloud().SendText("Explain my watchlist movement in one sentence.");
    }
  }

 private:
  void Refresh() {
    Cheeko().cloud().GetJson("cheeko://market/watchlist?symbols=AAPL,NVDA,BTC");
  }

  uint32_t last_refresh_ms_ = 0;
};

CHEEKO_APP(MarketPulseApp);
