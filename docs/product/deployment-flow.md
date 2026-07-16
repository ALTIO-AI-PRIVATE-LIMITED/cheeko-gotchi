# Deployment Flow

Cheekoai supports three deployment paths. Wi-Fi OTA is the primary production
path. BLE and USB exist for onboarding, recovery, and developer workflows.

## 1. Phone To Cloud To Device

```text
Owner says: "Make a fridge magnet that shows weather and groceries"
  -> mobile app sends prompt and selected template
  -> cloud creates generation job
  -> app source is validated against SDK permissions
  -> build worker compiles package
  -> package is signed
  -> cloud creates install command for device
  -> Cheekoai heartbeat receives update intent
  -> device downloads package over Wi-Fi
  -> device verifies signature and installs
```

Use this for normal users and developers building on the go.

## 2. Local Developer To Device

```text
cheeko new stock_pet
cheeko build
cheeko deploy --device <id>
```

Use this for software developers who want full local control. The CLI should
reuse the same manifest and package format as the cloud builder.

## 3. USB Recovery

```text
cheeko flash --recovery
```

Use this when Wi-Fi credentials are broken, the device cannot boot an app, or a
developer wants to replace the base firmware.

## BLE Role

BLE should not be the default path for large app packages. It is best for:

- pairing
- Wi-Fi credential transfer
- device status
- small debug commands
- recovery handoff

## Package Safety

Every package must include:

- app id
- semantic version
- SDK version range
- board compatibility
- permissions
- artifact hash
- signature
- rollback metadata

The base firmware must refuse unsigned or incompatible packages.
