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

class PermissionStore {
 public:
  void Grant(const AppIdentity& app, Permission permission);
  bool Allows(const AppIdentity& app, Permission permission) const;

 private:
  struct GrantRecord {
    AppIdentity app;
    Permission permission;
  };

  std::vector<GrantRecord> grants_;
};

}  // namespace cheekoai

