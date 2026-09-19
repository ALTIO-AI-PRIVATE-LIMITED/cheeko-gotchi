# Cheeko Cloud Builder

Cloud Builder is the proposed service layer for creating Cheekoai apps from a
phone prompt, building them in cloud runners, and deploying signed packages to a
paired Cheeko Gotchi device over Wi-Fi OTA, BLE relay, or USB relay.

This folder is a scaffold only. It defines the first API surface, job model, and
security posture without depending on the firmware or SDK internals.

## User Flow

1. The mobile app signs in the owner.
2. The owner pairs a device with QR code, BLE proximity, USB, or a short pairing
   code shown on the device.
3. The owner describes an app in natural language.
4. Cloud Builder creates a generation job and runs Codex/build workers in an
   isolated workspace.
5. The service produces a signed app package and adds it to the owner's app
   registry.
6. The owner taps Deploy. The cloud issues an OTA deployment command.
7. The device confirms install status through heartbeat and deployment events.

## Proposed Paths

| Path | Purpose |
| --- | --- |
| `src/server.js` | Dependency-free HTTP skeleton with route handlers |
| `src/contracts.js` | Shared enums and response helpers |
| `api-contract.md` | Human-readable REST contract |

## Local Syntax Check

```bash
node --check cloud/src/server.js
node --check cloud/src/contracts.js
```

The skeleton intentionally uses only Node built-ins so it can live inside this
SDK repository before the production service stack is chosen.

## Security Notes

- Packages must be signed by Cheeko Cloud Builder before device install.
- Device pairing must bind one owner account to one hardware identity.
- Generated apps must declare permissions such as `display`, `touch`, `wifi`,
  `cloud.fetch`, `cloud.voice`, `speaker.tone`, `mic.stream`, `storage.local`,
  and `notifications`.
- OTA commands must target a specific paired device and package digest.
- Build workers must run in isolated sandboxes with no long-lived credentials.
- Rate limits should apply per owner, per device, and per IP.
- Marketplace publishing requires malware scanning, permission review, and
  provenance metadata.
- Heartbeats must authenticate device identity and should never expose raw owner
  secrets.
