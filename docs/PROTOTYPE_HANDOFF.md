# ClawBuddy Prototype Handoff

## What is showable now

ClawBuddy now has a separate product folder with:

- Product brief and hardware target.
- Separation plan so the live prototype bridge stays untouched.
- Voice-input tuning profiles based on the live prototype settings that worked better.
- Runtime example config with non-conflicting product ports.
- Status contract for a future dashboard/CLI.
- LaunchAgent template using `com.openclaw.clawbuddy.*`, not prototype labels.

## Live prototype lessons captured

- ESP/server audio must agree on 16 kHz.
- Current comfortable VAD profile is:
  - `threshold: 0.40`
  - `threshold_low: 0.20`
  - `min_silence_duration_ms: 900`
- Public exposure should stay least-open:
  - no anonymous dashboards
  - OTA/config protected by known device/client identity
  - signed/expiring firmware URLs
  - public only for ESP-required routes

## Next implementation step

Build a separate local ClawBuddy runtime on ports `8100/8103/8104/8199`, initially LAN/local-only.
Do not point the ESP at it until `clawbuddy status` is clean and the live prototype bridge has a rollback path.
