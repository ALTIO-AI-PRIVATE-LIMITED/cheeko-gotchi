# App Lifecycle

Every developer app implements `CheekoApp` and is registered with `CHEEKO_APP()`.
For the full method-by-method contract, see
[`api-reference.md`](api-reference.md).

```cpp
class MyApp : public CheekoApp {
 public:
  void OnStart() override;
  void OnStop() override;
  void OnTick(uint32_t uptime_ms) override;
  void OnTouch(const TouchEvent& event) override;
  void OnButton(const ButtonEvent& event) override;
  void OnShake() override;                          // [Experimental]
  void OnMicAudio(const AudioFrame& frame) override;
  void OnCloudText(const std::string& text) override;
};

CHEEKO_APP(MyApp);
```

Override only what you need — every callback has an empty default.

## Events

| Event | Use |
| --- | --- |
| `OnStart` | Initialize UI, audio, cloud; load saved state from `storage()` |
| `OnStop` | Release app resources |
| `OnTick` | Animation, timers, periodic work — keep each call short |
| `OnTouch` | Tap, swipe, long press |
| `OnButton` | Volume, boot/power buttons |
| `OnShake` | Shake interaction (accelerometer) — **[Experimental]** |
| `OnMicAudio` | Advanced audio apps (after `mic().Start()`) |
| `OnCloudText` | Transcripts or model responses |

## Rules

1. Apps must be portable across board revisions. Board-specific details belong
   in `boards/<board-id>/board.json` and the board support package, never in
   app code.
2. Persist anything that must survive a power cycle through `storage()`, not
   global/static variables.
3. Never block in `OnTick` or an event callback — it freezes input and
   animation. Do periodic work in small slices.
