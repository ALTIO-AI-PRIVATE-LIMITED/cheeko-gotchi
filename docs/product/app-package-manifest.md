# App Package Manifest

Every cloud-built or locally-built Cheekoai app should produce a manifest like
this. The manifest is the contract between CLI, cloud builder, marketplace, and
base firmware.

```json
{
  "schema": "ai.cheeko.app.v1",
  "app_id": "com.cheeko.weather_face",
  "name": "Weather Face",
  "version": "0.1.0",
  "sdk": {
    "min": "0.1.0",
    "max": "0.x"
  },
  "boards": ["cheeko-gotchi"],
  "entrypoint": "CreateCheekoApp",
  "permissions": [
    "display",
    "touch",
    "wifi",
    "cloud.fetch",
    "speaker.tone"
  ],
  "artifacts": [
    {
      "type": "native",
      "path": "app.bin",
      "sha256": "..."
    }
  ],
  "rollback": {
    "keep_previous": true,
    "healthcheck_seconds": 15
  },
  "signature": {
    "alg": "ed25519",
    "key_id": "cheekoai-dev-1",
    "value": "..."
  }
}
```

## Permission Names

- `display`
- `touch`
- `buttons`
- `speaker.tone`
- `speaker.audio`
- `mic.level`
- `mic.stream`
- `wifi`
- `cloud.fetch`
- `cloud.voice`
- `storage.local`
- `notifications`

## Install Rules

- Reject package when board is incompatible.
- Reject package when SDK version is incompatible.
- Reject package when signature is missing or invalid.
- Ask owner before granting new permissions.
- Keep last known-good app until new app passes healthcheck.
