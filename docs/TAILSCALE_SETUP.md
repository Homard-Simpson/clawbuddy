# Tailscale setup: make myAI work anywhere

This is the easiest public-prototype pattern if you want myAI to work away from home.

Plain English version:

- Your Mac/server runs OpenClaw + the myAI server.
- Tailscale Funnel gives that server a safe HTTPS address.
- myAI connects to that HTTPS address from any normal Wi-Fi network.
- The device does **not** need Tailscale installed.
- Only paired/approved devices can actually use myAI. Random unpaired devices may see that an OTA endpoint exists, but they cannot receive firmware, websocket config, or reach the OpenClaw agent. That is the security win: public route, private access.

![Tailscale anywhere flow](assets/clawbuddy-tailscale-anywhere.svg)

## What you need

- A Mac/Linux machine that can stay on.
- Tailscale installed and logged in on that machine.
- Funnel enabled for your tailnet/account.
- myAI server/OTA endpoints running locally.
- A myAI firmware build that knows your OTA URL.

Use placeholders below:

- `<your-tailnet-host>`: your Tailscale DNS name, for example `your-mac.your-tailnet.ts.net`
- `<https-port>`: the HTTPS port you expose, for example `10000`

## Pairing and security: public route, private access

Tailscale Funnel makes a small HTTPS doorway reachable from the internet. That does **not** mean every device can talk to myAI or OpenClaw.

myAI uses a pairing/allowlist check at the OTA gateway:

1. The ESP sends its `device-id` and `client-id` when it calls the OTA endpoint.
2. The server checks those values against its approved device/client allowlist.
3. If the device is approved, the server can return firmware update info, websocket config, and signed/expiring firmware download URLs.
4. If the device is not approved, the server returns `403 forbidden` and does not give it the websocket URL/token or firmware download access.

Why this is good:

- You can use myAI anywhere without opening your whole network.
- Dashboards/admin pages stay private or tailnet-only.
- Losing the public URL is not enough to control your assistant.
- New devices must be deliberately paired/approved before they can reach Claw/OpenClaw.

Keep this rule: expose only the ESP-required paths, then pair/allowlist the actual devices you trust.

## How to pair a new device

Pairing is deliberately **not** exposed through Tailscale Funnel. Do it locally or over a private tailnet/admin session on the server machine.

1. Find the device identity:
   - Open the device setup portal (`http://192.168.4.1`) and check **Advanced → Device identity for pairing**.
   - Or watch USB serial / OTA `403 forbidden` logs for `Device-Id` and `Client-Id`.
2. On the server machine, start the local helper:

   ```bash
   bin/clawbuddy-server
   ```

3. Open the local page:

   ```text
   http://127.0.0.1:8199/pair
   ```

4. Paste `device-id` and `client-id`, then approve.
5. If your runtime does not automatically consume `config/device-allowlist.local.json`, copy the snippet from the page or run:

   ```bash
   bin/clawbuddy pair snippet
   ```

   Paste/merge it into the server config under `server.auth`.
6. Retry OTA from the device.

CLI-only version:

```bash
bin/clawbuddy pair add aa:bb:cc:dd:ee:ff 00000000-0000-4000-8000-000000000000 --label "my myAI"
bin/clawbuddy pair list
```

Paired means both the Wi-Fi MAC `device-id` and firmware UUID `client-id` are approved. Unpaired means OTA should return `403 forbidden` and withhold websocket config, tokens, and firmware download access.

## Recommended routing

For the prototype, expose only the ESP-required paths:

- `/xiaozhi/ota/` → local OTA server, usually `http://127.0.0.1:8003/xiaozhi/ota/`
- `/xiaozhi/v1/` → local websocket server, usually `http://127.0.0.1:8000/xiaozhi/v1/`
- optional `/mcp/vision/` → local vision helper, only if you use the camera tools

Do **not** expose dashboards/admin pages unless you know exactly why.

## Example Tailscale Funnel commands

Run these on the server/Mac that runs myAI/OpenClaw:

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

The firmware checks the OTA URL in this order:

1. A saved device setting named `ota_url` in the device's Wi-Fi/settings storage, if one exists.
2. The build-time default `CONFIG_OTA_URL`, if no saved setting exists.

Normal setup should use the setup page field, not firmware editing:

1. Join the temporary `myAI-XXXX` Wi-Fi setup network.
2. Open the captive portal or `http://192.168.4.1`.
3. On **Advanced**, set **Custom OTA URL** to your Tailscale Funnel HTTPS URL.
4. Leave it blank to fall back to the build-time default.

The setup page validates that the OTA URL starts with `http://` or `https://`. A Tailscale Funnel example is:

```text
https://your-clawbuddy-host.example.com/clawbuddy/ota/
```

There is also an optional **Voice server WebSocket URL** override. Leave it blank unless you deliberately need to bypass the websocket/server config returned by OTA.

The device uses its OTA URL to ask the server for:

- whether new firmware is available
- websocket server URL
- activation/server config
- optional asset updates

## Security checklist

Before you rely on this outside your house:

- Keep OpenClaw dashboards/admin tools private or tailnet-only.
- Expose only ESP-required routes through Funnel.
- Keep OTA POST allowlisted by device/client ID.
- Use signed/expiring firmware download URLs.
- Do not publish real device IDs, hostnames, tokens, or private URLs in docs/issues.
- If you change the public URL, update the setup page's saved `ota_url` setting or rebuild with a new `CONFIG_OTA_URL` default.

## Quick troubleshooting

### The device works at home but not away from home

Check that your OTA URL uses `https://...`, not a LAN IP like `http://192.168.x.x`.

### OTA works but voice does not connect

The OTA server may be reachable, but the websocket route may be missing. Check `tailscale serve status` and confirm `/xiaozhi/v1/` reaches the local websocket server. That route name is retained for firmware/server protocol compatibility for now.

### Browser can open OTA, but the ESP cannot update

Check the OTA allowlist. The server may reject unknown `device-id` / `client-id` pairs.

### You changed the Tailscale URL

Re-open the setup page and update **Custom OTA URL**, or rebuild firmware with the new `CONFIG_OTA_URL` if you want to change the fallback default.

### OTA says `403 forbidden`

That usually means the device is not paired/approved yet, or its `device-id` / `client-id` changed. This is expected security behavior. Add the device to the server allowlist, then retry.
