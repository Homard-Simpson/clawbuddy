# ClawBuddy Separation Plan

Goal: build ClawBuddy without destabilizing the working Xiaozhi bridge.

## Rules

1. No edits to `xiaozhi-bridge/` for ClawBuddy experiments.
2. No reuse of live LaunchAgent labels.
3. No reuse of live ports unless explicitly running a one-off local test.
4. No public Funnel changes for ClawBuddy until provisioning/auth is designed.
5. Any borrowed code must be copied into `products/clawbuddy/` and tracked as a fork/reference.
6. The working Xiaozhi bridge remains the daily-use system.

## Proposed separate runtime

- Product root: `products/clawbuddy/`
- Future server port: `8100` WebSocket
- Future OTA/config port: `8103`
- Future local bridge health port: `8199`
- Future MCP/device tools port: `8104`
- Future LaunchAgent prefix: `com.openclaw.clawbuddy.*`
- Future public path: separate from Xiaozhi, e.g. `/clawbuddy/` only after auth is ready

## Migration approach

Phase 1: docs/spec only.
Phase 2: copy/reference minimal Xiaozhi components into ClawBuddy tree.
Phase 3: build separate local server on non-conflicting ports.
Phase 4: test with ESP32-S3 Touch-AMOLED-1.8 on LAN only.
Phase 5: add secure provisioning and signed OTA.
Phase 6: optional public access via locked-down Funnel route.
Phase 7: decide whether to keep both systems or replace Xiaozhi.
