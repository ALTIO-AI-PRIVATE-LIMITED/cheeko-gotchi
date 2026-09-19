#include "ota_manager.h"

namespace cheekoai {

bool OtaManager::HasPendingUpdate() const {
  return has_pending_update_;
}

bool OtaManager::LastBootFailed() const {
  return last_boot_failed_;
}

bool OtaManager::ValidateManifest(const OtaManifest& manifest) const {
  return !manifest.version.empty() && !manifest.image_url.empty() &&
         !manifest.signature.empty();
}

bool OtaManager::InstallPendingUpdate() {
  if (!has_pending_update_ || !ValidateManifest(pending_manifest_)) {
    return false;
  }
  // TODO(idf): Verify signature, run esp_https_ota, and set rollback state.
  return true;
}

void OtaManager::MarkBootSuccessful() {
  // TODO(idf): Call esp_ota_mark_app_valid_cancel_rollback.
  last_boot_failed_ = false;
}

}  // namespace cheekoai

