# ClawBuddy ESP32 Firmware

ClawBuddy ESP32 firmware for the Waveshare ESP32-S3 Touch AMOLED 1.8 target.

This codebase is derived from the upstream Xiaozhi ESP32 firmware. Some internal Kconfig symbols, component names, wake-word model names, and protocol routes still contain `xiaozhi` for upstream/server compatibility and should not be renamed unless the matching upstream component or server route is changed at the same time.

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

Live legacy Xiaozhi/OpenClaw services are not used or mutated by firmware repo work.
