# ClawBuddy Changelog

## 2026-04-30 — assistant turn listening lockout

- Firmware now keeps voice processing/listening disabled during assistant TTS, including realtime/AEC mode.
- Listening/UI resumes only after queued and currently-playing assistant audio has fully drained.

## 2026-04-28 — multi-camera OpenClaw Vision scene bundles

- Added `config/vision-cameras.example.json` registry with HT-HC33 OpenClaw Vision and XIAO ESP32-S3 Sense camera `90:70:69:12:ca:58` at `192.168.50.62` (`/capture`).
- Added `bin/clawbuddy vision list|capture|scene-prompt` for configured/LAN-discovered cameras and all-camera snapshot bundles.
- Added local server endpoints `/vision/cameras`, `/vision/capture`, and `/vision/policy`.
- Added firmware-observed camera metadata to `/vision/cameras` so bench label/model mismatches are visible during testing.
- Hardened snapshot capture to reject non-JPEG responses instead of storing them as scene images.
- Corrected the XIAO ESP32-S3 Sense camera firmware status model string.
- Documented scene behavior: describe each camera separately unless multiple views are confidently the same scene from different angles.

## 2026-04-28 — 12-hour clock

- Changed the idle firmware status-bar clock from 24-hour `HH:MM` to 12-hour `H:MM AM/PM`.

## 2026-04-28 — PTT text-only replies

- Bumped firmware to `0.1.6-clawbuddy` for OTA testing.
- Push-to-talk/manual listening still captures immediately and sends on release, but now requests a text response and locally drops TTS audio for that PTT turn.
- Normal non-PTT listening and wake-word reply audio remain unchanged.

## 2026-04-25

- Captured live upstream prototype voice-input improvement as a ClawBuddy product requirement.
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
## 2026-04-28 — public prototype hardening

- Removed the broken `camera/ESP_HaLow` gitlink from tracked files and documented it as an optional, local-only third-party dependency.
- Redacted firmware Wi-Fi password logging in acoustic provisioning and BLUFI setup paths.
- Added root `LICENSE`, `NOTICE.md`, and `SECURITY.md` public-readiness docs.
- Documented the HT-HC33 `openclaw-vision` setup AP password as an insecure prototype/recovery credential only.

## 2026-04-28 — prototype onboarding and PTT hardening

- Made HT-HC33 setup AP credentials per-device by default; shared AP password now requires explicit `CAMERA_DEV_SHARED_AP_PASSWORD` bench build.
- Improved push-to-talk startup: microphone capture starts immediately on button-down and buffers audio while websocket/session setup completes.
- Added concise public prototype quick-start commands to the repo README, including prerequisites, build/test, flash, and provisioning notes.
