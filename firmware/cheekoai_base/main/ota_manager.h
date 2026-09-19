#pragma once

#include <string>

namespace cheekoai {

struct OtaManifest {
  std::string version;
  std::string image_url;
  std::string signature;
};

class OtaManager {
 public:
  bool HasPendingUpdate() const;
  bool LastBootFailed() const;
  bool ValidateManifest(const OtaManifest& manifest) const;
  bool InstallPendingUpdate();
  void MarkBootSuccessful();

 private:
  OtaManifest pending_manifest_;
  bool has_pending_update_ = false;
  bool last_boot_failed_ = false;
};

}  // namespace cheekoai

