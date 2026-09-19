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

```bash
./tools/cheeko doctor        # check your machine and any connected device
./tools/cheeko new my_app    # scaffold a new app
./tools/cheeko demo-loop     # run the full cloud->OTA loop, no hardware needed
```

`demo-loop` is the fastest way to understand the platform without a device: it
starts the local cloud service in-process, pairs a simulated device, generates a
signed app manifest, commands a Wi-Fi OTA install, and simulates device
verification.

## Check Your Device

Flash the [hardware test firmware](firmware/hardware_test/) to see every part of
the device working: display, touch, buttons, speaker, microphones, motion sensor
and charging. The prebuilt image flashes in one command:

```bash
esptool.py --chip esp32s3 -p PORT -b 460800 write_flash 0x0 cheeko-gotchi-hardware-test_merged.bin
```

Then unplug and replug the USB cable to start it. Its README also has the full
pin map and I²C device list for the board.

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

See [`docs/guides/example-apps.md`](docs/guides/example-apps.md) for which one to
start from.

## Layout

| Path | Purpose |
| --- | --- |
| `sdk/include/` | The public SDK header — the stable app contract |
| `examples/` | Ten example apps to fork |
| `docs/sdk/` | API reference, app lifecycle, SDK roadmap |
| `docs/guides/` | Getting started and cloud/audio workflows |
| `docs/product/` | Platform architecture, OTA flow, app package contract |
| `boards/cheeko-gotchi/` | Board capability metadata (what the device can do) |
| `schemas/` | Machine-readable app package contract |
| `cloud/` | Cloud builder API contract and a runnable local service |
| `demo/` | Local end-to-end loop: pairing, generation, OTA, install report |
| `firmware/hardware_test/` | Hardware test firmware: checks every part of the device, documents the pin map |
| `firmware/cheekoai_base/` | Base firmware/runtime scaffold (hardware adapters not connected yet) |
| `tools/cheeko` | Local CLI helper |

## Device Model

Cheeko Gotchi behaves like a small app platform:

- The board support package owns all hardware bring-up.
- The SDK exposes friendly APIs: `Cheeko().display()`, `Cheeko().mic()`,
  `Cheeko().speaker()`, `Cheeko().cloud()`.
- Apps are small, permissioned, signed packages installed over Wi-Fi OTA.
- Developers build apps without touching board bring-up code.

Board-specific hardware bring-up lives in the board support package. Apps never
depend on it. [`firmware/hardware_test/`](firmware/hardware_test/) shows how each
part of the board is driven, for developers who want to go below the SDK.

## Status

v0.1 is a developer foundation, not a finished runtime. The app lifecycle and
public SDK surface are locked; driver adapters behind the runtime are still
being wired up, so the base firmware does not drive the screen yet; flash
[`firmware/hardware_test`](firmware/hardware_test/) to exercise a device today.
See [`docs/sdk/roadmap.md`](docs/sdk/roadmap.md) for what lands
when, and [`docs/product/architecture.md`](docs/product/architecture.md) for the
platform blueprint.

## Contributing

Issues and pull requests are welcome. If you need something the API doesn't
expose, please open an issue — that's an SDK gap worth filing rather than a
reason to work around the runtime.

## License

MIT — see [LICENSE](LICENSE).
