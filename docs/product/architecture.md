# Cheekoai Product Architecture

Cheekoai is a small ESP32-S3 touch-display product that behaves like a pet,
desktop buddy, fridge magnet, and developer board. The product works when the
hardware, SDK, cloud builder, mobile app, and marketplace all agree on one
simple contract: apps are small, permissioned, signed packages that run on top
of a stable device runtime.

## Product Layers

```text
Developer / Owner
  |
  | phone app, web app, CLI, Codex/cloud prompt
  v
Cheekoai Cloud Builder
  |
  | generate -> validate -> build -> sign -> publish
  v
App Package / OTA Artifact
  |
  | Wi-Fi OTA first, BLE/USB for provisioning and recovery
  v
Cheekoai Device Runtime
  |
  | SDK APIs
  v
Display / Touch / Mic / Speaker / Wi-Fi / Storage
```

## Device Responsibilities

- Boot into a trusted base firmware.
- Show a pairing code on first run.
- Provision Wi-Fi through BLE or USB.
- Check cloud for assigned app installs.
- Download signed app packages over Wi-Fi OTA.
- Verify package signature and compatibility.
- Run one active app, then later support multiple installed apps.
- Expose friendly SDK services instead of raw board bring-up.

## Cloud Responsibilities

- Own user accounts, device ownership, and pairing.
- Accept prompts and template selections.
- Generate app source using the Cheekoai SDK contract.
- Validate permissions and API usage.
- Build artifacts in a reproducible build worker.
- Sign app packages.
- Store marketplace metadata, versions, and install commands.
- Track device heartbeat, fleet state, and failed deploys.

## Mobile App Responsibilities

- Pair a physical Cheekoai using a serial code or QR code.
- Configure Wi-Fi credentials locally.
- Browse templates and marketplace apps.
- Record a voice/text request for a new app.
- Show build/deploy status.
- Trigger install, rollback, and app removal.

## Developer Surfaces

- `cheeko new <name>` creates a local app.
- `cheeko build` validates and builds locally when toolchains are installed.
- `cheeko flash` flashes the base firmware or recovery image.
- `cheeko deploy` sends a signed app package to a paired device.
- Web/mobile cloud builder handles the no-laptop flow.

## First MVP Contract

The first product should install one active app at a time.

That keeps the launcher, storage, rollback, and app lifecycle easy to prove.
Multi-app launcher, background services, and revenue marketplace can arrive
after OTA and app signing are stable.
