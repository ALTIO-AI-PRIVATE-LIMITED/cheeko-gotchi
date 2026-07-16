#include "wifi_provisioning.h"

namespace cheekoai {

bool WifiProvisioning::HasProvisionedNetwork() const {
  return has_credentials_;
}

void WifiProvisioning::StartAccessPointProvisioning() {
  // TODO(idf): Start esp_wifi softAP and HTTP provisioning endpoint.
}

void WifiProvisioning::ApplyCredentials(const WifiCredentials& credentials) {
  // TODO(idf): Persist credentials in NVS before connecting.
  credentials_ = credentials;
  has_credentials_ = !credentials_.ssid.empty();
}

bool WifiProvisioning::Connect() {
  // TODO(idf): Use esp_netif/esp_wifi and return actual connection state.
  return has_credentials_;
}

}  // namespace cheekoai

