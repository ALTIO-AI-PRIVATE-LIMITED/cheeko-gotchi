#pragma once

namespace cheekoai {

class BleProvisioning {
 public:
  void StartProvisioningAdvertisement();
  void StopProvisioningAdvertisement();
  bool IsSupported() const;
};

}  // namespace cheekoai

