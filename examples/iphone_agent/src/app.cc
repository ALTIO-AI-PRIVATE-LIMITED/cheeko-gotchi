#include "cheeko.h"

using namespace cheeko;

// Status face for the iPhone bridge: the firmware does the mirroring and the clicking, the iOS
// app does the thinking, this app shows where things stand and gives the agent a stop button.
class IphoneAgentApp : public CheekoApp {
 public:
  void OnStart() override {
    Cheeko().phone().Start();
    Draw(Cheeko().phone().Status());
  }

  void OnTick(uint32_t uptime_ms) override {
    if (uptime_ms - last_draw_ms_ < 500) {
      return;
    }
    last_draw_ms_ = uptime_ms;
    PhoneBridgeStatus status = Cheeko().phone().Status();
    std::string key = Key(status);
    if (key != shown_) {
      shown_ = key;
      Draw(status);
    }
  }

  void OnButton(const ButtonEvent& event) override {
    if (event.button == ButtonEvent::Button::Power && event.pressed) {
      Cheeko().phone().StopAgent();
    }
  }

 private:
  void Draw(const PhoneBridgeStatus& s) {
    Display& d = Cheeko().display();
    d.Clear(0x000000);
    d.CenterText(16, "iPhone Agent");
    Row(d, 56, "app", s.app_linked ? "linked" : "waiting", s.app_linked);
    Row(d, 90, "usb", s.usb_ready ? "ok" : "plug in", s.usb_ready);
    Row(d, 124, "wifi", s.hotspot_ssid, true);
    d.Text(24, 146, "pass " + s.hotspot_pass);
    Row(d, 172, "mirror", s.mirroring ? "streaming" : "not mirroring", s.mirroring);
    d.Text(24, 210, "last: " + s.last_action);
    d.CenterText(262, "middle button: stop");
  }

  void Row(Display& d, int y, const std::string& label, const std::string& value, bool good) {
    d.FillCircle(12, y + 6, 5, good ? Color::Mint : Color::Amber);
    d.Text(24, y, label + "  " + value);
  }

  std::string Key(const PhoneBridgeStatus& s) {
    return std::to_string(s.app_linked) + std::to_string(s.usb_ready) + std::to_string(s.mirroring) +
           s.hotspot_ssid + "|" + s.hotspot_pass + "|" + s.last_action;
  }

  uint32_t last_draw_ms_ = 0;
  std::string shown_;
};

CHEEKO_APP(IphoneAgentApp);
