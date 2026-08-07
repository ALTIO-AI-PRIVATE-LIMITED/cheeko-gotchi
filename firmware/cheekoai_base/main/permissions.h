#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cheekoai {

enum class Permission : uint32_t {
  kDisplay = 1 << 0,
  kTouch = 1 << 1,
  kMicrophone = 1 << 2,
  kSpeaker = 1 << 3,
  kNetwork = 1 << 4,
  kCloud = 1 << 5,
  kStorage = 1 << 6,
  kOta = 1 << 7,
};

struct AppIdentity {
  std::string app_id;
  std::string publisher;
};

// Maps a fine-grained manifest permission name (see
// schemas/cheekoai-app-manifest.schema.json) onto the coarse firmware
// capability flag that enforces it. Returns false for unknown names and
// leaves *out untouched. Several manifest names collapse onto one flag:
// - "buttons" -> kTouch: the board has no discrete buttons; button-style
//   input is served by touch zones, so both ride the touch capability.
// - "notifications" -> kCloud: notifications are pushed through the cloud
//   connection; there is no local notification surface, so the grant that
//   matters is cloud access.
bool PermissionFromString(const std::string& name, Permission* out);

class PermissionStore {
 public:
  void Grant(const AppIdentity& app, Permission permission);
  bool Allows(const AppIdentity& app, Permission permission) const;

  // Grants every recognized manifest permission name to the app and returns
  // how many names were granted. Unknown names are skipped, not errors, so a
  // newer manifest schema does not brick older firmware.
  int GrantFromManifest(const AppIdentity& app,
                        const std::vector<std::string>& names);

 private:
  struct GrantRecord {
    AppIdentity app;
    Permission permission;
  };

  std::vector<GrantRecord> grants_;
};

}  // namespace cheekoai

