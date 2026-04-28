# Contributing to ClawBuddy

Thanks for helping improve ClawBuddy. This repository is a public prototype, so contributions should keep the project safe, reproducible, and easy to understand.

## Before opening a PR

- Run `make test` from the repository root.
- Do not commit local runtime files, secrets, tokens, Wi-Fi credentials, real device IDs, private hostnames, private IP plans, logs, or build artifacts.
- Keep hardware-specific changes documented, especially if they affect setup, pairing, OTA, or camera behavior.
- Preserve compatibility routes such as `/xiaozhi/ota/` and `/xiaozhi/v1/` unless a migration plan is documented.

## Good PRs include

- A short description of what changed and why.
- Test or validation notes.
- Documentation updates for user-visible behavior.
- Screenshots or diagrams only when they are public-safe and do not show private infrastructure.

## Security-sensitive changes

For vulnerabilities or changes involving pairing, OTA, tokens, credentials, device identity, or network exposure, follow `SECURITY.md` and avoid publishing exploit details in issues or PR text.
