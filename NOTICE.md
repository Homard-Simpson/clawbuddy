# Notices and upstream attribution

ClawBuddy is an OpenClaw public prototype. Portions of `firmware/clawbuddy/` were forked from the Xiaozhi/ESP32 voice-assistant firmware stack to validate hardware, audio, display, WebSocket, OTA, and MCP behavior quickly.

Inherited firmware files retain their original license where present. See:

- `firmware/clawbuddy/LICENSE`
- `firmware/clawbuddy/UPSTREAM.md`

The optional `camera/ESP_HaLow` Arduino core checkout is **not vendored** in this repository. It was previously present as a broken gitlink without `.gitmodules`, which made fresh clones fail. If HaLow experimentation is needed, install that third-party core separately according to its upstream license and setup instructions, outside normal ClawBuddy clone expectations.

Public prototype notes:

- Do not commit local Wi-Fi credentials, tokens, API keys, device identifiers, or private hostnames.
- Keep runtime overrides in ignored local files such as `config/runtime.local.json`.
- The HT-HC33 camera setup AP password `openclaw-vision` is a shared prototype/recovery credential only, not a production secret.
