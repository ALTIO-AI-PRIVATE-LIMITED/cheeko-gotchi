# Cheeko Pet

The namesake gotchi: a virtual pet whose mood, energy, and stars live in
`storage()` and survive power cycles. The face renders like `animated_face`,
with blinking driven by uptime and three visual states — happy, neutral, and
sleepy — that change the expression and colors.

It teaches the full pet-app pattern the SDK was designed around:

- Persistent stats loaded in `OnStart()` and saved on every change.
- Slow decay in `OnTick()`: mood and energy drop a point every 15 seconds.
- Interactions as short callbacks: feeding, playing, and star rewards.
- A second screen (stats bars) toggled by the physical buttons.

How to interact:

- Tap: feed (energy up, happy tone).
- Shake: play (mood up, energy down).
- Volume up / down: toggle the stats bar screen.
- Keep both stats high to earn a star; neglect makes Cheeko sleepy.

Runs in the simulator: `python tools/cheeko run examples/cheeko_pet`.

## Useful Extensions

- Night mode: dim colors and faster energy recovery after a "sleep" tap.
- Cloud check-ins that let a parent see the pet's stats remotely.
- More moods (hungry, excited, bored) with their own faces.
- Streak rewards for feeding on consecutive days.
