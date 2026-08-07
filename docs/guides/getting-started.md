# Getting Started

You do **not** need a device, ESP-IDF, or any embedded experience to build
Cheeko apps. Apps run in a desktop simulator first; flashing real hardware is
an optional later step.

## What you need

| For | You need |
| --- | --- |
| Running apps in the simulator | Python 3 + a C++ compiler (`g++`) |
| The cloud demo loop | Node.js |
| Flashing a real device | `arduino-cli` + ESP32 core + USB-C data cable |

Windows: install MinGW-w64 for `g++`, and invoke the CLI as
`python tools\cheeko <command>` (or `tools\cheeko.cmd <command>`).
macOS/Linux: `./tools/cheeko <command>`.

## First commands

```bash
python tools/cheeko doctor    # what's installed, what's missing, device port
python tools/cheeko ports     # serial ports (COMx on Windows)
```

`doctor` separates what the **simulator** needs (g++, node) from what
**hardware** needs (arduino-cli) — missing hardware tools never block you.

## Run your first app (no hardware)

```bash
python tools/cheeko run examples/cheeko_pet
```

This compiles the app against the simulator runtime and opens
`http://localhost:8123/` — the device screen in your browser. Click to tap,
drag to swipe, use the buttons/tilt/shake controls, and watch logs in the
side panel. See [`sim/README.md`](../../sim/README.md) for everything it can do.

## Write your own

```bash
python tools/cheeko new my_app
python tools/cheeko run apps/my_app
```

Edit `apps/my_app/src/app.cc` — one class, a handful of callbacks
([`docs/sdk/api-reference.md`](../sdk/api-reference.md)) — and re-run. Verify
any time with:

```bash
python tools/cheeko check     # syntax-checks every example, app, and runtime
```

## Flash real hardware (optional)

1. Install [arduino-cli](https://arduino.github.io/arduino-cli), then:
   `arduino-cli core install esp32:esp32` (~1 GB, one time).
2. Plug the device in with a **data** USB-C cable; `python tools/cheeko ports`
   should show it.
3. Flash:

```bash
python tools/cheeko flash examples/cheeko_pet
```

Under the hood this assembles an Arduino sketch from
[`firmware/arduino_runtime/`](../../firmware/arduino_runtime/) (the SDK
implemented against the verified pin map and bring-up sequences in
[`SKILL.md`](../../SKILL.md)) and compiles with the board's required FQBN
(`PSRAM=opi` matters — see SKILL.md Gotcha 1). Hardware debugging tips live in
SKILL.md section 11.

## The cloud story

```bash
python tools/cheeko demo-loop
```

Runs the whole product loop locally with a simulated device: pairing →
app generation → signing → OTA command → install report. The cloud service it
uses is [`cloud/src/server.js`](../../cloud/README.md) — an in-memory
development simulator of the future Cheeko cloud.
