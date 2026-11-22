#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
${CC:-clang} -O2 -Wall -Wextra -I"$ROOT/runtime/common" \
  "$ROOT/tests/x86_affinity_test/test_affinity.c" -o "$TMP/test_affinity"
"$TMP/test_affinity"
