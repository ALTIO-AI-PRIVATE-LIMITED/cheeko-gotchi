# iPhone Agent

Based on [ESP32 iPhone Agent](https://github.com/onehumanbeing/esp32-iphone-agent)
by Henry Yuan.

Plug the Cheeko into an iPhone and it becomes the eyes and hands of an AI agent
running in the iOS app on that phone: it receives the phone's screen over
AirPlay and taps and types through USB as a mouse and keyboard. This app is the
Cheeko side: a status screen, plus the middle button as a physical stop button.

It uses `Cheeko().phone()`, an **[Experimental]** SDK service that is not wired
up in the base firmware yet. To run this on a device today, flash
[`firmware/iphone_agent`](../../firmware/iphone_agent/), which does the same
thing without the SDK.

## Useful Extensions

- Push-to-talk: speak a task with `mic()` and the cloud, and pass it to the app.
- A face that follows the agent's pointer.
- A tone when a run finishes or needs help.
