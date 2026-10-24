# OpenRouter setup and model selection — 2026-10-16

Source: local checkout at `70857e503ca2ade9376ca8360335d663a11810a3`.
Existing uncommitted content: untracked `ios/fastlane/`, left untouched.
Implementation isolated in branch `codex/openrouter-model-selection`, with separate build output; no live databases or processes copied.

## Findings

The active phone app (`com.onehumanbeing.esp32agent`) logged HTTP 200 for the 10:02 image check, with 17 response characters and a normal stop. The response text was not logged, so its exact contents and reason for failing the color check are unknown. At 10:08 it returned `Red` successfully in 2.23 seconds using the same router. This does not establish an API-key or networking failure.

`openrouter/free` is a valid API model ID for automatic free-model routing, not an individual model. OpenRouter documents capability-aware routing, including images: https://github.com/OpenRouterTeam/docs/blob/main/guides/routing/routers/free-router.mdx

## Changes

- Consolidate the duplicate Free provider into OpenRouter, preserving the saved endpoint, key and `openrouter/free` selection.
- Replace long menus in setup and advanced overrides with a shared searchable list, checkmarks, refresh, and a custom-ID disclosure at the top.
- Separate `openrouter/free` and `openrouter/auto` into an automatic routing section with explicit labels and explanatory text.
- Report inconclusive image checks and empty responses accurately, rather than asserting that image input is unsupported. Include a bounded answer excerpt on mismatch. Do not accept substrings such as `infrared` or negated answers.
- Surface provider errors embedded in HTTP 200 JSON/SSE responses instead of treating them as empty completions.
- Add a DEBUG-only `-test-model` launch argument for testing the phone's existing configuration without extracting its key.
- Add Simplified Chinese translations for new copy.

## Validation

- 74 iOS unit tests passed, including legacy-provider migration, image-check validation, and streamed provider errors.
- Final signed device build succeeded.
- Simulator layout inspected with the live public OpenRouter model catalog. A temporary presentation override opened the sheet for inspection; it was removed before the final device build. Search typing and large Dynamic Type interactions were not manually exercised.
- Updated the active phone app. After Henry unlocked it, the final build passed live inference at 10:16:47 China time: `openrouter/free`, one sample image, HTTP 200, response `Red`, 1,956 ms total. The model catalog loaded 465 entries. The existing phone credential remained in place; no credential extraction was needed.
- No API key was read, exported, or included in source.

## Design convention

Installed `swiftui-pro` from https://github.com/twostraws/SwiftUI-Agent-Skill and recorded Henry's default SwiftUI convention in `~/.codex/design-principles.md`. Existing iOS deployment target retained.
