# ClawBuddy

OpenClaw on the go — a portable ESP32-S3 Touch-AMOLED-1.8 device that connects to a real OpenClaw agent securely.

## Hard requirements

- Device: **ESP32-S3 Touch-AMOLED-1.8**
- Keep separate from the current working Xiaozhi bridge.
- Do not modify or depend on the live `xiaozhi-bridge/` implementation for active use.
- Treat the current Xiaozhi bridge as a reference/prototype only.
- Security-first: safest useful default, public only where required, allowlisted devices, signed/expiring URLs.

## Repo layout

- `bin/` — product CLI/status server prototypes.
- `config/` — runtime config and voice tuning profiles.
- `hardware/` — device notes, pinout, display/touch/audio assumptions.
- `firmware/` — future ClawBuddy firmware source/build notes.
- `bridge/` — future standalone ClawBuddy server/bridge code.
- `ops/` — deployment, watchdog, provisioning, OTA, security runbooks.
- `docs/` — product docs, setup guides, demos.

## Current showable commands

```bash
cd products/clawbuddy
make status          # product config, no live mutation
make live            # read-only comparison against working Xiaozhi prototype
make profiles        # voice-input tuning profiles
bin/clawbuddy tune forgiving
make test
```

Optional local-only status API:

```bash
bin/clawbuddy-server
curl http://127.0.0.1:8199/status
curl http://127.0.0.1:8199/status/live
```

## Separation rule

Keep any existing production voice bridge operational. ClawBuddy work should stay isolated until it is stable enough to migrate or replace pieces deliberately.
