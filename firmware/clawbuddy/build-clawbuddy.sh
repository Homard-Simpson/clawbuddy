#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
IDF_EXPORT="${IDF_EXPORT:-$HOME/.espressif/v5.5.2/esp-idf/export.sh}"
if [[ ! -f "$IDF_EXPORT" ]]; then
  echo "ESP-IDF export.sh not found. Set IDF_EXPORT=/path/to/esp-idf/export.sh" >&2
  exit 1
fi
source "$IDF_EXPORT" >/tmp/clawbuddy-idf-export.log 2>&1
exec python3 scripts/release.py waveshare/esp32-s3-touch-amoled-1.8
