# Spidey Pet

A spider-hero themed desktop pet. Idle mask with the classic angular lens
eyes, spoken quotes, and two mini-games.

Run it (no hardware needed):

```bash
python tools/cheeko run apps/spidey_pet
```

## Modes (cycle with VOL+ / VOL-)

The UI is deliberately text-free; these are the controls:

| Mode | What happens |
| --- | --- |
| Idle | Full-bleed mask that blinks; shake for a spidey-sense flash |
| Quotes | Tap for the next quote in a comic speech panel; spoken aloud in the simulator |
| Thwip Shot | 30-second score attack: tap the cyan drones over the night skyline; best score persists |
| Wall Climb | Endless climb up a building: tilt (or hold a screen side) to dodge falling debris; best height persists |

## Voice status (honest)

`speaker().Play("say:<text>")` is a **simulator-only preview**: the browser
speaks the text. On the device it logs a warning until the PCM asset pipeline
lands in the runtime (see the roadmap) — the boot/quote sounds fall back to
chiptune tones there. Do not design apps that require voice yet.

## Shipping note

Character theming is generic on purpose (no names, logos, or trade dress from
any film). Keep it that way for anything that goes to a marketplace.
