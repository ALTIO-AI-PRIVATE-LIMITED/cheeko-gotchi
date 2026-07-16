#pragma once

#include "permissions.h"

namespace cheekoai {

class CloudService {
 public:
  bool ConnectIfPermitted(const PermissionStore& permissions,
                          const AppIdentity& app);
  void Pump();
  void Disconnect();

 private:
  bool connected_ = false;
};

}  // namespace cheekoai

