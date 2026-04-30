# myAI Prototype Handoff

## What is showable now

myAI now has a separate product folder with:

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

## Current hardware / vision status

- Firmware build validates with ESP-IDF: `idf.py build` passed on 2026-04-30 for `waveshare/esp32-s3-touch-amoled-1.8`; current `clawbuddy.bin` size is `0x258f30`, leaving 40% of the smallest app partition free.
- Release script currently skips because `releases/v0.1.6-clawbuddy_waveshare-esp32-s3-touch-amoled-1.8-clawbuddy.zip` already exists; rebuild/repackage before OTA if the current 12-hour clock change must be included in a zip.
- XIAO ESP32-S3 Sense camera sketch compiles with `arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 camera/xiao_esp32s3_sense_openclaw_vision`.
- Live camera registry sees two configured entries. `openclaw-vision.local` is currently unreachable from this host, but `192.168.50.62` is reachable and captures a valid JPEG scene image.
- `/vision/cameras` now exposes firmware-observed model/hostname/IP so bench label mismatches are visible. Current reachable device reports observed model `HT-HC33` at `192.168.50.62`; the local XIAO sketch source has been corrected to report `XIAO ESP32-S3 Sense` when flashed.
- Flash-readiness serial check is blocked by hardware state: `/dev/cu.usbmodem21101` disappeared before `esptool chip-id`; current visible serial ports are Bluetooth, `E35BT`, and `debug-console` only. Reconnect/boot ESP USB serial before flashing.

## Next implementation step

Build a separate local myAI runtime on ports `8100/8103/8104/8199`, initially LAN/local-only.
Do not point the ESP at it until `clawbuddy status` is clean and the live prototype bridge has a rollback path.
