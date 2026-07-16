#include "services/display_service.h"

namespace cheekoai {

void DisplayService::Initialize() {
  // TODO(board): Initialize LCD panel, backlight, rotation, and frame buffer.
}

void DisplayService::ShowBootSplash(const std::string& product_name) {
  (void)product_name;
  // TODO(ui): Draw product splash once display adapter is connected.
}

void DisplayService::ShowPairingCode(const std::string& code) {
  (void)code;
  // TODO(ui): Render pairing QR and short code.
}

void DisplayService::ShowRecovery(const std::string& reason) {
  (void)reason;
  // TODO(ui): Render recovery instructions and support code.
}

void DisplayService::ShowLauncher() {
  // TODO(ui): Render installed app launcher or default Cheeko face.
}

}  // namespace cheekoai

