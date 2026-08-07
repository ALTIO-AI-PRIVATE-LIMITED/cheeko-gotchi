# Tilt Maze

A motion game: tilt the device to roll a ball through a walled maze to the mint
goal cell. Wins play a tone and persist as a counter in `storage()`.

It teaches motion input and hardware-friendly drawing:

- `motion().Read()` tilt physics: `velocity += tilt * 0.004` per tick, clamped
  to +-6, friction `* 0.985`, moving and colliding one axis at a time so the
  ball slides along walls instead of sticking.
- Partial redraw: each tick erases only the ball's previous bounding box and
  repaints just the wall/goal slivers that intersect it. Full-screen clears are
  visibly slow on real hardware (a full frame is ~142KB over SPI), so animated
  apps should never `Clear()` per frame.

How to interact:

- Tilt: roll the ball. Reach the mint cell in the bottom-right to win.
- Shake: reset the ball to the start.

Runs in the simulator: `python tools/cheeko run examples/tilt_maze`.

## Useful Extensions

- Multiple maze layouts that rotate after each win.
- A best-time clock using the `OnTick()` uptime.
- Holes that reset the ball, drawn as dark circles.
- A victory jingle instead of a single tone.
