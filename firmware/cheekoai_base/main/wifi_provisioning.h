#pragma once

#include <string>

namespace cheekoai {

struct WifiCredentials {
  std::string ssid;
  std::string password;
};

class WifiProvisioning {
 public:
  bool HasProvisionedNetwork() const;
  void StartAccessPointProvisioning();
  void ApplyCredentials(const WifiCredentials& credentials);
  bool Connect();

 private:
  WifiCredentials credentials_;
  bool has_credentials_ = false;
};

}  // namespace cheekoai

