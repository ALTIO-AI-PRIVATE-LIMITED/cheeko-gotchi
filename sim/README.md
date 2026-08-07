# Cheeko Gotchi Simulator

Run any Cheeko app on your computer — no device, no ESP-IDF, no Arduino
toolchain. The simulator implements the whole public SDK
([`sdk/include/cheeko.h`](../sdk/include/cheeko.h)) on the host and shows the
240x296 screen in your browser.

```bash
python tools/cheeko run examples/cheeko_pet     # Windows
./tools/cheeko run examples/cheeko_pet          # macOS / Linux
```

That compiles the app with your local `g++`, starts a tiny local web server,
and opens `http://localhost:8123/`.

## What works in the simulator

| SDK area | Behavior |
| --- | --- |
| `display()` | Full drawing API rendered to a canvas (2x zoom) |
| `touch()` / `OnTouch` | Click = tap, drag = swipe |
| `OnButton` | BOOT / POWER / VOL+ / VOL- buttons under the screen |
| `motion()` / `OnShake` | Tilt sliders + a SHAKE button |
| `speaker().Tone` | Played through WebAudio (square wave, like the real amp) |
| `storage()` | Persisted to `cheeko-storage.txt` next to the built app |
| `log()` | Terminal + the Device log panel |
| `cloud().GetJson/PostJson` | Fetched by the browser page; replies arrive in `OnCloudText` |
| `cloud().SendText` | Shown in the cloud panel; auto-reply or type your own reply |
| `mic()` / `OnMicAudio` | **Not implemented** — logs a warning, frames never arrive |
| `speaker().Play` / `display().Image` | Stubbed (logged); asset pipeline is future work |

The cloud panel is the "person behind the curtain": anything you type is
delivered to the app's `OnCloudText()`, which is exactly how you demo
`examples/claude_agent` without wiring a real model.

Note on `GetJson`/`PostJson`: the page performs the request with the browser's
`fetch()`, so APIs must allow CORS (open-meteo, coingecko, etc. do). APIs that
don't will deliver a `fetch_failed` error body to `OnCloudText` — handle errors
in your app like you would on-device.

## Headless screenshots

```bash
python tools/cheeko run examples/animated_face --screenshot face.bmp --ticks 80
```

Runs N ticks without a server (deterministic 16ms per tick) and writes a BMP of
the framebuffer. Used by CI and handy for docs.

## How it works

One C++ file, [`runtime/cheeko_sim.cc`](runtime/cheeko_sim.cc), plus an embedded
HTML page ([`runtime/sim_page.h`](runtime/sim_page.h)). Single-threaded: each
loop iteration handles pending HTTP requests, dispatches queued cloud text,
calls the app's `OnTick`, and sleeps ~12ms. The browser polls `/frame` (raw RGB
bytes) and `/events` (tones, logs, fetch requests) and POSTs input back.

There are no third-party dependencies on either side.
