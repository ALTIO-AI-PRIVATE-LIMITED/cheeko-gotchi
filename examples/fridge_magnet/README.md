# Fridge Magnet

A grocery-and-notes board for the kitchen. Up to six note lines survive power
cycles through `storage()` (`note0`..`note5` plus a count), under a magnet-style
header with a small uptime clock.

It teaches the two skills every home widget needs:

- Persisting a small list in `storage()` and reloading it in `OnStart()`.
- Touch hit-testing: mapping a tap's y coordinate to the note row under it.

How to interact:

- Tap the bottom third: add the next canned note (milk, eggs, bread, ...).
- Tap a note line: remove that note.
- Shake: clear the whole board with a confirmation tone.

Runs in the simulator: `python tools/cheeko run examples/fridge_magnet`.

## Useful Extensions

- Voice-dictated notes via `cloud().StartVoiceSession()`.
- Sync the list with a family shopping app through `GetJson`/`PostJson`.
- Per-category magnet colors (dairy, produce, chores).
- A "done" checkmark state instead of instant removal.
