#include "services/cloud_service.h"

namespace cheekoai {

bool CloudService::ConnectIfPermitted(const PermissionStore& permissions,
                                      const AppIdentity& app) {
  if (!permissions.Allows(app, Permission::kCloud)) {
    connected_ = false;
    return false;
  }
  // TODO(cloud): Open authenticated MQTT/WebSocket session to Cheeko cloud.
  connected_ = true;
  return true;
}

void CloudService::Pump() {
  if (!connected_) {
    return;
  }
  // TODO(cloud): Process inbound app messages, voice replies, and OTA notices.
}

void CloudService::Disconnect() {
  connected_ = false;
}

}  // namespace cheekoai

