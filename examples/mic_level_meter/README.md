# Mic Level Meter

Purpose: verify that microphone capture is working and reaching your app.

Expected behavior:

- Start microphone capture at 24 kHz.
- Compute RMS level from incoming frames.
- Draw a simple level bar.
- Tapping near the microphones should move the bar.

The runtime owns the capture path. Your app just receives PCM frames from
`Cheeko().audio()` — see [`docs/sdk/api-reference.md`](../../docs/sdk/api-reference.md).
