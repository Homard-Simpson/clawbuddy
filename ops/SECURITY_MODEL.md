# ClawBuddy Security Model

Security and reliability are product features. ClawBuddy should be useful without leaving random public surfaces open.

## Default posture

- Local/LAN only until provisioning and auth are complete.
- No public dashboards.
- Public routes only for ESP-required paths.
- Device identity required for OTA/config and WebSocket.
- Signed, expiring firmware download URLs.
- Logs should avoid storing raw spoken content unless debug mode is explicitly enabled.

## Device registry

Each device should have:

- `device_id` — hardware identity reported by ESP.
- `client_id` — app/firmware generated UUID.
- `display_name` — human label.
- `owner_agent` — OpenClaw agent/session target.
- `created_at`, `last_seen_at`.
- `enabled` boolean.
- Optional `capabilities`: display, touch, mic, speaker, battery.

## Provisioning flow

1. Generate one-time provisioning token.
2. Show QR/manual setup URL locally or on the device.
3. Device exchanges token for registered `client_id`.
4. Server stores device/client pair in registry.
5. Token expires immediately after use or after short TTL.

## OTA/config rules

- `GET /ota` may return a generic alive message only.
- `POST /ota` requires known `device-id` + `client-id`.
- Firmware URLs must be signed and short-lived.
- Config response should reveal only the endpoint required by the authorized device.

## WebSocket rules

- Require known `device-id` + `client-id` headers.
- Optionally require a signed session token.
- Reject unknown clients before any agent/session allocation.

## Public exposure rules

Allowed only after healthcheck passes:

- `/clawbuddy/v1/` → device WebSocket
- `/clawbuddy/ota/` → OTA/config POST

Not public:

- status API
- dashboard
- logs
- admin/provisioning endpoints
- raw OpenClaw bridge

## Log scrubbing

- Default logs: state transitions, errors, timings, device IDs partially masked.
- Debug logs: opt-in, local-only, time-boxed.
- Never log auth tokens, signed URLs, raw API keys, or full spoken transcripts by default.
