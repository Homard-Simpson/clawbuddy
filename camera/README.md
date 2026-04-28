# Camera prototypes

This folder contains local camera firmware prototypes used by ClawBuddy.

## HT-HC33 OpenClaw Vision

`camera/ht_hc33_openclaw_vision/` is the maintained HT-HC33 setup with captive portal, saved Wi-Fi networks, snapshot, and stream endpoints. See `camera/ht_hc33_openclaw_vision/SETUP.md`.

Security note: default HT-HC33 builds use a per-device setup AP password printed on serial. The shared `openclaw-vision` password is only available when explicitly compiled with `CAMERA_DEV_SHARED_AP_PASSWORD` for private bench testing.

## Optional ESP_HaLow dependency

`camera/ESP_HaLow/` is intentionally not vendored or tracked. It was removed from the public prototype because it had been committed as a broken gitlink without `.gitmodules`, which breaks fresh clones.

If HaLow experimentation is needed, install the third-party Arduino core separately from its upstream source and keep that checkout local/ignored.
