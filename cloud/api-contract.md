# Cloud Builder API Contract

Base URL: `https://api.cheekoai.example/v1`

All owner-facing endpoints require an owner access token. Device endpoints use
device credentials established during pairing.

## Device Pairing

### `POST /devices/pairing-sessions`

Creates a short-lived pairing session for a phone and device.

Request:

```json
{
  "ownerId": "owner_123",
  "pairingMethod": "qr_code",
  "deviceClaim": {
    "serial": "CG-2026-0001",
    "hardwareModel": "cheeko-gotchi"
  }
}
```

Response:

```json
{
  "pairingSessionId": "pair_123",
  "pairingCode": "482913",
  "expiresAt": "2026-06-12T10:15:00Z",
  "status": "pending"
}
```

### `POST /devices/:deviceId/claim`

Completes owner pairing after the device proves possession of the pairing code.

## App Generation Job

### `POST /apps/generation-jobs`

Starts a cloud generation/build job from a mobile prompt.

Request:

```json
{
  "ownerId": "owner_123",
  "deviceId": "dev_123",
  "prompt": "Make a tiny weather companion that speaks the forecast",
  "targetSdk": "0.1",
  "requestedPermissions": ["display", "touch", "wifi", "cloud.fetch", "speaker.tone"]
}
```

Response:

```json
{
  "jobId": "job_123",
  "status": "queued",
  "statusUrl": "/v1/apps/generation-jobs/job_123"
}
```

### `GET /apps/generation-jobs/:jobId`

Returns job status, build logs, generated app metadata, and artifact link when
available.

## Build Artifact

### `GET /apps/:appId/artifacts/:artifactId`

Returns signed artifact metadata and a short-lived download URL.

Response:

```json
{
  "artifactId": "art_123",
  "appId": "app_123",
  "version": "1.0.0",
  "packageDigest": "sha256:...",
  "signature": "base64-signature",
  "downloadUrl": "https://storage.cheekoai.example/art_123?token=short-lived",
  "expiresAt": "2026-06-12T10:15:00Z"
}
```

## OTA Deployment Command

### `POST /devices/:deviceId/deployments`

Issues an install command to a paired device.

Request:

```json
{
  "appId": "app_123",
  "artifactId": "art_123",
  "transport": "wifi_ota",
  "installMode": "replace_current"
}
```

Response:

```json
{
  "deploymentId": "dep_123",
  "status": "commanded",
  "deviceId": "dev_123"
}
```

Transport values: `wifi_ota`, `ble_relay`, `usb_relay`.

### `POST /devices/:deviceId/deployments/:deploymentId/report`

Device-authenticated install progress or final install result.

Request:

```json
{
  "status": "installed",
  "appId": "com.cheeko.weather_face",
  "message": "verified manifest and activated app"
}
```

## App Registry And Marketplace

### `GET /owners/:ownerId/apps`

Lists private apps owned by the user.

### `POST /marketplace/apps`

Submits an app for public listing after review.

### `GET /marketplace/apps`

Lists approved public apps with permission labels, compatibility metadata, and
install counts.

## Device Heartbeat

### `POST /devices/:deviceId/heartbeat`

Device-authenticated status update.

Request:

```json
{
  "firmwareVersion": "0.1.0",
  "batteryPercent": 84,
  "network": "wifi",
  "installedApps": ["app_123"],
  "activeDeploymentId": "dep_123"
}
```

Response:

```json
{
  "accepted": true,
  "serverTime": "2026-06-12T10:00:00Z",
  "pendingCommands": []
}
```

## Security Requirements

- Every artifact must include package digest, signing certificate id, SDK target,
  permission manifest, source provenance, and build runner id.
- Device install must verify signature, digest, owner binding, compatibility, and
  requested permissions before launch.
- Pairing sessions expire quickly and can be used once.
- Owner accounts can revoke devices and deployments.
- Rate limits apply to pairing creation, generation jobs, artifact downloads,
  deployment commands, marketplace submissions, and heartbeats.
