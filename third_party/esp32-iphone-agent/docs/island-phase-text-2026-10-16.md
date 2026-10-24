# Explicit Dynamic Island phase text

Source HEAD: 7c7b656e02382f0f42d5d2ab8e52a548b2f4b73a. Isolated branch: codex/island-phase-text. Existing untracked ios/fastlane/ was not changed. Separate build output; no live data copied.

- Model queries emit Waiting before the request and Planning (planner) or Thinking (actor) only after the first nonempty reasoning/content event. Headers, role-only chunks, and heartbeat comments do not count as content. Nonstreaming JSON can only report receiving when its body completes.
- Timeout retry stays Retrying until the new response starts. Empty-response retries return to Waiting. Progress callbacks are removed when the query ends.
- Full labels say Waiting for the model to respond, Thinking through my plan, and Thinking about my next move. Minimal presentation gets an explicit localized Wait / Plan / Think / Retry label, plus Look / Tap / Type / Swipe / Draw / Home / Back and terminal/help labels. It no longer infers every pending state from the face mood.
- Running expanded/lock-screen activities show the human-readable status rather than raw per-step thought/coordinate text. Replay data remains available inside the app.
- 80 simulator tests passed. Coverage includes SSE heartbeat/reasoning/content progress, planner versus actor phases, callback cleanup, and distinct minimal labels. Signed device build passed. Live Activity rendering with concurrent screen mirroring remains subject to iOS sizing/update scheduling; this change does not claim hardware visual verification.
