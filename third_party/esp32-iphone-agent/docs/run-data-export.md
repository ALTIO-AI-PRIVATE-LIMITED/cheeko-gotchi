# Export runs for agent review

The current app already exports the screenshots, actions, model calls and saved annotations from a run. Give the raw ZIP to an agent for analysis; no dataset conversion is required.

Audited on 2026-10-17 against `main` at `9b1a667`. This guide uses the existing export paths.

## Export one run

1. Finish or stop the run, then open it in **Runs**.
2. Review the screenshots and add any **Correct / Wrong**, corrected tap points or notes you want the reviewing agent to see. These edits are saved in `review.json` beside the trace.
3. Open **⋯ → Export trace** in the review screen.
4. Use the share sheet to save the ZIP to Files or transfer it to your Mac.

This exports the run folder as it is, including the original screenshots and typed text. The **Keep run data for sharing** switch is not required for this path. Keep this raw archive private when it contains personal screen content.

For a batch, use **Settings → Diagnostics → Export traces**. This exports all trace folders, including older runs beyond the 30 shown in the list. **Settings → About → Logs → Export all logs** also includes the rest of Documents, such as debug logs and demonstration recordings; it is broader than needed for most run reviews.

## Copy over USB

The app enables Finder file sharing. On a paired iPhone, open **Finder → iPhone → Files → ESP32 Agent** and copy the `traces` folder to your Mac.

For a development installation whose app container is accessible, Xcode's command-line tool can copy the same folder. Replace both placeholders with your device and installed app bundle ID; the bundle ID comes from your build configuration.

```bash
xcrun devicectl device copy from \
  --device '<device identifier>' \
  --domain-type appDataContainer \
  --domain-identifier '<installed app bundle ID>' \
  --source Documents/traces \
  --destination ./run-export/traces
```

The command syntax was checked with the installed Xcode help. A physical-device transfer was not performed for this audit.

## What the agent receives

A run lives under `Documents/traces/<yyyyMMdd-HHmmss>/`. ZIP wrapper directories can vary by export path; locate the run by its `steps.jsonl`.

```text
<run>/
  steps.jsonl
  002.jpg, ...
  metrics.json       # when a metrics snapshot was written
  review.json        # when review edits were saved
  video/
    <epoch-ms>.jpg   # low-frame-rate screen recording, when available
```

| File | How to use it |
|---|---|
| `steps.jsonl` | One JSON object per event. `n` is the record sequence; `t` is the write time in epoch seconds; `kind` identifies task, model call (`llm`) or recorded action (`step`). |
| `kind=llm` | Saved user prompt, model reply, role, model ID, elapsed seconds, optional first-token timing and provider diagnostics/usage. `frame` names the actual screenshot supplied to that call. |
| `kind=step` | Parsed action and its parameters, with fields such as `think_s` and `act_s` where available. The current loader associates it with the latest `frame` at or before the step event; steps have no explicit model-call ID. |
| `review.json` | Edited steps with `verdict` (`true`, `false` or unjudged), corrected point `fix`, `note`, `inserted` and `uncertain`; also the edited task prompt and hints. Each step's `action` is a JSON string that needs a second JSON decode. |
| Root JPG files | Screen images. Tap/swipe marks and correction markers are drawn by the app from the action/review data, rather than burned into these images. Action and correction coordinates use 0–1000 screen units. |
| `video/*.jpg` | Approximate 2 fps screen history with epoch-millisecond filenames, useful for inspecting the visible effect of an action. |
| `metrics.json` | App/board diagnostic snapshot. Its recent-step buffer is shared across runs and capped at 40; do not treat it as a complete, isolated run summary. |

Suggested handoff to an analysis agent:

> Read the exported run folders. Use steps.jsonl and its referenced screenshots to inspect the decisions and actions. Read review.json for my corrections and notes, and video frames for visible effects. Report observations with the run ID, event n and image filename. Distinguish my annotations from model output and your own conclusions.

## Choose the export that preserves your inputs

**Settings → Privacy → enable Keep run data for sharing → select runs → Prepare Export** creates a separate sharing archive. It processes text to hide selected values, hides typed action text in supported formats, blacks out the top 7% of images and re-encodes the JPGs. It includes saved reviews, but this processing is not a guarantee that all personal content is removed. Use the raw **Export trace / Export traces** paths when an agent needs the original input images and text.

## Limits relevant to analysis

- A terminal `done` is the agent's claim, rather than an independent human success judgment. Step verdicts and notes are separate evidence.
- Review steps can be inserted or deleted. Match their saved action/frame/time; do not join them to original steps by array index. Stable source step IDs are not persisted.
- Post-action `changed` feedback is updated in memory after the step is written and is not persisted back to that event. Inspect the video for effects; there is no explicit action-to-after-image link.
- Model events omit the system prompt and full request configuration, and failed attempts are not fully recorded. Existing exports support inspection, but do not reproduce every original request exactly.
- A demonstration under `Documents/recordings` contains keyframes and inferred actions, rather than measured finger touches. The all-logs export includes it; the traces-only batch does not.

## Verification and source

The relevant implementation is [TraceStore](../ios/IPhoneAgent/Services/TraceStore.swift), [review export](../ios/IPhoneAgent/Review/ReviewView.swift), [annotations](../ios/IPhoneAgent/Review/ReviewModel.swift), [model events](../ios/IPhoneAgent/Agent/AgentLoop.swift) and [sharing export](../ios/IPhoneAgent/Services/RunShare.swift).

All 13 selected tests in `ShareTests`, `ReviewTests` and `StoreTests` passed on a dedicated iOS 26.5 simulator. They cover trace loading, correction input, redaction and a minimal sharing ZIP import/export round trip using synthetic data; they do not verify a physical iPhone transfer or preservation of every raw archive member.
