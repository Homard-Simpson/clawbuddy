# myAI Firmware

First-class myAI firmware for the Waveshare ESP32-S3 Touch AMOLED 1.8.

This codebase began as a fork of an ESP32 voice-assistant firmware stack, but the product target is myAI:

- English-first UI and docs.
- myAI branding, not upstream demo branding.
- Push-to-talk and normal listening modes on the BOOT button.
- Push-to-talk/manual replies are text-only on screen; normal chat replies still play voice audio.
- OpenClaw-oriented OTA, provisioning, and assistant bridge integration.
- No hard-coded personal hostnames, device IDs, tokens, or home-network assumptions.

## Target board

`waveshare/esp32-s3-touch-amoled-1.8`

## Build

```bash
cd firmware/myai
./build-myai.sh
```

If ESP-IDF is installed somewhere non-default:

```bash
IDF_EXPORT=/path/to/esp-idf/export.sh ./build-myai.sh
```

For a direct local build:

```bash
source "$HOME/.espressif/v6.0/esp-idf/export.sh"
idf.py build
```

## Product direction

The current firmware still contains some inherited internal identifiers and compatibility code. Those are being retired incrementally while keeping the device flashable after every step.

The goal is not to ship a rebranded upstream demo. The goal is a standalone myAI firmware that speaks to a standalone myAI server and OpenClaw.
