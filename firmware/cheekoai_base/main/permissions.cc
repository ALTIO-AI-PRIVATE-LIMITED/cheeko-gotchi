#include "permissions.h"

namespace cheekoai {

void PermissionStore::Grant(const AppIdentity& app, Permission permission) {
  grants_.push_back({app, permission});
}

bool PermissionStore::Allows(const AppIdentity& app,
                             Permission permission) const {
  for (const auto& grant : grants_) {
    if (grant.app.app_id == app.app_id && grant.permission == permission) {
      return true;
    }
  }
  return false;
}

}  // namespace cheekoai

