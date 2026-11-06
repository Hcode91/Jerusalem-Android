#!/usr/bin/env bash
# Runs the plain-JVM unit tests (no Android SDK needed).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

python3 "$ROOT/tests/make_test_iso.py" "$OUT/images"
javac -d "$OUT/classes" \
  "$ROOT/overlay/app/src/main/java/com/winlator/jerusalem/IsoImage.java" \
  "$ROOT/tests/IsoImageTest.java"
java -cp "$OUT/classes" IsoImageTest "$OUT/images"
