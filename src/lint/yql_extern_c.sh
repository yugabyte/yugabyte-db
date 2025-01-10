#!/usr/bin/env bash
set -euo pipefail

if grep -q '^extern "C" {$' "$1"; then
  if [[ "$1" != */ybc_*.h ]]; then
    echo "error:missing_ybc_in_filename:1:$(head -1 "$1")"
  fi
else
  if [[ "$1" == */ybc_*.h ]]; then
    echo "error:bad_ybc_in_filename:1:$(head -1 "$1")"
  fi
fi
