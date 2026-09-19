# SDK Roadmap

## v0.1: Developer Foundation

- Hardware knowledge base
- Board metadata
- CLI doctor/ports/board checks
- SDK app lifecycle header
- Example app contracts
- Hardware self-test contract

## v0.2: Runnable App Wrapper

- Build template for ESP-IDF apps
- Runtime adapter around the firmware display, touch, audio, Wi-Fi, and cloud
- `cheeko build`
- `cheeko flash`
- `cheeko run`

## v0.3: Audio And Cloud SDK

- Microphone capture API
- Speaker playback API
- PCM frame callbacks
- Cloud voice session abstraction
- WebSocket/MQTT transport adapters

## v0.4: Developer Distribution

- One-command install
- Versioned board packages
- App templates
- CI build for examples
- Published docs site

## Runtime Boundary

The SDK should expose:

```cpp
Cheeko.display()
Cheeko.touch()
Cheeko.mic()
Cheeko.speaker()
Cheeko.wifi()
Cheeko.cloud()
```

The firmware should own:

- display bring-up and drawing
- audio capture and playback
- motion and sensors
- power management
- OTA and provisioning internals

