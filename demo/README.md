# Local Product Loop Demo

This demo proves the Cheekoai product loop without hardware:

1. Phone/cloud creates a pairing session.
2. Simulated device claims ownership.
3. Cloud builder generates an app package and manifest.
4. Owner commands a Wi-Fi OTA deployment.
5. Simulated device heartbeats, receives the command, verifies the artifact, and
   reports install success.

Run:

```bash
node demo/run-local-loop.js
```

The script writes the latest run to `demo/out/latest-run.json` and the generated
manifest to `demo/out/generated-manifest.json`.

This is not a replacement for ESP32 hardware testing. It is the local proof that
cloud, mobile, package, OTA command, and device state agree on the same contract.
