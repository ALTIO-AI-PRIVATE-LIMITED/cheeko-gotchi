# Self Test

The first app every developer should run after receiving a device. It walks
through every hardware block, one tap at a time, and is written entirely against
the public SDK ([`sdk/include/cheeko.h`](../../sdk/include/cheeko.h)) — a good
template for how an app should be structured.

Source: [`src/app.cc`](src/app.cc)

## Flow

`Intro → Display → Touch → Speaker → Mic → Wi-Fi → Done`

Tap to advance at each step. On **Done** the result is saved with `storage()`
(`self_test_passed`, `self_test_wifi`) and every step logs via `log()`.

## Test Checklist

1. **Display** — color bars (red, orange, yellow, green, cyan, blue, white).
   - Red must look red. Orange/yellow must not look blue. White must look white.
   - Tap when the colors look right.
2. **Touch** — tap the screen `5` times; a marker tracks each touch and the
   counter advances. Auto-continues when complete.
3. **Speaker** — a tone plays on entry. Press **VOL+** to replay. Tap = "I heard
   it", continue.
4. **Microphone** — speak or tap near the mics; the level bar moves. Auto-passes
   when the level crosses threshold. Tap to skip.
5. **Wi-Fi** — connects to the provisioned network and confirms via
   `wifi().IsConnected()`, with a 12 s timeout. Tap to skip.

## Notes

- The v0.1 SDK exposes `wifi().Connect()` / `IsConnected()` but no SSID scan, so
  this step is connect-and-confirm rather than a network list. A scan API is a
  candidate for a later SDK version.
- This app touches no hardware directly — display, touch, speaker, mic, and
  Wi-Fi all go through the SDK runtime. Pin/codec details stay in the board
  support package.
