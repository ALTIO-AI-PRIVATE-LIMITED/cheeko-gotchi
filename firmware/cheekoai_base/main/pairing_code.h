#pragma once

#include <cstdint>
#include <string>

namespace cheekoai {

class PairingCode {
 public:
  std::string Generate();
  bool IsExpired(uint32_t uptime_seconds) const;
  const std::string& current_code() const { return current_code_; }

 private:
  std::string current_code_;
  uint32_t generated_at_seconds_ = 0;
};

}  // namespace cheekoai

