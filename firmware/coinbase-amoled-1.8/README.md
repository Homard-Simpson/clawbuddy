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

**Caution:** both variants share the OTA project name `coinbase_amoled_1_8`, so the OTA portal cannot reject a build made for the other board revision. Check the boot log line `board variant: ...` and keep V1/V2 binaries clearly separated.

The board port follows Waveshare's official V2 sources: CO5300 over the unchanged QSPI pins, CST820 at I2C address `0x15` through Espressif's compatible CST816S driver, and the V2 `0x10` panel X gap. Before sending the first CO5300 command, firmware reproduces the official V2 Arduino reset sequence on TCA9554 outputs 0, 1, and 2 (20 ms low, then high). It deliberately performs no raw AXP2101 register writes; replaying the old V1 PMU sequence can remove panel power. The application behavior and OTA partition layout remain compatible with the original firmware.

## Behavior

- Polls the HTTPS `/coinbase-device/` route every 30 seconds with both bearer token and device ID authorization.
- Prices: BTC, SOL, XLM, HYPE, ETH.
- Loads recent closes for compact direction-colored sparklines on the prices screen.
- Tap any asset row for an expanded 24-36 candle chart with green/red bodies and wicks, proportional volume bars, high/low and interval/window labels, a live-price marker, and an amber entry line when that asset has an open position. Tap the left/right half to move to the previous/next asset, then tap **PRICES** to return. If candle data is absent or invalid, the expanded page falls back to the prior close-only line chart.
- Uses a 2x minimum font size, brighter secondary text, and reflowed position/chart metadata for readability on the 1.8-inch panel.
- Portfolio balance, unrealized P/L, and Coinbase daily realized P/L for the current America/New_York day.
- Touch **POSITIONS** for all open positions plus positions closed today; touch **REFRESH** for an immediate poll.
- Shows LIVE, OFFLINE, HTTP/JSON errors, and STALE after 75 seconds without a successful update.
- Refuses feed data unless `read_only` is true. No order endpoints or write code exist.
- Reuses up to 10 Wi-Fi credentials stored by ClawBuddy in the `wifi` NVS namespace.
- If no credentials exist, starts the open `ClawBuddy-Coinbase-XXXX` setup AP immediately. If saved networks cannot connect, the same captive portal starts after 45 seconds while station retries continue.
- Captive DNS sends AP clients to `http://192.168.4.1`; only clients on the AP subnet may use the setup or OTA routes. Saving Wi-Fi moves that network to the front of the saved list and restarts the display.
- Supports dual-slot wireless OTA with bootloader rollback. OTA is locked by default: physically hold BOOT for 10 seconds to open a five-minute window and show a random six-digit code. The portal accepts only an ESP-IDF image whose project name is `coinbase_amoled_1_8`, validates the complete image before changing the boot slot, and leaves the running slot selected on any failure.

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

## Build

```sh
./scripts/build.sh
```

Validated with ESP-IDF 5.5.2. The managed display/touch dependencies are `espressif/esp_lcd_co5300` 2.1.x and `espressif/esp_lcd_touch_cst816s` 1.1.1~2, matching Waveshare's official ESP-IDF V2 implementation. Waveshare labels the fitted controller CST820 while retaining CST816-family API identifiers for the compatible register protocol.

Primary port references (inspected at official repository commit `8badafee5633d3786db00aa64dcf1977d5df6707`):

- [Waveshare board repository](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8)
- [`13_display_colorbar` CO5300 ESP-IDF example](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8/tree/main/examples/esp-idf/13_display_colorbar)
- [Waveshare `esp32_s3_touch_amoled_1_8` BSP](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_8/versions/2.0.3/readme)
- [Official V2 Arduino examples](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8/tree/main/examples/arduino-v2), used to confirm the shipping CST820-compatible touch register map

The build script reads the feed token from `~/.openclaw/state/coinbase-epaper-token`, generates a temporary ignored header with the protected HTTPS route and allowlisted device ID, compiles, then deletes the header. Do not publish firmware binaries: the device token is embedded in them.

For bench-only overrides, set `COINBASE_EPAPER_FEED_URL` and/or `COINBASE_EPAPER_DEVICE_ID` when building. The default device ID is the dedicated Coinbase AMOLED board (`1c:db:d4:7b:7f:38`); production builds retain bearer-token and device-ID checks.

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

This OTA gate provides local physical-presence and image-integrity protection. Secure Boot/signature enforcement is not enabled by this project, so only install binaries from the protected local build process.
