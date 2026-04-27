# ClawBuddy Runtime Ports and Labels

ClawBuddy must never collide with the live Xiaozhi prototype.

## Reserved live Xiaozhi resources — do not use

- Ports: `8000`, `8003`, `8004`, `8899`, public Funnel `10000`
- LaunchAgents: `com.openclaw.xiaozhi-*`
- Live session: `bill-live-esp`
- Live config/token files under `xiaozhi-bridge/`
- Live Tailscale Funnel routes for `/`, `/xiaozhi/ota/`, `/mcp/vision/`

## ClawBuddy product resources

- WebSocket/device server: `8100`
- OTA/config HTTP: `8103`
- MCP/device tools endpoint: `8104`
- Local status/health API: `8199`
- LaunchAgent prefix: `com.openclaw.clawbuddy.*`
- Product root: `products/clawbuddy/`
- Local runtime config: `products/clawbuddy/config/runtime.local.json`

## Public exposure policy

Default: **off**.

Do not create public Funnel routes until all are true:

1. Device registry exists.
2. Device ID and client ID allowlists are configured.
3. OTA config does not leak WebSocket URLs to unauthenticated GET.
4. Firmware URLs are signed and expiring.
5. Healthcheck passes locally.
6. Rollback to live Xiaozhi is documented.

## Collision check

Run:

```bash
cd products/clawbuddy
bin/clawbuddy status
```

`separation: ok` means planned product ports/labels do not overlap with Xiaozhi.
