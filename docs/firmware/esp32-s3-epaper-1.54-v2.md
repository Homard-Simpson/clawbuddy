# Waveshare ESP32-S3 ePaper 1.54 v2 myAI firmware

Built and flashed for Mr. Evans's ESP32-S3 ePaper 1.54 v2 device on 2026-05-08.

## Device identity from flash/boot logs

- Board/SKU: `esp32-s3-epaper-1.54-v2`
- MAC / device-id: `10:51:db:39:8c:e4`
- UUID / client-id observed during boot: `0b54b9a7-9e26-4f93-8167-01e5cc1050d4`
- UUID / client-id observed during OTA setup: `0ce37b5f-25ca-472c-b864-398cf00786a8`
- Flash / PSRAM: 8MB / 8MB

## Release artifact

- `firmware/clawbuddy/releases/v0.1.17-myai_waveshare-esp32-s3-epaper-1.54-v2.zip`
- Contains `merged-binary.bin` for full USB flash at offset `0x0`.

## Build

```bash
cd firmware/clawbuddy
source "$HOME/.espressif/v5.5.2/esp-idf/export.sh"
python scripts/release.py --name esp32-s3-epaper-1.54-v2 waveshare/esp32-s3-epaper-1.54
```

## Flash

```bash
cd firmware/clawbuddy
unzip -p releases/v0.1.17-myai_waveshare-esp32-s3-epaper-1.54-v2.zip merged-binary.bin > /tmp/esp32-s3-epaper-1.54-v2-merged.bin
python -m esptool --chip esp32s3 \
  --port /dev/cu.usbmodem21101 \
  --baud 921600 write_flash \
  --flash_mode dio --flash_size 8MB --flash_freq 80m \
  0x0 /tmp/esp32-s3-epaper-1.54-v2-merged.bin
```

## OpenClaw / Xiaozhi setup URLs

Use this OTA URL in the setup portal:

```text
https://minis-mac-mini.tail1edc3a.ts.net:10000/xiaozhi/ota/
```

If asked for websocket URL:

```text
wss://minis-mac-mini.tail1edc3a.ts.net:10000/xiaozhi/v1/
```

## Caveat

Do not serve this ePaper binary to the AMOLED devices. It is board-specific even though the app version is also `0.1.17-myai`.
