# ClawBuddy — OpenClaw on the go

## One-liner
ClawBuddy is a pocket/desk voice device that gives you secure, hands-free access to your personal OpenClaw assistant anywhere.

## Product thesis
OpenClaw is powerful but usually lives behind a phone, browser, laptop, or chat app. ClawBuddy makes it ambient: a small physical companion that can hear you, speak back, control your world, and relay into your full OpenClaw workflow without feeling like a toy mode.

## Who it is for
- OpenClaw power users who want voice-first access away from the keyboard.
- Busy operators who need quick commands: reminders, smart home, notes, messages, weather, calendar, job search/admin tasks.
- Home/workshop/garage users who want an always-available assistant without pulling out a phone.
- Tinkerers/devices people who want an open, hackable AI companion.

## Core promise
“Say it once. ClawBuddy handles it.”

## Hardware requirement

ClawBuddy must use the **ESP32-S3 Touch-AMOLED-1.8** as the target device. The existing Xiaozhi bridge remains a working prototype/reference only and must stay separate/usable while ClawBuddy is developed.

## MVP behavior
1. Wake/listen reliably.
2. Transcribe clean English speech.
3. Route requests into the user’s real OpenClaw agent/session.
4. Speak concise answers back.
5. Control its own device state: volume, brightness, theme, text size, network setup, device status.
6. Control user-approved integrations: Home Assistant, reminders, calendar, weather, notes, job-search workflows, etc.
7. Stay secure by default: least public exposure, allowlisted device identity, signed OTA/firmware URLs, no public dashboard.
8. Recover automatically from common failures: stuck listening, bad ASR loops, bridge restart, stale ports, Funnel route drift.

## Current prototype foundation
- ESP/Xiaozhi-compatible device connects to a self-hosted ClawBuddy/Xiaozhi server.
- Public access should expose only the required device bootstrap/WebSocket paths, preferably through a managed tunnel or reverse proxy.
- OpenClaw bridge exposes OpenAI-compatible chat endpoint locally on `:8899`.
- ESP voice turns relay into a dedicated OpenClaw voice session.
- Device-native self tools exist for status/volume/screen controls, but session-scoped connectivity still needs reliability work.
- Smart-home control can work through user-approved integrations.
- Voice reliability improved on the prototype after matching server audio to the ESP's 16 kHz stream and loosening Silero VAD: `threshold: 0.40`, `threshold_low: 0.20`, `min_silence_duration_ms: 900`. Product version should expose this as a tuning profile, not a buried YAML tweak.

## Current security posture
- OTA POST requires known ESP `device-id` + `client-id`.
- OTA GET no longer leaks the WebSocket URL.
- Firmware download URLs are signed and expiring when auth is enabled.
- WebSocket allowlist checks both device and client ID.
- Public vision test-client bypass is disabled by default.
- Dashboards/control surfaces remain local or private-network only.

## Product architecture

### Device
- Required hardware: **ESP32-S3 Touch-AMOLED-1.8**.
- Voice device with mic, speaker/audio output, AMOLED display, and touch input.
- Shows listening/thinking/speaking/error/network states.
- Local device controls are first-class tools.

### Edge server
- Xiaozhi server handles device WebSocket, OTA, VAD/ASR/TTS, and device MCP tools.
- Watchdog keeps ports, endpoints, and public route healthy.
- OTA/config endpoint is auth-gated.

### OpenClaw bridge
- Converts voice requests into OpenClaw agent turns.
- Uses short voice-safe timeout so the device never freezes on long jobs.
- Filters low-confidence ASR artifacts and self-generated retry loops.
- Delegates long jobs to sessions/subagents when needed.

### OpenClaw core
- The user’s real assistant, memory, tools, integrations, sessions, reminders, smart-home systems, calendar/notes apps, etc.
- ClawBuddy should never be sandboxed away from the main assistant unless explicitly configured that way.

## Differentiators
- Not just “AI voice chat” — it controls the real assistant with memory/tools.
- Device-native controls and status are part of the agent tool loop.
- Security-first: public only where needed, allowlists/tokens by default.
- Open/hackable: users can bring their own OpenClaw, tools, Home Assistant, local models, TTS/STT.
- Works as desk buddy, garage helper, bedside assistant, travel companion.

## MVP deliverables
1. Stable local prototype on a developer workstation or small server.
2. Repeatable install script for Xiaozhi bridge + LaunchAgents.
3. Device provisioning flow with QR/manual URL.
4. Auth-gated OTA/config endpoint.
5. Health dashboard/status command.
6. Recovery watchdog.
7. Basic device UI states.
8. Demo commands:
   - “Turn off the workshop lights.”
   - “What’s tomorrow’s weather?”
   - “Remind me in 20 minutes.”
   - “Device status.”
   - “Start a job-search follow-up.”
   - “Summarize my day.”

## Separation constraint

ClawBuddy development lives under `products/clawbuddy/` and must not destabilize the live `xiaozhi-bridge/`. Use separate ports, LaunchAgent labels, config, OTA/provisioning paths, and public exposure rules. Borrow from Xiaozhi only by copying/reference, not by hacking the working bridge in place.

## Near-term engineering backlog
- Make ESP self-tools reliably reachable from the live voice session, not just server-side.
- Add a ClawBuddy voice-input tuning profile with named modes like `strict`, `normal`, and `forgiving`; current live prototype is effectively `forgiving`.
- Add signed one-time provisioning token flow.
- Add per-device identity registry instead of hard-coded allowlist.
- Add OTA admin command: enable/disable public OTA route on demand.
- Replace raw Funnel route dependency with an OpenClaw-managed exposure profile.
- Add health endpoint that reports ASR/TTS/bridge/watchdog/device connection state.
- Add device-side reconnect and “backend restarting” UX.
- Add privacy modes: mute mic, local-only mode, no-memory mode, guest mode.
- Add logs scrubber to avoid storing sensitive spoken content unnecessarily.

## Packaging ideas
- DIY kit: docs + firmware + install script.
- Prebuilt device: ESP hardware flashed and paired.
- OpenClaw plugin: `openclaw clawbuddy setup` / `openclaw clawbuddy status`.
- Smart-home add-on later.

## Name options
- ClawBuddy — friendly, obvious, productable.
- PocketClaw — more portable emphasis.
- ClawGo — short but less warm.
- MiniClaw — short, but may conflict with existing assistant/device names.

Recommendation: keep **ClawBuddy**.

## Taglines
- “OpenClaw, off the leash.”
- “Your assistant, anywhere.”
- “A real OpenClaw agent in your pocket.”
- “Voice access to the assistant that already knows your world.”

## Product stance
Security and reliability are product features, not polish. Default to safest useful configuration: no anonymous public dashboards, no public config leaks, allowlisted devices, signed expiring URLs, and automatic recovery from common stuck states.
