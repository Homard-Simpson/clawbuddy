# ESP32-S3 Touch-AMOLED-1.8

ClawBuddy target hardware.

## Must-haves

- ESP32-S3 Touch-AMOLED-1.8 board
- AMOLED display support
- Touch input support
- Microphone input path
- Speaker/audio output path
- Wi-Fi provisioning/reset flow
- OTA firmware update support
- On-device states: idle, listening, thinking, speaking, reconnecting, error, muted

## Product assumptions to verify

- Exact board vendor/model and schematic
- Display driver IC
- Touch controller IC
- Audio codec / I2S pins
- PSRAM/flash size
- Battery/power management, if any
- USB flashing mode
- Factory reset button/touch gesture

## UX direction

Small companion display, not a phone replacement:

- Big readable status
- Touch controls for mute, volume, reconnect, Wi-Fi setup
- Minimal text; voice-first
- Clear privacy indicator when mic is active
