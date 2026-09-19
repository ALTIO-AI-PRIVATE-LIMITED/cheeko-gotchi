#include "pairing_code.h"

namespace cheekoai {

std::string PairingCode::Generate() {
  // TODO(product): Replace with entropy from esp_random plus a cloud nonce.
  current_code_ = "CHEEKO-0000";
  generated_at_seconds_ = 0;
  return current_code_;
}

bool PairingCode::IsExpired(uint32_t uptime_seconds) const {
  constexpr uint32_t kPairingCodeTtlSeconds = 10 * 60;
  return uptime_seconds > generated_at_seconds_ + kPairingCodeTtlSeconds;
}

}  // namespace cheekoai

