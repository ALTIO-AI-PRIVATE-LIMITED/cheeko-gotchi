# Pomodoro Timer

A physical-feeling pomodoro cube for Cheeko Gotchi: pick a preset, **rotate the
device 180° to start**, and a 60-tick ring drains around big seven-segment
digits while you focus. An alarm rings at the end of every focus and break
period — tap the screen to silence it and roll into the next period.

## Screens & controls

| Screen | What you do |
| --- | --- |
| **Menu** | Tap a preset: Classic 25/5, Deep Work 50/10, Quick 15/3, Demo **15s/10s** (for demos), or Custom |
| **Custom** | `-` / `+` buttons set focus (5–90 min, steps of 5) and break (1–30 min); values persist. Rotate to start right from here |
| **Armed** | Shows the full ring + focus time. **Rotate the device (90° is enough) to start**, or tap for menu |
| **Running** | Ring ticks go dark clockwise as time drains. **Rotate to pause** |
| **Paused** | **Rotate to resume**, or tap RESET for the menu |
| **Alarm** | Flashing screen + repeating chime. **Tap the screen or knock the case** to silence (the touch panel only feels fingers on the glass; knocks are picked up by the accelerometer); the next period starts (a session counter ticks up per completed pomodoro) |

**Sound controls**: the speaker icon in the top-left corner of every screen
mutes/unmutes on tap (a red slash = muted; alarms then flash silently). The
physical **+ / − volume buttons** set the level, showing a brief volume bar —
and pressing either always unmutes. Volume and mute both persist.

## The flip gesture

Orientation comes from the accelerometer: whichever screen axis gravity
dominates decides the rotation — upright, upside-down, or either side. Every
screen auto-rotates through all four orientations (portrait layouts for 0°/180°,
landscape layouts for 90°/270°), rendering text through the app's own bundled
font ([src/pomo_font.h](src/pomo_font.h)) because the SDK's `Text()` cannot
rotate.

Rotation IS the timer control, like turning the physical cube: from the armed
or custom screen, *any* rotation — a 90° quarter-turn in either direction, or
a full 180° — starts the countdown, which **ticks like a fast clock** (two
clicks a second, quieter than the alarms) the whole time it runs. Rotating a
**running** timer pauses it — the ticking stops with it — and rotating again
resumes. Because any
movement means pause, the running screen keeps the orientation it started in
(menu and alarm screens still auto-rotate). Hold or prop the device
upright-ish: lying flat on a table there is no gravity change to detect. (An
accelerometer only sees the end state, so clockwise and counter-clockwise
turns both work.)

## Run it

```bash
python tools/cheeko run apps/pomodoro_timer
```

In the simulator the tilt sliders are gravity: Y **+1.00g** = upright,
Y **-1.00g** = upside-down, X **+1.00g** = rotated clockwise (landscape),
X **-1.00g** = rotated counter-clockwise. Jumping between opposite values is
the 180° flip gesture. Click = tap.

On hardware:

```bash
python tools/cheeko flash apps/pomodoro_timer
```

If auto-detection picks the wrong serial port (Bluetooth COM ports are a common
culprit on Windows), pass yours explicitly with `--device-port <port>`.
