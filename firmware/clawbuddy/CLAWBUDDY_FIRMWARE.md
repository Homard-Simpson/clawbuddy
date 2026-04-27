# ClawBuddy ESP32 Firmware

Fork of the Xiaozhi ESP32 firmware for the Waveshare ESP32-S3 Touch AMOLED 1.8 target.

Product requirements:

- English-only visible UI and prompts.
- OpenClaw / ClawBuddy branding.
- No WeChat-style UI.
- 16 kHz / 60 ms voice path aligned with the working OpenClaw bridge.
- OpenClaw Vision camera display tools for the paired HT-HC33 camera.
- Build target: `waveshare/esp32-s3-touch-amoled-1.8`, variant `clawbuddy-esp32-s3-touch-amoled-1.8`.

## OpenClaw Vision display tools

The Waveshare ESP32-S3 Touch AMOLED 1.8 board registers these device-side MCP tools:

- `self.camera.show_snapshot` — fetches `http://openclaw-vision.local/capture` and displays the JPEG snapshot.
- `self.camera.show_live` — starts a low-FPS live view by refreshing `/capture`; default 30s, 2s refresh.
- `self.camera.stop_live` — stops live refresh and clears the preview.

This is intentionally direct ESP-to-ESP over Wi-Fi. The existing server-side describe/vision path remains separate.

Live Xiaozhi runtime is not used or mutated by this fork.
