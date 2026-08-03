# myAI Changelog

## 2026-08-03 — Coinbase AMOLED USB-aware standby
- Split Power/PWRKEY short-press policy by live VBUS state: battery operation retains full Wi-Fi/feed/touch-paused light-sleep standby, while USB power uses display-only standby with touch ignored and networking/background work left active. VBUS wins whether the battery is charging, full, or absent, and insertion/removal while the panel is off transitions modes automatically without lighting the screen.
- Added one exclusive runtime gate for manual OTA, automatic OTA, and full standby so OTA writers cannot overlap and Wi-Fi cannot be stopped under an active firmware update. Full standby is deferred until the update finishes.
- Serialized captive-portal lifecycle/auth state across Wi-Fi, timer, UI, and HTTP tasks; accumulated fragmented manual-OTA headers; bounded untrusted descriptor strings before suffix checks; constrained build versions to ESP-IDF's descriptor/uint32 limits; restored the Wi-Fi fallback timer after resume; narrowed runtime AXP writes; and covered the arbitration/edge policies with host tests.
- Kept Power as the only standby/wake control and preserved BOOT exactly while the panel is on: short executes the blue bottom action, release after 0.8 to under 10 seconds toggles privacy, and an uninterrupted 10-second hold arms OTA without a release action. BOOT is ignored while the panel is off.
- Moved the battery and clock eight pixels inward to symmetric 24-pixel top-bar anchors and added host coverage for source-policy selection and safe-area positioning.

## 2026-08-01 — Coinbase AMOLED screen-off standby
- Corrected controls to the authoritative mapping: Power/PWRKEY short exclusively enters/wakes standby; BOOT short calls the same blue-button action; BOOT 0.8-to-under-10-second release toggles privacy; continuous BOOT 10 seconds arms OTA without a release action.
- Preserved feed/touch pause acknowledgements, panel-off, Wi-Fi stop/resume, immediate refetch/redraw, and light-sleep power savings. Cross-variant wake now uses 250 ms timer slices to poll/consume only AXP INTSTS2 bit 3 because no safe AXP IRQ GPIO mapping is established.
- Restricted runtime PMU writes to proven IRQ registers `0x41`/`0x49`; V2 receives no rail-register writes. Added host coverage for controls, transitions, polling interval, and register allowlisting.
- Raised the ESP-IDF main-task stack from 3584 to 8192 bytes after serial validation exposed a V1 overflow on the first full chart/feed redraw; final V1/V2 serial runs remained stable through repeated HTTP 200 redraws.

## 2026-08-01 — Coinbase AMOLED daily-pivot key levels
- Added bounded, backward-compatible support/resistance arrays to the read-only device feed, derived from clustered 180-day daily pivot highs/lows and refreshed every six hours with stale-safe retention.
- Reused the expanded chart's freed footer row for nearest support and resistance, with adaptive precision and no effect on candle scaling.
- Added feed-model and firmware host tests; retained the no-orders/no-write safety boundary.

## 2026-07-22 — Coinbase AMOLED positions and daily realized P/L

- Fixed Coinbase CDE contract-root mapping so authenticated BTC/SOL/XLM/HYPE/ETH open positions reach the AMOLED rows.
- Added Coinbase daily realized P/L and same-day closed-position detail using the America/New_York trading date.
- Kept the feed read-only and protected by the existing bearer token plus allowlisted device ID.
- Updated the dedicated AMOLED layout to show all open positions and recent positions closed today without changing touch controls.

## 2026-05-20 — Waveshare 1.85 round LCD two-image rotation

- Updated the dedicated Waveshare ESP32-S3-LCD-1.85 image-viewer firmware to embed two supplied images.
- Baked each asset with horizontal mirroring, top-right anchored cover crop after mirroring, and 180 degree rotation.
- Changed the firmware to alternate the embedded images every 30 seconds indefinitely after every power-up.
- Disabled panel-level mirroring because the requested transforms are now baked into the assets.

## 2026-05-20 — Waveshare 1.85 round LCD mirrored image

- Enabled horizontal panel mirroring in the dedicated Waveshare ESP32-S3-LCD-1.85 image-viewer firmware so the embedded full-screen image appears mirrored on the display.

## 2026-05-20 — Waveshare 1.85 round LCD image viewer

- Added dedicated ESP-IDF firmware under `firmware/waveshare-esp32-s3-lcd-1.85-image-viewer/` for the non-touch Waveshare ESP32-S3-LCD-1.85 round display.
- Embedded the supplied image as a center-cropped 360x360 RGB565 asset and draw it full-screen on boot.
- Flashed the viewer to the connected ESP32-S3 on `/dev/cu.usbmodem21101`; boot logs reached `image_viewer: Image displayed`.

## 2026-05-02 — long assistant turn OTA 0.1.17

- Bumped firmware to `0.1.17-myai`.
- Extended the firmware protocol idle timeout from 120 seconds to 1800 seconds so long OpenClaw/tool-using assistant turns can finish and return audio instead of disconnecting early.
- Built/staged OTA app binary under `firmware/clawbuddy/ota/v0.1.17-myai/`.
- Built merged release zip `firmware/clawbuddy/releases/v0.1.17-myai_waveshare-esp32-s3-touch-amoled-1.8-myai.zip`.
- Local OTA endpoint `/myai/ota/` will serve `0.1.17-myai` when `bin/clawbuddy-server` is running.

## 2026-04-30 — PTT text-only OTA 0.1.16

- Bumped firmware to `0.1.16-myai`.
- Restored manual push-to-talk turns to request `response_mode: text` while wake/realtime voice still receives normal audio.
- Staged OTA app binary under `firmware/clawbuddy/ota/v0.1.16-myai/`.
- Added local OTA POST handling to `bin/clawbuddy-server` and verified it returns the staged `0.1.16-myai` payload.
- Updated the README top logo SVG so it visually says `myAI` instead of `ClawBuddy`.

## 2026-04-30 — myAI loading polish

- Bumped firmware to `0.1.14-myai`.
- Added centered myAI wordmark on the firmware splash/loading screen.
- Kept visible top/status text at normal readable size on the Waveshare AMOLED UI; long labels still ticker-scroll instead of wrapping.
- Added local-only myAI OTA metadata/download endpoints to `bin/clawbuddy-server`.

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

- Captured live upstream prototype voice-input improvement as a myAI product requirement.
- Product should support a named voice-input tuning profile instead of requiring manual YAML edits.
- Current live prototype tuning that feels better: Silero VAD `threshold: 0.40`, `threshold_low: 0.20`, `min_silence_duration_ms: 900`, with ESP/server audio aligned at 16 kHz.

## 2026-04-24

- Created myAI product folder and brief.
- Defined myAI as “OpenClaw on the go” using ESP32-S3 Touch-AMOLED-1.8.
- Set separation rule: live upstream prototype bridge remains operational; myAI work stays isolated under `products/clawbuddy/`.
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
