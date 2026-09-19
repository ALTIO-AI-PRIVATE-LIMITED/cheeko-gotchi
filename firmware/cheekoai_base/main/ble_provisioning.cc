#include "ble_provisioning.h"

namespace cheekoai {

void BleProvisioning::StartProvisioningAdvertisement() {
  // TODO(idf): Bind to NimBLE or ESP BLE provisioning once mobile flow lands.
}

void BleProvisioning::StopProvisioningAdvertisement() {
  // TODO(idf): Stop BLE advertising and release provisioning handles.
}

bool BleProvisioning::IsSupported() const {
  return false;
}

}  // namespace cheekoai

