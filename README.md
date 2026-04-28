# ClawBuddy

OpenClaw on the go — a portable ESP32-S3 Touch-AMOLED-1.8 device that connects to a real OpenClaw agent securely.

## Quick start for public prototypes

Fastest safe path (macOS/Linux, ESP-IDF already installed):

```bash
git clone <repo-url> clawbuddy && cd clawbuddy && make test && (cd firmware/clawbuddy && ./build-clawbuddy.sh)
```

If ESP-IDF is not installed yet, do this first:

```bash
git clone --recursive https://github.com/espressif/esp-idf.git ~/.espressif/v6.0/esp-idf
~/.espressif/v6.0/esp-idf/install.sh esp32s3
source ~/.espressif/v6.0/esp-idf/export.sh
```

Build, flash, provision:

```bash
cd clawbuddy
make test
cd firmware/clawbuddy
./build-clawbuddy.sh
source ~/.espressif/v6.0/esp-idf/export.sh
idf.py -p /dev/cu.usbmodemXXXX flash monitor   # replace with your ESP32-S3 serial port
```

First boot: use the device screen/serial logs for Wi-Fi/provisioning prompts. For the optional HT-HC33 vision camera, join its `OpenClaw-Vision-XXXXXX` setup AP and use the per-device password printed on serial.

## Hard requirements

- Device: **ESP32-S3 Touch-AMOLED-1.8**
- Keep firmware/server development isolated from any production voice bridge.
- Treat inherited upstream voice-assistant code as scaffolding, not the product identity.
- Product target is standalone ClawBuddy firmware + standalone ClawBuddy server + OpenClaw.
- Security-first: safest useful default, public only where required, allowlisted devices, signed/expiring URLs.

## Repo layout

- `bin/` — product CLI/status server prototypes.
- `config/` — runtime config and voice tuning profiles.
- `hardware/` — device notes, pinout, display/touch/audio assumptions.
- `firmware/clawbuddy/` — first-class ClawBuddy firmware fork for the target ESP32-S3 device.
- `bridge/` — future standalone ClawBuddy server/bridge code.
- `ops/` — deployment, watchdog, provisioning, OTA, security runbooks.
- `docs/` — product docs, setup guides, demos.

## Current showable commands

```bash
cd clawbuddy
make status          # product config, no live mutation
make live            # read-only comparison against a running prototype bridge
make profiles        # voice-input tuning profiles
bin/clawbuddy tune forgiving
make test
```

Optional local-only status API:

```bash
bin/clawbuddy-server
curl http://127.0.0.1:8199/status
curl http://127.0.0.1:8199/status/live
```

## Separation rule

Keep any existing production voice bridge operational. ClawBuddy work should stay isolated until it is stable enough to migrate or replace pieces deliberately.

## Public prototype security notes

- See `SECURITY.md` before publishing, flashing, or field-testing.
- Root license/notice files are `LICENSE` and `NOTICE.md`; inherited firmware attribution is in `firmware/clawbuddy/UPSTREAM.md`.
- The optional `camera/ESP_HaLow/` third-party checkout is not vendored; install it separately only if needed.
- HT-HC33 setup AP credentials are per-device by default. Only compile with `CAMERA_DEV_SHARED_AP_PASSWORD` on a private bench.
