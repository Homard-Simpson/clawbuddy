# ClawBuddy Bridge

This folder will hold the standalone ClawBuddy bridge. It should borrow from the live prototype/OpenClaw bridge by copying known-good ideas, not by importing or mutating the live runtime.

## Reference pieces to fork carefully

- `prototype bridge OpenAI-compatible relay`
  - OpenAI-compatible local relay into OpenClaw.
  - Keep `/health`.
  - Keep short voice-safe timeout.
  - Replace live fixed session with ClawBuddy device/session identity.

- `prototype self-tools WebSocket bridge`
  - Stdio ⇄ WebSocket MCP bridge.
  - Preserve newline-delimited JSON-RPC framing.
  - Improve child lifecycle ownership so orphan self-tool servers do not accumulate.

- `prototype watchdog`
  - Port checks, HTTP checks, WebSocket handshake, and recovery pattern.
  - Rewrite for ClawBuddy ports/labels only.

## Product bridge requirements

- Local-only by default.
- Does not bind to prototype ports.
- Health endpoint on product status API.
- Device/session identity is explicit.
- Long jobs should return quickly and continue in OpenClaw sessions/subagents.
- Low-confidence ASR artifacts and self-generated retry loops should be filtered.
- Logs must not include secrets or raw spoken transcripts by default.

## Proposed environment

```bash
CLAWBUDDY_ROOT=products/clawbuddy
CLAWBUDDY_PROFILE=forgiving
CLAWBUDDY_AGENT_ID=esp
CLAWBUDDY_SESSION_PREFIX=clawbuddy
CLAWBUDDY_AGENT_TIMEOUT=60
CLAWBUDDY_BRIDGE_PORT=8199
```
