# Security policy

## Supported status

ClawBuddy is currently a public prototype. Do not deploy it as a production or unattended field device without a security review.

## Reporting vulnerabilities

If you find a vulnerability, please open a private security advisory on GitHub when available. If advisories are not available, open a minimal public issue that says you have a security concern without exploit details. Please avoid posting exploit details publicly until a fix is available.

## Secrets and credentials

- Never commit Wi-Fi passwords, API keys, tokens, private URLs, device IDs, or personal information.
- Keep local runtime values in ignored files such as `config/runtime.local.json`.
- Firmware logs must not print credential values. SSIDs may appear in logs for setup/debugging; passwords must be redacted.
- Generated firmware/release artifacts should be scanned before publishing.

## Prototype setup AP

The HT-HC33 camera setup AP uses per-device credentials by default:

- SSID format: `OpenClaw-Vision-XXXXXX`
- Password is generated/stored per device and printed on serial during setup.
- The legacy shared `openclaw-vision` credential is available only for explicit private bench builds with `CAMERA_DEV_SHARED_AP_PASSWORD`; do not ship or publish field builds with that flag.

## Public release checklist

Before making this repository public:

1. Run `make test`.
2. Run a secret/personal-info scan over tracked files.
3. Verify `git submodule status` is clean or intentionally absent.
4. Verify a fresh local clone has no missing submodules/gitlinks.
5. Confirm no live OpenClaw/Xiaozhi bridge or ESP service was mutated during validation.
