# Animated Face

The first app every Cheeko developer should understand: draw a face, blink the
eyes, and make the mouth animate while the device is "talking".

This example is intentionally simple. It teaches the mental model:

- `OnStart()` paints the first frame.
- `OnTick()` updates animation state.
- Touch toggles talking mode.
- The face is drawn with display primitives, so developers can replace it with
  their own character later.

## Try Next

- Change the colors to match your character.
- Swap touch toggling for `OnCloudText()` so the face talks during cloud replies.
- Add emotions such as sleepy, happy, confused, and thinking.
