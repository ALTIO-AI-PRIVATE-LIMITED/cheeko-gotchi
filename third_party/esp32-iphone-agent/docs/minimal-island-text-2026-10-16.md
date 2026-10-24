# Text-only minimal Dynamic Island

Source HEAD: `5899204`. The source checkout contained only untracked `ios/fastlane/`; implementation used the isolated `codex/minimal-island-text` branch.

The minimal agent presentation now shows only the short status label. This is the primary presentation during Screen Mirroring, when another system activity shares the island. The former 16-point face above a caption was cramped and repeatedly appeared to slide or disappear during updates on the user's device.

The shared `Eyes`/`AgentFace` implementation remains in use in larger presentations. The idle gaze timer implementation is also preserved, but disabled: cosmetic updates are unnecessary for text-only status and may trigger system transitions. Action-driven gaze and mood updates remain available. Comments explain this decision next to both changes.

The previous minimal composition was a zero-spacing vertical stack of `Eyes(state: s).frame(width: 16, height: 16)` and the short status caption, constrained to 36 points wide. It can be revisited if a future system presentation supports it without distracting refresh behavior. Recording retains its existing recording indicator.

Validation: signed device build succeeded and whitespace checks passed. The phone was unavailable to CoreDevice, so this revision has not been installed or visually verified on the phone.
