# Coinbase AMOLED 1.8 firmware

Dedicated read-only Coinbase display for both Waveshare ESP32-S3 Touch AMOLED 1.8 revisions:

- **V2** (default build): CO5300 panel, CST820 touch, MAC/device ID `1c:db:d4:7b:7f:38`.
- **V1**: SH8601 panel, FT5x06/FT3168 touch, TCA9554 + AXP2101 power sequencing, MAC/device ID `3c:dc:75:6e:b4:c8`.

Select the hardware variant at build time:

```sh
COINBASE_AMOLED_BOARD=v1 ./scripts/build.sh   # V1 board
./scripts/build.sh                             # V2 board (default)
```

The variant controls the panel driver, touch driver, power/reset sequencing, panel X gap, boot pacing, and the default embedded device ID (`COINBASE_EPAPER_DEVICE_ID` still overrides). V1-specific behavior, all required by the hardware: the AXP2101 PMU rail setup runs on V1 only (it blanks a V2 panel while drivers log success); the diagnostic full-bus i2c scan is V2-only because address-only probes can wedge the V1 FT-family touch controller; touch init happens within a few seconds of power-on because the FT chip stops accepting register writes after sitting idle; and touch reads are gated on the INT line (GPIO 21, active low) because the controller NACKs i2c reads while idle. Touch failure is non-fatal on both variants — the display keeps running without it.

Both variants keep the OTA project name `coinbase_amoled_1_8`, but release image versions carry a mandatory board suffix (`-v2` or `-v1`). The manual portal enforces that suffix, and the automatic release channel additionally requires the exact V2 board identity `v2-co5300-cst820`.

The board port follows Waveshare's official V2 sources: CO5300 over the unchanged QSPI pins, CST820 at I2C address `0x15` through Espressif's compatible CST816S driver, and the V2 `0x10` panel X gap. Before sending the first CO5300 command, firmware reproduces the official V2 Arduino reset sequence on TCA9554 outputs 0, 1, and 2 (20 ms low, then high). V2 never receives PMU rail writes: runtime AXP2101 writes are restricted to the proven PWRKEY short-press IRQ accesses, INTEN2 `0x41` bit 3 and INTSTS2 `0x49` bit 3 W1C. Replaying the old V1 PMU rail sequence on V2 can remove panel power. The application behavior and OTA partition layout remain compatible with the original firmware.

## Behavior

- Polls the authenticated HTTPS `/coinbase-device/` route on the feed-provided cadence (currently ~2 seconds for prices and 30 seconds for account/position data) with both bearer token and device ID authorization.
- Prices: BTC, SOL, XLM, HYPE, ETH.
- Loads recent closes for compact direction-colored sparklines on the prices screen.
- Tap any asset row for an expanded 24-36 **volume-candle** chart: green/red bodies and wicks, with each candle body's width proportional to that candle's volume within the visible window. Thin cyan/violet **BB20** upper/lower lines (20-close SMA ± two population standard deviations) and a subtle middle SMA begin at the twentieth valid candle. Finite bands may expand chart scaling only when their full envelope stays within half the raw candle range beyond either edge; rejected outliers are not drawn, so bad values cannot flatten or edge-clamp the chart. Muted light-blue price levels occupy a dedicated left gutter, outside candle, BB20, live-price, and entry-marker drawing. There is no separate volume histogram or volume text. The freed footer row shows the nearest daily-pivot support below (`S`, green) and resistance above (`R`, red), using adaptive price precision. Key levels never enter chart bounds. The page also shows high/low, a live-price marker, and an amber entry line when that asset has an open position. Tap the left/right half to move to the previous/next asset, then tap **PRICES** to return. If candle data is absent or invalid, the expanded page falls back to the prior close-only line chart (without bands) and still shows axis/key levels.
- Uses a 2x minimum font size, brighter secondary text, and reflowed position/chart metadata for readability on the 1.8-inch panel. The top row keeps battery, status/privacy, and SNTP-synchronized America/New_York local time aligned on one row; battery and clock use symmetric 24-pixel safe-area anchors so the round corners do not clip them, and time is right-aligned in 12-hour AM/PM format.
- Portfolio balance, unrealized P/L, and Coinbase daily realized P/L for the current America/New_York day.
- Touch **POSITIONS** for all open positions plus every reconstructed closing order for the current America/New_York day; rows use the live average entry with cents and show an overflow count when the screen cannot fit them all.
- Physical **Power/PWRKEY short press** is the exclusive screen-standby control. With VBUS absent (battery operation), it retains full standby: panel off, acknowledged feed/touch pause, Wi-Fi stopped (including setup/OTA portal traffic), and timer-sliced light sleep. A second Power short consumes the latched AXP event, resumes Wi-Fi/tasks, forces a fresh authenticated fetch, and redraws. With valid VBUS/USB, the same press instead uses display-only standby: the panel turns off and touch input is ignored while Wi-Fi, feed, portal, OTA, and background tasks keep running; the next Power short wakes and redraws the panel. VBUS selects display-only mode whether the battery is charging, full, or absent. USB insertion during full standby resumes networking/tasks but deliberately leaves the panel off; USB removal during display-only standby automatically moves to full battery standby. An active manual or automatic OTA temporarily defers full standby so Wi-Fi cannot disappear beneath an update. BOOT never enters or wakes standby.
- **BOOT short press** executes exactly the current page's blue bottom button action: chart -> PRICES, PRICES -> POSITIONS, and POSITIONS -> PRICES. **BOOT long release** from 0.8 seconds to under 10 seconds toggles Portfolio/account privacy obfuscation. An uninterrupted **BOOT 10-second hold** physically arms OTA, with no privacy toggle on release; a release first sampled after the 10-second threshold also arms instead of falling through to another action. All BOOT actions are ignored while the panel is off, preserving Power as the only wake control.
- Both standby modes preserve framebuffer/feed/UI state in RAM and never deep-sleep or reboot. In full battery standby, no cross-variant-safe AXP IRQ-to-ESP32 wake GPIO is established, so firmware uses 250 ms light-sleep timer slices and polls safe AXP INTSTS2 `0x49` bit 3 plus live VBUS while Wi-Fi/feed/touch remain stopped. USB display-only standby does not light-sleep or pause those workers. A single atomic operation gate serializes full standby, manual OTA, and automatic OTA; only one can own the network/OTA transition at a time. Runtime PMU writes are limited to preserving INTEN2 while setting `0x41` bit 3 and consuming exactly `0x49` bit 3 W1C; V2 receives no `0x80`-`0x99` rail writes. V1 retains its hardware-required boot-time rail setup.
- Shows OFFLINE, HTTP/JSON errors, STALE when the price feed stops, and ACCT STALE when fresh prices are masking an outdated position snapshot.
- Refuses feed data unless `read_only` is true. No order endpoints or write code exist.
- Reuses up to 10 Wi-Fi credentials stored by ClawBuddy in the `wifi` NVS namespace.
- If no credentials exist, starts the open `ClawBuddy-Coinbase-XXXX` setup AP immediately. If saved networks cannot connect, the same captive portal starts after 45 seconds while station retries continue.
- Captive DNS sends AP clients to `http://192.168.4.1`; only clients on the AP subnet may use the setup or OTA routes. Saving Wi-Fi moves that network to the front of the saved list and restarts the display.
- Supports dual-slot wireless OTA with bootloader rollback. Manual OTA is locked by default: physically hold BOOT continuously for 10 seconds to open a five-minute window and show a random six-digit code. Portal lifecycle and one-time-code state are serialized across Wi-Fi events, timers, standby, and HTTP handlers. The upload path accumulates fragmented headers, bounds and NUL-validates untrusted app-descriptor fields before checking the exact project and running-board suffix, validates the complete image before changing the boot slot, and leaves the running slot selected on any failure. A manual upload returns busy instead of overlapping automatic OTA or full standby, and keeps the exclusive gate until its scheduled reboot.
- V2 automatically checks an authenticated HTTPS manifest 30 seconds after startup and every six hours thereafter (15-minute retry after failure). Manifest and image requests carry the existing bearer credential and device allowlist ID. Firmware requires exact project/board, a newer dotted version ending in `-v2`, an image URL on the compiled HTTPS origin, exact size, SHA-256, matching bounded ESP app descriptor, and full ESP-IDF image validation before writing/selecting the inactive slot and rebooting. Automatic OTA takes the same exclusive gate as manual OTA and full standby. V1 compiles the code as a regression gate but its automatic channel is disabled. The previous slot remains available for bootloader rollback until the new build reaches display/NVS initialization and marks itself valid.

## Candle feed schema

The optional expanded-chart payload is bounded to the newest 36 valid candles per symbol in firmware. The preferred compact schema is:

```json
{
  "candle_interval_seconds": 60,
  "candles": {
    "BTC": [[1774814400, 68420.1, 68488.0, 68392.4, 68465.7, 12.35]],
    "SOL": [[1774814400, 188.2, 188.9, 187.8, 188.6, 9042.0]]
  }
}
```

Compact array order is `[timestamp, open, high, low, close, volume]`. Object entries may instead use numeric `timestamp`, `open`, `high`, `low`, `close`, and `volume` fields. Epoch seconds are preferred; epoch milliseconds are accepted and normalized. Series may arrive in either time order. Malformed candles are skipped, duplicate timestamps are replaced, storage is capped at 36, and `price_history` remains the legacy fallback and the source for list-page sparklines. The parser also accepts short object keys (`t/o/h/l/c/v`), `start` as the timestamp key, and the early alias `candles_interval_seconds`; producers should emit the preferred names above.

## Key-level feed schema

Key levels are an optional additive field, so feeds without them remain compatible:

```json
{
  "key_levels": {
    "BTC": [62000.0, 64500.0, 68100.0, 70200.0],
    "SOL": [176.25, 184.8, 193.4]
  },
  "key_levels_as_of": 1785585600
}
```

Each per-symbol array contains at most eight sorted, finite positive prices. Firmware defensively validates, de-duplicates, sorts, and bounds storage, then selects the closest value strictly below and above the live price. `key_levels_as_of` is producer metadata; firmware ignores it. The local feed derives these from clustered highs/lows over 180 daily candles, refreshes them every six hours with request concurrency capped at two, and persists the last valid per-symbol result across API failures and service restarts. One-minute visible-window extrema are not used.

## Build

```sh
./scripts/build.sh
```

Run the pure host-side tests for bounded/symmetric key-level storage, rolling BB20 math/scaling guards, chart-axis math/formatting, 12-hour time formatting, symmetric top-bar safe-area anchors, bounded automatic/manual OTA descriptor/version/origin validation, exclusive OTA/standby arbitration, exact blue-button transitions, BOOT hold/release timing, VBUS-versus-battery standby selection, the 250 ms Power/VBUS polling policy, and the narrow runtime AXP register-write allowlist with `./tests/run_host_tests.sh`.

Validated with ESP-IDF 5.5.2. `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192` is intentional: the former 3584-byte main stack overflowed on V1 during the first full feed/chart redraw. The managed display/touch dependencies are `espressif/esp_lcd_co5300` 2.1.x and `espressif/esp_lcd_touch_cst816s` 1.1.1~2, matching Waveshare's official ESP-IDF V2 implementation. Waveshare labels the fitted controller CST820 while retaining CST816-family API identifiers for the compatible register protocol.

ESP-IDF 5.5.2's timer wake source is used for cross-variant-safe light-sleep slices. The AXP2101 latches PWRKEY short press in INTSTS2, so each slice needs only one safe status read and, when set, one bit-3 W1C consume. No unverified AXP IRQ GPIO mapping is assumed. RAM and task state survive throughout.

Primary port references (inspected at official repository commit `8badafee5633d3786db00aa64dcf1977d5df6707`):

- [Waveshare board repository](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8)
- [`13_display_colorbar` CO5300 ESP-IDF example](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8/tree/main/examples/esp-idf/13_display_colorbar)
- [Waveshare `esp32_s3_touch_amoled_1_8` BSP](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_8/versions/2.0.3/readme)
- [Official V2 Arduino examples](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8/tree/main/examples/arduino-v2), used to confirm the shipping CST820-compatible touch register map

The build script reads the feed token from `~/.openclaw/state/coinbase-epaper-token`, generates a temporary ignored header with the protected HTTPS routes and allowlisted device ID, compiles, then deletes the header. Release `2.0.0` builds embed app descriptor versions `2.0.0-v2` or `2.0.0-v1`; overrides are limited to one-to-four uint32 numeric components and the complete suffixed version must fit ESP-IDF's 31-byte app-descriptor field. Do not publish firmware binaries: the device token is embedded in them.

For bench-only overrides, set `COINBASE_EPAPER_FEED_URL`, `COINBASE_EPAPER_DEVICE_ID`, `COINBASE_AMOLED_OTA_MANIFEST_URL`, and/or `COINBASE_AMOLED_VERSION` when building. The automatic image URL must use the same HTTPS origin as the compiled manifest endpoint. The default device ID is the dedicated Coinbase AMOLED board (`1c:db:d4:7b:7f:38`); production builds retain bearer-token and device-ID checks.

## Flash (do not erase flash)

```sh
cd /Users/miniclaw/.openclaw/workspace/products/clawbuddy/firmware/coinbase-amoled-1.8
./scripts/build.sh
./scripts/flash.sh /dev/cu.usbmodemXXXX
```

Use `scripts/flash.sh` for the initial USB installation. The build intentionally deletes its temporary token header, so a later bare `idf.py flash` may try to rebuild and fail; the helper flashes the already-verified binaries without rebuilding. The first OTA-layout flash writes bootloader at `0x0`, partition table at `0x8000`, initial OTA metadata at `0x10000`, and the app in `ota_0` at `0x20000`. NVS remains at its existing `0x9000` offset with the same `0x6000` size, so this migration preserves saved Wi-Fi credentials. Do not flash only the app at the legacy `0x10000` offset.

## First boot and recovery

1. With saved Wi-Fi, the display tries all saved networks and begins normal 30-second polling after DHCP succeeds.
2. Without saved Wi-Fi, connect a phone/computer to the SSID shown on the AMOLED, then use the captive prompt or browse to `http://192.168.4.1`.
3. If saved networks remain unavailable for 45 seconds, the setup AP appears without stopping background station retries.
4. A successful connection shuts down captive DNS, HTTP, and the AP. A submitted Wi-Fi form is committed to NVS and deliberately restarts after 1.5 seconds.

## Wireless OTA

1. Hold the physical BOOT button for 10 seconds. The display shows the setup SSID, OTA expiry, and a one-time six-digit code.
2. Join that AP, open `http://192.168.4.1`, choose the newly built `coinbase_amoled_1_8.bin`, and enter the code.
3. The code expires after five minutes and is never embedded in the web page. Requests from the station/LAN interface, unarmed requests, wrong codes, oversized files, foreign project images, truncated images, and failed ESP-IDF validation are rejected.
4. After a complete validated write, firmware selects the inactive OTA slot, responds success, and restarts. The bootloader rolls back if the new image cannot reach `app_main` and initialize NVS plus display hardware; a healthy boot then marks itself valid.

This manual OTA gate provides local physical-presence and image-integrity protection and remains the recovery path if the automatic service is unavailable. Secure Boot/signature enforcement is not enabled by this project, so only install binaries from the protected local build process.

### Automatic V2 manifest contract

The authenticated endpoint returns a small JSON object such as:

```json
{"project":"coinbase_amoled_1_8","board":"v2-co5300-cst820","version":"2.0.1-v2","url":"https://same-origin.example/coinbase-device/ota/v2/2.0.1.bin","sha256":"64-lower-or-uppercase-hex-digits","size":1234567}
```

The endpoint and binary are not served by this firmware repository. They must be staged behind the existing HTTPS bearer-token and device-ID checks before automatic delivery can occur; until then, manifest checks fail closed and normal display operation continues.
