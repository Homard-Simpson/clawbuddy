# Upstream / Fork Notes

ClawBuddy firmware started from a working ESP32 voice-assistant firmware stack so the hardware, audio, display, WebSocket, OTA, and MCP pieces could be validated quickly.

That upstream code is now treated as scaffolding. ClawBuddy owns this fork and will progressively replace inherited names, docs, protocol assumptions, and server dependencies.

Current fork policy:

- Keep the firmware buildable after every refactor.
- Keep the Waveshare ESP32-S3 Touch AMOLED 1.8 target first.
- Do not hard-code private infrastructure, home-network IPs, personal identifiers, device IDs, client IDs, tokens, or API keys.
- Preserve license notices for inherited code.
- Prefer ClawBuddy/OpenClaw naming in public docs and user-facing surfaces.
