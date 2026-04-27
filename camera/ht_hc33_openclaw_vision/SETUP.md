# OpenClaw Vision HT-HC33 setup

Firmware behavior:

- Keeps fallback setup AP online: `OpenClaw-Vision` / `openclaw-vision`
- Opens a captive portal automatically when a phone joins the setup AP
- Scans nearby Wi-Fi networks and lets you tap one instead of typing SSID manually
- Saves up to 8 SSIDs/passwords
- Shows saved SSIDs with **Connect** / **Forget** buttons
- On boot, scans for saved SSIDs and connects to the strongest saved network it sees
- Advertises LAN name: `http://openclaw-vision.local/`

## First-time Wi-Fi setup

1. Power the HT-HC33 camera.
2. Join Wi-Fi network `OpenClaw-Vision`.
   - Password: `openclaw-vision`
3. Captive portal should open automatically.
   - If it does not, open `http://192.168.4.1/`.
4. Tap your home Wi-Fi in the scanned list.
5. Enter password and tap **Save and connect**.
6. Confirm `/status` shows `sta_connected: true` and a `sta_ip`.

## Important note

The setup AP intentionally stays available as a recovery path. That does **not** mean it failed to join home Wi-Fi. Check the status JSON:

- Connected: `sta_connected: true`
- LAN IP: `sta_ip`
- LAN URL: `http://openclaw-vision.local/`

## Endpoints

- Setup/status page: `/`
- Status JSON: `/status`
- Snapshot: `/capture`
- Stream: `:81/stream`

## OpenClaw/Xiaozhi integration

`mcp_self_tools_server.py` tries LAN/mDNS first:

1. `http://openclaw-vision.local`
2. fallback `http://192.168.4.1` after reconnecting Mac Wi-Fi to `OpenClaw-Vision`

Override with:

```bash
XIAOZHI_CAMERA_BASE_URLS=http://<camera-ip>,http://openclaw-vision.local
```
