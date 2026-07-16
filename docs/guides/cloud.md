# Cloud Programming

Cloud apps should not own Wi-Fi, mic capture, codec setup, or speaker playback.
They should request a voice session from the SDK.

## Proposed Flow

```cpp
void OnStart() override {
  Cheeko.display().Text("Connecting");
  Cheeko.wifi().Connect();
  Cheeko.cloud().Connect();
}

void OnTouch(const TouchEvent& touch) override {
  Cheeko.cloud().StartVoiceSession();
}

void OnCloudText(const std::string& text) override {
  Cheeko.display().Text(text);
}
```

## Runtime Responsibilities

The SDK runtime should handle:

- Wi-Fi setup and reconnect
- microphone capture
- audio encoding/packetization
- WebSocket or MQTT transport
- streamed TTS or PCM playback through the speaker
- UI status updates
- OTA and credentials storage

## Configuration Example

```json
{
  "cloud": {
    "provider": "openai",
    "api_key_env": "OPENAI_API_KEY",
    "voice": "alloy",
    "transport": "websocket"
  }
}
```

