#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="$(mktemp -d "${TMPDIR:-/tmp}/coinbase-amoled-tests.XXXXXX")"
trap 'rm -rf "$OUT_DIR"' EXIT
for source in "$ROOT"/tests/test_*.cpp; do
  name="$(basename "${source%.cpp}")"
  "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror "$source" -o "$OUT_DIR/$name"
  "$OUT_DIR/$name"
  echo "PASS $name"
done
