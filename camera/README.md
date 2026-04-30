# Camera prototypes

This folder contains local camera firmware prototypes used by ClawBuddy.

## HT-HC33 OpenClaw Vision

`camera/ht_hc33_openclaw_vision/` is the maintained HT-HC33 setup with captive portal, saved Wi-Fi networks, snapshot, and stream endpoints. See `camera/ht_hc33_openclaw_vision/SETUP.md`.

Security note: default HT-HC33 builds use a per-device setup AP password printed on serial. The shared `openclaw-vision` password is only available when explicitly compiled with `CAMERA_DEV_SHARED_AP_PASSWORD` for private bench testing.

## XIAO ESP32-S3 Sense OpenClaw Vision

`camera/xiao_esp32s3_sense_openclaw_vision/` is the XIAO ESP32-S3 Sense camera build. Current bench unit:

- MAC: `90:70:69:12:ca:58`
- IP: `192.168.50.62`
- Capture endpoint: `http://192.168.50.62/capture`
- Status endpoint: `http://192.168.50.62/status`

## Multi-camera scene behavior

The ClawBuddy CLI/server camera registry lives at `config/vision-cameras.local.json` if present, falling back to `config/vision-cameras.example.json`. It should include every OpenClaw Vision firmware camera ClawBuddy is allowed to use.

Run:

```bash
bin/clawbuddy vision list
bin/clawbuddy vision capture
```

When answering a scene question, callers should capture all reachable cameras and use the scene policy from `bin/clawbuddy vision scene-prompt`: describe each camera separately unless multiple views are strongly likely to be different angles of the same scene, in which case describe the scene as a whole and name the supporting cameras.

## Optional ESP_HaLow dependency

`camera/ESP_HaLow/` is intentionally not vendored or tracked. It was removed from the public prototype because it had been committed as a broken gitlink without `.gitmodules`, which breaks fresh clones.

If HaLow experimentation is needed, install the third-party Arduino core separately from its upstream source and keep that checkout local/ignored.
