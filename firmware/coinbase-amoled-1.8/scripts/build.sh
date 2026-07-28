#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOKEN_FILE="${TOKEN_FILE:-$HOME/.openclaw/state/coinbase-epaper-token}"
FEED_URL_VALUE="${COINBASE_EPAPER_FEED_URL:-https://mac-mini.tail1edc3a.ts.net:10000/coinbase-device/}"
# Board hardware variant. v2 (default) = CO5300 panel + CST820 touch, no PMU writes.
# v1 = SH8601 panel + FT5x06 touch + TCA9554/AXP2101 power sequencing.
BOARD_VALUE="$(printf '%s' "${COINBASE_AMOLED_BOARD:-v2}" | tr '[:upper:]' '[:lower:]')"
case "$BOARD_VALUE" in
  v1) BOARD_IS_V1=1; DEFAULT_DEVICE_ID="3c:dc:75:6e:b4:c8" ;;
  v2) BOARD_IS_V1=0; DEFAULT_DEVICE_ID="1c:db:d4:7b:7f:38" ;;
  *) echo "COINBASE_AMOLED_BOARD must be v1 or v2, got: $BOARD_VALUE" >&2; exit 1 ;;
esac
DEVICE_ID_VALUE="${COINBASE_EPAPER_DEVICE_ID:-$DEFAULT_DEVICE_ID}"
echo "Building board variant: $BOARD_VALUE (device id $DEVICE_ID_VALUE)"
[[ -s "$TOKEN_FILE" ]] || { echo "Missing feed token: $TOKEN_FILE" >&2; exit 1; }
python3 - "$TOKEN_FILE" "$ROOT/main/feed_config.h" "$FEED_URL_VALUE" "$DEVICE_ID_VALUE" "$BOARD_IS_V1" <<'PY'
import json, pathlib, sys
token=pathlib.Path(sys.argv[1]).read_text().strip()
pathlib.Path(sys.argv[2]).write_text(
    '#pragma once\n'
    '#define FEED_URL '+json.dumps(sys.argv[3])+'\n'
    '#define FEED_TOKEN '+json.dumps(token)+'\n'
    '#define DEVICE_ID '+json.dumps(sys.argv[4])+'\n'
    '#define BOARD_IS_V1 '+sys.argv[5]+'\n'
)
PY
trap 'rm -f "$ROOT/main/feed_config.h"' EXIT
if [[ -z "${IDF_PYTHON_ENV_PATH:-}" && -x "$HOME/.espressif/python_env/idf5.5_py3.14_env/bin/python" ]]; then
  export IDF_PYTHON_ENV_PATH="$HOME/.espressif/python_env/idf5.5_py3.14_env"
fi
source "${IDF_EXPORT:-$HOME/.espressif/v5.5.2/esp-idf/export.sh}" >/dev/null || { echo "ESP-IDF export failed" >&2; exit 1; }
cd "$ROOT"
idf.py set-target esp32s3 >/dev/null
idf.py build
