# Cheeko Gotchi SDK

Build apps for **Cheeko Gotchi** — an ESP32-S3 touch-display AI companion with a
microphone, speaker, and cloud connection.

You write one C++ class. The base firmware owns the device; your app just
responds to lifecycle events and draws, listens, speaks, and calls the cloud.

```cpp
#include "cheeko.h"
using namespace cheeko;

class MyApp : public CheekoApp {
 public:
  void OnStart() override {
    Cheeko().display().Clear(Color::Black);
    Cheeko().display().CenterText(72, "Hello");
  }
};

CHEEKO_APP(MyApp);
```

The public API is a single header — [`sdk/include/cheeko.h`](sdk/include/cheeko.h),
locked at **v0.1.0**. The full contract is in
[`docs/sdk/api-reference.md`](docs/sdk/api-reference.md).

## Quick Start

No device needed — apps run in a **desktop simulator** in your browser
(you need `g++`; run `doctor` to check):

```bash
# Windows: python tools\cheeko <command>     macOS/Linux: ./tools/cheeko <command>
python tools/cheeko doctor                    # check your machine
python tools/cheeko run examples/cheeko_pet   # run an app in the simulator
python tools/cheeko new my_app                # scaffold a new app in apps/
python tools/cheeko run apps/my_app           # run yours
python tools/cheeko check                     # syntax-check every app + runtime
python tools/cheeko demo-loop                 # full cloud->OTA loop (needs Node)
```

The simulator ([`sim/`](sim/)) implements the entire SDK on your computer:
screen in the browser, click = touch, tilt sliders + shake button, WebAudio
tones, a cloud panel for testing `OnCloudText`. See [`sim/README.md`](sim/README.md).

With a real device, `python tools/cheeko flash examples/cheeko_pet` assembles an
Arduino sketch from [`firmware/arduino_runtime/`](firmware/arduino_runtime/)
(built on the verified bring-up in [`SKILL.md`](SKILL.md)) and flashes it over
USB via `arduino-cli`.

## Examples

| Example | What it teaches |
| --- | --- |
| [`hello_display`](examples/hello_display/) | The simplest app lifecycle |
| [`animated_face`](examples/animated_face/) | Blinking/talking face and character animation |
| [`weather_node`](examples/weather_node/) | Cloud fetch and tap-to-voice summary |
| [`market_pulse`](examples/market_pulse/) | Watchlist with a cloud-generated explanation |
| [`pill_guard`](examples/pill_guard/) | Reminder app with escalation hooks |
| [`focus_beacon`](examples/focus_beacon/) | Focus timer with an ambient status face |
| [`mic_level_meter`](examples/mic_level_meter/) | Microphone capture and PCM frames |
| [`audio_loopback`](examples/audio_loopback/) | Simultaneous capture and playback |
| [`cloud_voice_bot`](examples/cloud_voice_bot/) | Push-to-talk cloud voice sessions |
| [`self_test`](examples/self_test/) | Display, touch, speaker, mic, Wi-Fi checklist |
| [`fridge_magnet`](examples/fridge_magnet/) | Persistent notes board with touch hit-testing |
| [`claude_agent`](examples/claude_agent/) | Claude text round-trip with word-wrapped replies |
| [`tilt_maze`](examples/tilt_maze/) | Tilt physics and partial redraw |
| [`cheeko_pet`](examples/cheeko_pet/) | The full persistent virtual pet pattern |

See [`docs/guides/example-apps.md`](docs/guides/example-apps.md) for which one to
start from.

## Layout

| Path | Purpose |
| --- | --- |
| `sdk/include/` | The public SDK header — the stable app contract |
| `sdk/runtime/` | Shared runtime pieces (bitmap font) used by sim + device |
| `sim/` | Desktop simulator — run any app in your browser, no hardware |
| `examples/` | Fourteen example apps to fork |
| `apps/` | Your own apps (`cheeko new` scaffolds here) |
| `docs/sdk/` | API reference, app lifecycle, SDK roadmap |
| `docs/guides/` | Getting started and cloud/audio workflows |
| `docs/product/` | Platform architecture, OTA flow, app package contract |
| `boards/cheeko-gotchi/` | Board capability metadata (what the device can do) |
| `schemas/` | Machine-readable app package contract |
| `cloud/` | Cloud builder API contract and a runnable local service |
| `demo/` | Local end-to-end loop: pairing, generation, OTA, install report |
| `firmware/arduino_runtime/` | On-device SDK runtime (Arduino path, from SKILL.md) |
| `firmware/cheekoai_base/` | Product firmware scaffold (ESP-IDF path) |
| `SKILL.md` | Verified hardware bring-up guide for the real board |
| `tools/cheeko` | Cross-platform CLI: doctor, run, build, flash, check |

## Device Model

Cheeko Gotchi behaves like a small app platform:

- The board support package owns all hardware bring-up.
- The SDK exposes friendly APIs: `Cheeko().display()`, `Cheeko().mic()`,
  `Cheeko().speaker()`, `Cheeko().cloud()`.
- Apps are small, permissioned, signed packages installed over Wi-Fi OTA.
- Developers build apps without touching board bring-up code.

Board-specific hardware bring-up lives in the board support package and is not
part of this repository. Apps never depend on it.

## Status

The app lifecycle and public SDK surface are locked at v0.1.0, and the SDK now
has two runtimes:

- **Simulator** (`sim/`) — implements the full SDK on the desktop. Every
  example runs today with just `g++`.
- **Arduino device runtime** (`firmware/arduino_runtime/`) — implements the SDK
  on the real board using the verified bring-up in [`SKILL.md`](SKILL.md).
  Derived from hardware-tested code; re-verify on your unit when flashing.
  Microphone capture (`OnMicAudio`) is not wired up yet in either runtime.

The ESP-IDF scaffold (`firmware/cheekoai_base/`) remains the blueprint for the
signed-OTA product firmware. See [`docs/sdk/roadmap.md`](docs/sdk/roadmap.md)
and [`docs/product/architecture.md`](docs/product/architecture.md).

## Contributing

Issues and pull requests are welcome. If you need something the API doesn't
expose, please open an issue — that's an SDK gap worth filing rather than a
reason to work around the runtime.

## License

MIT — see [LICENSE](LICENSE).
