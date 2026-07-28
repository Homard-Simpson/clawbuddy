#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-${ESPPORT:-}}"

if [[ -z "$PORT" ]]; then
  mapfile=()
  while IFS= read -r candidate; do mapfile+=("$candidate"); done < <(find /dev -maxdepth 1 -name 'cu.usbmodem*' -print 2>/dev/null | sort)
  if [[ ${#mapfile[@]} -ne 1 ]]; then
    echo "Usage: $0 /dev/cu.usbmodemXXXX" >&2
    exit 2
  fi
  PORT="${mapfile[0]}"
fi

for file in \
  "$ROOT/build/bootloader/bootloader.bin" \
  "$ROOT/build/partition_table/partition-table.bin" \
  "$ROOT/build/ota_data_initial.bin" \
  "$ROOT/build/coinbase_amoled_1_8.bin"; do
  [[ -s "$file" ]] || { echo "Missing build artifact: $file" >&2; exit 1; }
done

if [[ -z "${IDF_PYTHON_ENV_PATH:-}" && -x "$HOME/.espressif/python_env/idf5.5_py3.14_env/bin/python" ]]; then
  export IDF_PYTHON_ENV_PATH="$HOME/.espressif/python_env/idf5.5_py3.14_env"
fi
source "${IDF_EXPORT:-$HOME/.espressif/v5.5.2/esp-idf/export.sh}" >/dev/null || { echo "ESP-IDF export failed" >&2; exit 1; }
cd "$ROOT"
python -m esptool --chip esp32s3 -p "$PORT" -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/ota_data_initial.bin \
  0x20000 build/coinbase_amoled_1_8.bin
