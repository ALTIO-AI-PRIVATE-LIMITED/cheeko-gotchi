#pragma once

#include <cstdint>
#include <string>

namespace cheekoai {

class PairingCode {
 public:
  // Generates a fresh "CHEEKO-XXXX" code from the supplied entropy word and
  // records now_seconds as the generation time for expiry checks.
  std::string Generate(uint32_t entropy, uint32_t now_seconds);
  bool IsExpired(uint32_t uptime_seconds) const;
  const std::string& current_code() const { return current_code_; }

 private:
  std::string current_code_;
  uint32_t generated_at_seconds_ = 0;
};

}  // namespace cheekoai

