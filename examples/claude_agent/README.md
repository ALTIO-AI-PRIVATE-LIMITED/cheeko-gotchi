# Claude Agent

A pocket AI companion powered by Claude. A small face waits with "TAP TO ASK";
one tap sends a rotating question through the cloud, and the reply renders as
word-wrapped text while the face talks it out.

It teaches the cloud text round-trip plus basic text layout:

- `cloud().Connect()` + `cloud().SendText()` to ask, `OnCloudText()` to receive.
- Simple word-wrap: about 19 characters per line at the 12px glyph advance.
- Scrolling a long reply one line at a time with the volume buttons.
- Mouth animation in `OnTick()` while the reply is fresh (under 3 seconds).

How to interact:

- Tap: ask Claude the next rotating question.
- Volume up / down: scroll the reply when it overflows the screen.

Runs in the simulator: `python tools/cheeko run examples/claude_agent`.

## Useful Extensions

- Push-to-talk questions via `cloud().StartVoiceSession()`.
- A custom question list, or questions built from other app state.
- Reply history persisted in `storage()`.
- Face emotions driven by the reply content.
