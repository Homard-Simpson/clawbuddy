# myAI Firmware UI Direction

Target: Waveshare ESP32-S3 Touch AMOLED 1.8, 368x448.

## Product feel

- English-only, no visible Xiaozhi/Chinese branding.
- Dark by default for AMOLED: quiet black/blue surface, OpenClaw green accent.
- Tiny-screen UX: status first, minimal text, no dense chat UI.
- Friendly device identity: `myAI`, not a generic ESP assistant.

## Layout

1. **Top status strip**
   - Center title/status label.
   - Network, mute, and battery icons stay in the corners.
   - Low-opacity background + subtle divider so it does not fight the main view.

2. **Idle hero card**
   - Center rounded card.
   - OpenClaw green AI/microchip icon.
   - `MYAI` label.
   - `Tap or talk` hint.

3. **Conversation caption**
   - No WeChat-style message thread.
   - Bottom rounded subtitle pill appears only when there is speech/text.
   - Dark translucent card, inset from screen edge for a polished device feel.

4. **Alerts**
   - Low battery stays bottom-centered, rounded, high-contrast red.

## Palette

- Dark background: `#030712`
- Dark content surface: `#07111F`
- Card: `#101827`
- Accent: `#00F59B` / `#00D084`
- Text: `#EAFBF4`

## String tone

Short, device-native English:

- `Starting myAI...`
- `Connecting to OpenClaw...`
- `Finding OpenClaw...`
- `Ready when you are.`
- `OpenClaw is offline. Try again soon.`

## Current implementation notes

- Main changes are in `main/display/lcd_display.cc` non-WeChat branch.
- Idle status-bar clock is formatted as 12-hour `H:MM AM/PM` in `main/display/lvgl_display/lvgl_display.cc`.
- English strings are in `main/assets/locales/en-US/language.json`.
- Generated header is `main/assets/lang_config.h` via `scripts/gen_lang.py`.
- WeChat style remains disabled in `sdkconfig` and board `config.json`.
