#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOKEN_FILE="${TOKEN_FILE:-$HOME/.openclaw/state/coinbase-epaper-token}"
FEED_URL_VALUE="${COINBASE_EPAPER_FEED_URL:-https://mac-mini.tail1edc3a.ts.net:10000/coinbase-device/}"
OTA_MANIFEST_URL_VALUE="${COINBASE_AMOLED_OTA_MANIFEST_URL:-https://mac-mini.tail1edc3a.ts.net:10000/coinbase-device/ota/v2/manifest}"
VERSION_VALUE="${COINBASE_AMOLED_VERSION:-2.0.0}"
# Board hardware variant. v2 (default) = CO5300 panel + CST820 touch, no PMU rail writes.
# v1 = SH8601 panel + FT5x06 touch + TCA9554/AXP2101 power sequencing.
BOARD_VALUE="$(printf '%s' "${COINBASE_AMOLED_BOARD:-v2}" | tr '[:upper:]' '[:lower:]')"
case "$BOARD_VALUE" in
  v1) BOARD_IS_V1=1; DEFAULT_DEVICE_ID="3c:dc:75:6e:b4:c8" ;;
  v2) BOARD_IS_V1=0; DEFAULT_DEVICE_ID="1c:db:d4:7b:7f:38" ;;
  *) echo "COINBASE_AMOLED_BOARD must be v1 or v2, got: $BOARD_VALUE" >&2; exit 1 ;;
esac
DEVICE_ID_VALUE="${COINBASE_EPAPER_DEVICE_ID:-$DEFAULT_DEVICE_ID}"
IMAGE_VERSION="$(python3 - "$VERSION_VALUE" "$BOARD_VALUE" <<'PY'
import sys
version, board = sys.argv[1:]
parts = version.split('.')
if not 1 <= len(parts) <= 4 or any(not part.isascii() or not part.isdigit() for part in parts):
    raise SystemExit('COINBASE_AMOLED_VERSION must be 1-4 dotted numeric components')
if any(int(part) > 0xFFFFFFFF for part in parts):
    raise SystemExit('COINBASE_AMOLED_VERSION components must fit uint32')
image = f'{version}-{board}'
if len(image.encode('ascii')) >= 32:
    raise SystemExit('firmware image version must fit ESP-IDF app descriptor (31 bytes max)')
print(image)
PY
)"
export COINBASE_AMOLED_IMAGE_VERSION="$IMAGE_VERSION"
echo "Building board variant: $BOARD_VALUE version $IMAGE_VERSION (device id $DEVICE_ID_VALUE)"
[[ -s "$TOKEN_FILE" ]] || { echo "Missing feed token: $TOKEN_FILE" >&2; exit 1; }
python3 - "$TOKEN_FILE" "$ROOT/main/feed_config.h" "$FEED_URL_VALUE" "$DEVICE_ID_VALUE" "$BOARD_IS_V1" "$OTA_MANIFEST_URL_VALUE" <<'PY'
import json, pathlib, sys
from urllib.parse import urlsplit
token=pathlib.Path(sys.argv[1]).read_text().strip()
manifest=urlsplit(sys.argv[6])
if manifest.scheme != 'https' or not manifest.netloc or manifest.username or manifest.password or manifest.fragment:
    raise SystemExit('COINBASE_AMOLED_OTA_MANIFEST_URL must be an HTTPS URL without credentials or a fragment')
origin=f'{manifest.scheme}://{manifest.netloc}'
pathlib.Path(sys.argv[2]).write_text(
    '#pragma once\n'
    '#define FEED_URL '+json.dumps(sys.argv[3])+'\n'
    '#define FEED_TOKEN '+json.dumps(token)+'\n'
    '#define DEVICE_ID '+json.dumps(sys.argv[4])+'\n'
    '#define BOARD_IS_V1 '+sys.argv[5]+'\n'
    '#define OTA_MANIFEST_URL '+json.dumps(sys.argv[6])+'\n'
    '#define OTA_ALLOWED_ORIGIN '+json.dumps(origin)+'\n'
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
