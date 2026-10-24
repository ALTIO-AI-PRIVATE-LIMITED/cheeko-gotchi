# Consistent active-run status

Source: `851a8c6`; only untracked `ios/fastlane/` existed in the source checkout. Changes used the isolated `codex/run-state-consistency` branch.

## Evidence

The connected phone's latest trace was `20261016-113321`. Its Live Activity was successfully requested at 11:33:21 China time, and the agent started at 11:33:30. The user had returned to the app at 11:33:27, so the foreground safety gate held the agent before its first model request. The board disconnected at 11:34:00.

The list correctly used the active agent state. Review instead used only the stored trace: no terminal step meant Failed, even while the run was active. Retry was disabled because that same agent was still running. Foreground holding also prevented connection recovery from being checked.

## Fix

List and review now share the same active-run outcome resolver. Review observes agent and run-store changes, shows the foreground/recovery reason and Stop for an active run, and hides inactive-run editing controls while active. Failed and stopped runs offer Retry. Intent-associated runs also display their actual outcome.

The foreground gate now checks screen recovery when the feed is lost, allowing existing bounded recovery and failure handling to run. It publishes an explicit Paused Live Activity state while waiting for the user to leave the app.

## Validation

All 81 simulator tests passed, including a regression for an empty active trace versus an abandoned trace. The final device build succeeded. Activity creation logs do not establish on-screen visibility when another app has a competing Live Activity; physical display behavior remains unverified.
