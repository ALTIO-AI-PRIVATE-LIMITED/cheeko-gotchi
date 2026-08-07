#include "pairing_code.h"

namespace cheekoai {

std::string PairingCode::Generate(uint32_t entropy, uint32_t now_seconds) {
  // Unambiguous alphabet: no 0/O or 1/I, so codes are easy to read aloud and
  // retype from the device screen.
  static const char kAlphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
  constexpr uint32_t kAlphabetSize = sizeof(kAlphabet) - 1;

  std::string code = "CHEEKO-";
  uint32_t mixed = entropy;
  for (int i = 0; i < 4; ++i) {
    // xorshift step so every output character draws on the full entropy word.
    mixed ^= mixed << 13;
    mixed ^= mixed >> 17;
    mixed ^= mixed << 5;
    code.push_back(kAlphabet[mixed % kAlphabetSize]);
  }

  current_code_ = code;
  generated_at_seconds_ = now_seconds;
  return current_code_;
}

bool PairingCode::IsExpired(uint32_t uptime_seconds) const {
  constexpr uint32_t kPairingCodeTtlSeconds = 10 * 60;
  return uptime_seconds > generated_at_seconds_ + kPairingCodeTtlSeconds;
}

}  // namespace cheekoai
