# ClawBuddy Status Contract

`clawbuddy status` should be boring and trustworthy.

## Local product runtime

- Config exists and validates.
- Selected voice profile is valid.
- Planned ports are not occupied before install.
- LaunchAgent labels use `com.openclaw.clawbuddy.*`, never `com.openclaw.xiaozhi.*`.
- Public exposure is disabled unless provisioning/auth is ready.

## Optional live prototype comparison

`clawbuddy status --live` may inspect the working Xiaozhi bridge as a reference only.
It must not restart, reconfigure, or mutate the live bridge.

Live checks:

- `8000` Xiaozhi websocket/http listener
- `8003` OTA/config listener
- `8004` MCP endpoint listener
- `8899` OpenClaw bridge health
- Tailscale Funnel `:10000` routes for Xiaozhi
- Current Silero VAD and audio parameters

## Output rule

Default output is concise human text. `--json` emits machine-readable JSON for dashboard use.
