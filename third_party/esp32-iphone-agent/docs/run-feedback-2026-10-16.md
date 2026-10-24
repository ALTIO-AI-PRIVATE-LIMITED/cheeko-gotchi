# Model timing, timeout recovery, and Dynamic Island feedback

Source HEAD: e8e811049760d7d607105b8e8526b618ba8a9bf6. Source had only untracked ios/fastlane/, left untouched. Work isolated in codex/run-feedback with separate build output.

## Observed failure

Phone logs show a planner request at 10:42:32 China time, HTTP 200 SSE headers after 2.8 seconds, then a timeout at 90.1 seconds (the configured resource deadline). No completed reply was recorded. This establishes a request completion timeout, not an authentication failure; it does not establish whether the provider was still reasoning or stalled. The initial planner call previously bypassed the actor loop's error recovery and ended the run immediately. Separate BLE connection timeouts occurred after 10:46; this change does not fix the radio link.

## Changes

- Show time to response headers, first answer token and total request time in both model setup screens, plus elapsed time while testing. Clearly label these as including network and model processing. Reset timing when configuration changes; failed tests retain total elapsed time.
- Retry each timed-out model query at most once within that query, rechecking the foreground gate and current screen first. Existing actor-loop recovery remains bounded separately. No gestures are retried by this path. Cancellation is not retried, and the 90-second resource deadline remains in place.
- Replace raw NSError dumps at the end of a failed planner call with an actionable timeout message.
- Keep a Retrying status throughout the second request. Add waiting elapsed time in the expanded Island using a system-rendered timer.
- Minimal Island previously rendered only a face. It now includes a short status underneath; compact/expanded presentations retain detailed status text. Multiple active activities (including screen mirroring in the reported screenshot) can force minimal presentation; the camera region cannot be treated as app content.
- Swipe gaze now follows the displacement vector, not the endpoint's location relative to the screen center. Directional text accompanies the gaze in compact/expanded layouts. Eye animation respects Reduce Motion. The existing app-driven gaze changes are subject to iOS scheduling, not a guaranteed continuous widget animation.

Apple reference: https://developer.apple.com/documentation/activitykit/displaying-live-data-with-live-activities

## Validation

77 simulator tests passed, including planner timeout recovery, bounded retry, repeat gate checks, and swipe direction. Signed device build succeeded and was installed on com.onehumanbeing.esp32agent. On-device sample-image test at 10:53 passed with Red: HTTP headers 1.3 seconds, first answer token 6.8 seconds, total 6.822 seconds. Key remained on the phone.

The original long Notes task was not replayed; intermittent upstream timeouts remain possible. The updated two-activity Island layout and animation have not been visually verified on hardware. No claim of a full task completion or a radio-link fix is made.
