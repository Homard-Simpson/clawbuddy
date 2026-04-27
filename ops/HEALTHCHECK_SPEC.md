# ClawBuddy Healthcheck Spec

Health should answer one question: can the device complete a voice turn safely right now?

## `clawbuddy status`

Checks product config only:

- Runtime config exists.
- Selected voice profile exists.
- Product ports are free before install or owned by ClawBuddy after install.
- LaunchAgent prefix is `com.openclaw.clawbuddy`.
- Public exposure is off by default.
- No planned collision with live Xiaozhi resources.

## `clawbuddy status --live`

Read-only comparison against the working Xiaozhi prototype:

- Local Xiaozhi WebSocket/http on `8000`.
- OTA/config on `8003`.
- MCP endpoint on `8004`.
- OpenClaw voice bridge health on `8899`.
- Tailscale Funnel `:10000` is public for required Xiaozhi routes.
- Current live audio/VAD config is visible for comparison.

This command must never restart or mutate live services.

## Future installed runtime checks

When ClawBuddy has its own runtime:

- `8100` device WebSocket accepts authorized handshake.
- `8103` OTA/config responds correctly to authorized POST and safe generic GET.
- `8104` MCP endpoint connected and lists self tools.
- `8199` local status API healthy.
- Bridge can complete a short OpenClaw test turn.
- ASR receives audio frames and emits transcript.
- TTS emits audio with expected sample rate.
- Watchdog is active and not thrashing.
- Connected device count and last-seen timestamps are fresh.

## JSON shape

```json
{
  "ok": true,
  "profile": "forgiving",
  "ports": {},
  "security": {},
  "devices": [],
  "checks": []
}
```

Each check should include `name`, `ok`, `detail`, and optional `fix`.
