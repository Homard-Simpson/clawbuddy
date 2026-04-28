# Tailscale setup: make ClawBuddy work anywhere

This is the easiest public-prototype pattern if you want ClawBuddy to work away from home.

Plain English version:

- Your Mac/server runs OpenClaw + the ClawBuddy/Xiaozhi server.
- Tailscale Funnel gives that server a safe HTTPS address.
- ClawBuddy connects to that HTTPS address from any normal Wi-Fi network.
- The device does **not** need Tailscale installed.

![Tailscale anywhere flow](assets/clawbuddy-tailscale-anywhere.svg)

## What you need

- A Mac/Linux machine that can stay on.
- Tailscale installed and logged in on that machine.
- Funnel enabled for your tailnet/account.
- ClawBuddy server/OTA endpoints running locally.
- A ClawBuddy firmware build that knows your OTA URL.

Use placeholders below:

- `<your-tailnet-host>`: your Tailscale DNS name, for example `your-mac.your-tailnet.ts.net`
- `<https-port>`: the HTTPS port you expose, for example `10000`

## Recommended routing

For the prototype, expose only the ESP-required paths:

- `/xiaozhi/ota/` → local OTA server, usually `http://127.0.0.1:8003/xiaozhi/ota/`
- `/xiaozhi/v1/` → local websocket server, usually `http://127.0.0.1:8000/xiaozhi/v1/`
- optional `/mcp/vision/` → local vision helper, only if you use the camera tools

Do **not** expose dashboards/admin pages unless you know exactly why.

## Example Tailscale Funnel commands

Run these on the server/Mac that runs ClawBuddy/OpenClaw:

```bash
tailscale funnel --bg --yes --https=<https-port> --set-path / http://127.0.0.1:8000
tailscale funnel --bg --yes --https=<https-port> --set-path /xiaozhi/ota/ http://127.0.0.1:8003/xiaozhi/ota/
tailscale funnel --bg --yes --https=<https-port> --set-path /mcp/vision/ http://127.0.0.1:8003/mcp/vision/
```

Then verify:

```bash
tailscale serve status
curl https://<your-tailnet-host>:<https-port>/xiaozhi/ota/
```

The OTA check should answer. It may say POST requires an approved ESP device; that is okay.

## The OTA URL you put into firmware

Use this format:

```text
https://<your-tailnet-host>:<https-port>/xiaozhi/ota/
```

Example placeholder:

```text
https://your-mac.your-tailnet.ts.net:10000/xiaozhi/ota/
```

## Where the OTA URL comes from

Today, the firmware checks the OTA URL in this order:

1. A saved device setting named `ota_url` in the device's Wi-Fi/settings storage, if one exists.
2. The build-time default `CONFIG_OTA_URL`, if no saved setting exists.

For the public prototype, assume the reliable path is **build-time default**.

That default is set in:

```text
firmware/clawbuddy/main/boards/waveshare/esp32-s3-touch-amoled-1.8/config.json
```

Look for:

```text
CONFIG_OTA_URL="https://your-clawbuddy-host.example.com/clawbuddy/ota/"
```

Replace it with your Tailscale Funnel OTA URL before building firmware.

## Is the OTA URL part of the ESP setup web page?

Not currently in the normal user-friendly path.

The first-time setup/provisioning flow is mainly for Wi-Fi credentials. The device then uses its OTA URL to ask the server for:

- whether new firmware is available
- websocket server URL
- activation/server config
- optional asset updates

So the practical setup order is:

1. Decide your public/Tailscale OTA URL.
2. Put that URL into the firmware build config.
3. Build and flash firmware.
4. On first boot, use the device setup flow to connect to Wi-Fi.
5. The device calls the OTA URL and receives the websocket/server details.

A future improvement should add a simple setup page field for OTA URL so builders do not need to edit firmware config.

## Security checklist

Before you rely on this outside your house:

- Keep OpenClaw dashboards/admin tools private or tailnet-only.
- Expose only ESP-required routes through Funnel.
- Keep OTA POST allowlisted by device/client ID.
- Use signed/expiring firmware download URLs.
- Do not publish real device IDs, hostnames, tokens, or private URLs in docs/issues.
- If you change the public URL, rebuild or update the saved `ota_url` setting on the device.

## Quick troubleshooting

### The device works at home but not away from home

Check that your OTA URL uses `https://...`, not a LAN IP like `http://192.168.x.x`.

### OTA works but voice does not connect

The OTA server may be reachable, but the websocket route may be missing. Check `tailscale serve status` and confirm `/xiaozhi/v1/` reaches the local websocket server.

### Browser can open OTA, but the ESP cannot update

Check the OTA allowlist. The server may reject unknown `device-id` / `client-id` pairs.

### You changed the Tailscale URL

Rebuild firmware with the new `CONFIG_OTA_URL`, or update the device's saved `ota_url` setting if your build/setup path supports that.
