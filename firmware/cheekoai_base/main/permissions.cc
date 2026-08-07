#include "permissions.h"

namespace cheekoai {

bool PermissionFromString(const std::string& name, Permission* out) {
  struct Mapping {
    const char* name;
    Permission permission;
  };
  // Manifest permissions are finer-grained than the firmware flags; the
  // rationale for the collapsed mappings is documented in permissions.h.
  static const Mapping kMappings[] = {
      {"display", Permission::kDisplay},
      {"touch", Permission::kTouch},
      {"buttons", Permission::kTouch},
      {"speaker.tone", Permission::kSpeaker},
      {"speaker.audio", Permission::kSpeaker},
      {"mic.level", Permission::kMicrophone},
      {"mic.stream", Permission::kMicrophone},
      {"wifi", Permission::kNetwork},
      {"cloud.fetch", Permission::kCloud},
      {"cloud.voice", Permission::kCloud},
      {"storage.local", Permission::kStorage},
      {"notifications", Permission::kCloud},
  };

  for (const auto& mapping : kMappings) {
    if (name == mapping.name) {
      if (out != nullptr) {
        *out = mapping.permission;
      }
      return true;
    }
  }
  return false;
}

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

int PermissionStore::GrantFromManifest(const AppIdentity& app,
                                       const std::vector<std::string>& names) {
  int granted = 0;
  for (const auto& name : names) {
    Permission permission = Permission::kDisplay;
    if (!PermissionFromString(name, &permission)) {
      continue;  // Unknown manifest names are skipped.
    }
    // Names that collapse onto an already-held flag still count as granted;
    // the duplicate record is just not stored twice.
    if (!Allows(app, permission)) {
      Grant(app, permission);
    }
    ++granted;
  }
  return granted;
}

}  // namespace cheekoai

