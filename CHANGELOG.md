# ClawBuddy Changelog

## 2026-04-25

- Captured live upstream prototype prototype voice-input improvement as a ClawBuddy product requirement.
- Product should support a named voice-input tuning profile instead of requiring manual YAML edits.
- Current live prototype tuning that feels better: Silero VAD `threshold: 0.40`, `threshold_low: 0.20`, `min_silence_duration_ms: 900`, with ESP/server audio aligned at 16 kHz.

## 2026-04-24

- Created ClawBuddy product folder and brief.
- Defined ClawBuddy as “OpenClaw on the go” using ESP32-S3 Touch-AMOLED-1.8.
- Set separation rule: live upstream prototype bridge remains operational; ClawBuddy work stays isolated under `products/clawbuddy/`.
- Captured security-first posture: public only where required, allowlisted device/client identity, signed/expiring OTA/firmware URLs, no public dashboards.

## 2026-04-25 — product scaffold

- Added `bin/clawbuddy` CLI with `status`, `status --live`, `profiles`, `tune`, and `init`.
- Added voice-input profiles: `strict`, `normal`, `forgiving`.
- Added `bin/clawbuddy-server`, a local-only JSON status API for future dashboard/plugin use.
- Added runtime separation config on non-upstream prototype ports `8100/8103/8104/8199`.
- Added docs: runtime ports/labels, security model, healthcheck spec, bridge README, demo script.
- Validated with `make test`; live upstream prototype comparison is read-only and does not mutate the working bridge.
