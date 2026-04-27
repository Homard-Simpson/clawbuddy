# ClawBuddy Demo Script

Goal: show that ClawBuddy is becoming a real product without risking the live Xiaozhi bridge.

## 1. Show separation

```bash
cd products/clawbuddy
bin/clawbuddy status
```

Expected:

- `separation: ok`
- public exposure off
- product ports `8100/8103/8104/8199` free

## 2. Show live prototype comparison

```bash
bin/clawbuddy status --live
```

Expected:

- live Xiaozhi ports healthy
- Funnel `:10000` healthy
- live audio is `16000` / `60ms`
- live Silero profile is `0.40 / 0.20 / 900ms`

## 3. Show voice tuning profiles

```bash
bin/clawbuddy profiles
bin/clawbuddy tune strict
bin/clawbuddy tune normal
bin/clawbuddy tune forgiving
```

Explain:

- These change ClawBuddy product config only.
- They do not touch the live bridge.
- `forgiving` matches the better-feeling prototype setting.

## 4. Show local status API

```bash
bin/clawbuddy-server
curl http://127.0.0.1:8199/health
curl http://127.0.0.1:8199/status
curl http://127.0.0.1:8199/profiles
```

Expected:

- local-only JSON API
- no public exposure
- usable later by dashboard/OpenClaw plugin

## 5. Voice-path demo on live prototype

Use the actual ESP/Xiaozhi device, not ClawBuddy runtime yet:

- “Device status.”
- “Set your text size bigger.”
- “What’s the weather today?”
- “Turn off the workshop lights.”
- “Remind me in 20 minutes to check the garage.”

## 6. Product story

ClawBuddy is no longer just an idea. It has:

- a target device
- a security model
- a separated runtime plan
- status/tuning CLI
- local status API
- a clear path to fork the working bridge safely
