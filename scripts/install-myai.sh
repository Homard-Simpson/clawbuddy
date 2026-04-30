#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FIRMWARE="$ROOT/firmware/clawbuddy"
IDF_EXPORT="${IDF_EXPORT:-$HOME/.espressif/v5.5.2/esp-idf/export.sh}"
PORT="${PORT:-}"
DO_BUILD=0
DO_FLASH=0
DO_MONITOR=0

usage() {
  cat <<'EOF'
myAI / ClawBuddy installer

Usage:
  scripts/install-myai.sh [--build] [--flash --port /dev/cu.usbmodemXXXX] [--monitor]

Fastest local setup:
  scripts/install-myai.sh

Build firmware:
  scripts/install-myai.sh --build

Build + flash device:
  scripts/install-myai.sh --build --flash --port /dev/cu.usbmodemXXXX

Env:
  IDF_EXPORT=/path/to/esp-idf/export.sh
  PORT=/dev/cu.usbmodemXXXX
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build) DO_BUILD=1 ;;
    --flash) DO_FLASH=1 ;;
    --monitor) DO_MONITOR=1 ;;
    --port) PORT="${2:-}"; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; usage; exit 2 ;;
  esac
  shift
done

need() {
  command -v "$1" >/dev/null 2>&1 || { echo "Missing required command: $1" >&2; exit 1; }
}

echo "== myAI / ClawBuddy setup =="
cd "$ROOT"
need python3
need make

if [[ ! -f config/runtime.local.json ]]; then
  echo "Creating local runtime config..."
  bin/clawbuddy init
else
  echo "Runtime config exists: config/runtime.local.json"
fi

echo "Running local checks..."
make test >/tmp/myai-install-test.log
bin/clawbuddy status

echo "Local prototype tools are OK."

if [[ "$DO_BUILD" -eq 1 || "$DO_FLASH" -eq 1 ]]; then
  [[ -f "$IDF_EXPORT" ]] || { echo "ESP-IDF export.sh not found: $IDF_EXPORT" >&2; echo "Set IDF_EXPORT=/path/to/esp-idf/export.sh" >&2; exit 1; }
  echo "Building firmware with: $IDF_EXPORT"
  (cd "$FIRMWARE" && IDF_EXPORT="$IDF_EXPORT" ./build-clawbuddy.sh)
fi

if [[ "$DO_FLASH" -eq 1 ]]; then
  [[ -n "$PORT" ]] || { echo "Missing --port /dev/tty... for flashing" >&2; exit 2; }
  echo "Flashing firmware to $PORT"
  # shellcheck disable=SC1090
  source "$IDF_EXPORT" >/tmp/myai-idf-export.log 2>&1
  cd "$FIRMWARE"
  idf.py -p "$PORT" flash
  if [[ "$DO_MONITOR" -eq 1 ]]; then
    idf.py -p "$PORT" monitor
  fi
fi

echo "Done."
