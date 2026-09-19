#pragma once

#include <string>

namespace cheekoai {

class DisplayService {
 public:
  void Initialize();
  void ShowBootSplash(const std::string& product_name);
  void ShowPairingCode(const std::string& code);
  void ShowRecovery(const std::string& reason);
  void ShowLauncher();
};

}  // namespace cheekoai

