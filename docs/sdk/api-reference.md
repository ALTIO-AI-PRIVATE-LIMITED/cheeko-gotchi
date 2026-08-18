# Cheeko Gotchi SDK — API Reference

This is the authoritative contract for app developers. You write one C++ class;
the base firmware owns all hardware. Low-level device details live in the board
support package, not in your app.

Header: [`sdk/include/cheeko.h`](../../sdk/include/cheeko.h)
SDK version: **0.2.0** (`CHEEKO_SDK_VERSION_STRING`)

---

## Versioning & stability

The SDK follows semantic versioning. Two stability tiers are marked on every
type in the header:

| Tier | Promise |
| --- | --- |
| **[Stable]** | Frozen for the entire `0.x` series. Build products on it. |
| **[Experimental]** | Shape may change before `1.0`. Usable, but pin the SDK version if you depend on it. |

Compile-time version macros: `CHEEKO_SDK_VERSION_MAJOR`, `_MINOR`, `_PATCH`,
and `CHEEKO_SDK_VERSION_STRING`. The runtime reports the same string, so a
device can refuse an app built against an incompatible SDK.

---

## The app model

Every app is one class deriving from `CheekoApp`, registered once with the
`CHEEKO_APP()` macro. Everything the app can do hangs off the global `Cheeko()`
runtime.

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

That is a complete, flashable app. No `main()`, no setup code, no driver init.

---

## Lifecycle callbacks

Override only the ones you need; all have empty defaults.

| Callback | Fires when | Notes |
| --- | --- | --- |
| `OnStart()` | App becomes active | Draw initial UI, start mic, connect cloud, load saved state |
| `OnStop()` | App is being torn down | Release resources |
| `OnTick(uptime_ms)` | Periodically | **Keep it short** — long work here blocks input and animation |
| `OnTouch(TouchEvent)` | Panel touched / released | `event.pressed` distinguishes down vs up |
| `OnButton(ButtonEvent)` | Boot / power / volume key | `event.button`, `event.pressed` |
| `OnShake()` | Device shaken | **[Experimental]** backed by the on-board accelerometer |
| `OnMicAudio(AudioFrame)` | PCM frames arrive | Only after `mic().Start()`. Advanced audio apps |
| `OnCloudText(string)` | Transcript / model reply | After a voice session or `SendText()` |

---

## Subsystems

All reached through `Cheeko()`. Never construct these yourself.

### `display()` — Display *[Stable]*

| Method | Purpose |
| --- | --- |
| `Clear(rgb)` | Fill the whole screen |
| `Text(x, y, text)` | Draw text at a point |
| `CenterText(y, text)` | Draw text horizontally centered |
| `Rect / FillRect(x, y, w, h, rgb)` | Outlined / filled rectangle |
| `Circle / FillCircle(x, y, r, rgb)` | Outlined / filled circle |
| `Line(x1, y1, x2, y2, rgb)` | Straight line |
| `Image(path)` | Draw an image asset by path |

Screen is **240 × 296**. Colors are `0xRRGGBB`; named constants live in `Color`
(`Color::Amber`, `Color::Mint`, …).

### `speaker()` — Speaker *[Stable]*

| Method | Purpose |
| --- | --- |
| `Play(path)` | Play an audio asset |
| `Tone(freq_hz, duration_ms)` | Beep / chirp |
| `SetVolume(0..100)` | Output volume |

### `mic()` — Microphone *[Stable]*

| Method | Purpose |
| --- | --- |
| `Start(sample_rate_hz = 24000)` | Begin capture; frames arrive via `OnMicAudio` |
| `Stop()` | End capture |

Capture runs through the runtime's microphone path. You get PCM frames; the
firmware handles audio routing and echo cancellation.

### `touch()` — Touch *[Stable]*

| Method | Purpose |
| --- | --- |
| `Get()` | Latest `TouchEvent` (poll outside `OnTouch`) |
| `IsPressed()` | Is the panel touched right now |

### `motion()` — Motion *[Experimental]*

| Method | Purpose |
| --- | --- |
| `Read()` | Current acceleration as a `MotionSample` (x/y/z in g) |
| `IsShaken()` | Edge-triggered shake flag (true once per shake) |

For the common case, just override `OnShake()`.

### `storage()` — Storage *[Stable]*

Key/value persistence that survives reboot **and** OTA (NVS-backed). This is how
a pet keeps its mood, energy, and stars between power cycles.

| Method | Purpose |
| --- | --- |
| `PutInt(key, value)` / `GetInt(key, fallback=0)` | Integer values |
| `PutString(key, value)` / `GetString(key, fallback="")` | String values |
| `Has(key)` | Does the key exist |
| `Remove(key)` | Delete a key |

### `log()` — Log *[Stable]*

Use this instead of `printf`. Lines surface on the USB serial console and in the
`tools/` log viewers.

| Method | Purpose |
| --- | --- |
| `Info(msg)` / `Warn(msg)` / `Error(msg)` | Leveled developer logging |

### `cloud()` — Cloud *[Stable]*

| Method | Purpose |
| --- | --- |
| `Connect()` | Open the cloud session |
| `StartVoiceSession()` | Begin a wake-word / streaming voice turn |
| `SendText(text)` | Send a text prompt to the model |
| `GetJson(url)` / `PostJson(url, json)` | HTTP(S) JSON calls; replies via `OnCloudText` |

### `wifi()` — Wifi *[Stable]*

| Method | Purpose |
| --- | --- |
| `Connect()` | Join the provisioned network |
| `IsConnected()` | Connection state |
| `Scan(out, max_count)` | **[Experimental]** Fill `out` with visible networks (`WifiNetwork`: ssid/rssi/secured); blocks a few seconds and drops any current connection. The radio is 2.4 GHz-only, so everything listed is joinable |
| `SetCredentials(ssid, password)` | **[Experimental]** Persist credentials (survives reboot and OTA) and join immediately — enables on-device provisioning UIs |

---

## Rules for app code

1. **Don't block.** `OnTick` and the event callbacks run on the app loop. Long
   or blocking work freezes the UI. Do periodic work in small slices.
2. **Don't touch hardware.** Go through the API — if you need something it
   doesn't expose, that's an SDK gap to file, not a reason to drop down to
   drivers.
3. **Persist through `storage()`**, not global state — apps can be stopped and
   restarted, and the device power-cycles.
4. **Stay portable.** Board-specific details belong in
   `boards/<board-id>/board.json`, never in app code.

---

## What the firmware owns (the boundary)

So you don't have to: display bring-up, audio capture and playback, motion,
power management, OTA, and Wi-Fi provisioning. The app sees only the API above.

---

## A complete example

A minimal pet that remembers how many times it has been shaken:

```cpp
#include "cheeko.h"
using namespace cheeko;

class ShakePet : public CheekoApp {
 public:
  void OnStart() override {
    shakes_ = Cheeko().storage().GetInt("shakes", 0);
    Draw();
  }

  void OnShake() override {
    shakes_++;
    Cheeko().storage().PutInt("shakes", shakes_);
    Cheeko().speaker().Tone(880, 60);
    Cheeko().log().Info("shaken!");
    Draw();
  }

 private:
  void Draw() {
    auto& d = Cheeko().display();
    d.Clear(Color::Black);
    d.FillCircle(120, 120, 70, Color::Amber);
    d.CenterText(220, "shakes: " + std::to_string(shakes_));
  }

  int shakes_ = 0;
};

CHEEKO_APP(ShakePet);
```
