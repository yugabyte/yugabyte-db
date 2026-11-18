#!/bin/bash
#
# Copyright 2026 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

# Runs every support bundle shell test and returns one exit code.
#
#   bash managed/devops/supportbundle/run_tests.sh
#
# A skipped file is reported separately rather than counted as a pass: a file that asserts nothing
# and a file whose every check passed both exit 0 otherwise, and telling them apart is the point.
#
# serverconfredact/integration_test.sh is not run from here. It needs a Go helper binary built
# first and has its own target: make -C serverconfredact -f GNUmakefile integration-test

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

OUT_DIR="$(mktemp -d)"
trap 'rm -rf "$OUT_DIR"' EXIT

passed=0
skipped=0
failed=0
sections_skipped=0
failed_names=()

for test_file in "$SCRIPT_DIR"/*_test.sh; do
  name="$(basename "$test_file")"
  echo "=============================================================================="
  echo "$name"
  echo "=============================================================================="
  bash "$test_file" 2>&1 | tee "$OUT_DIR/$name.out"
  rc=${PIPESTATUS[0]}
  case "$rc" in
    0)  passed=$((passed + 1)) ;;
    77) skipped=$((skipped + 1)) ;;
    *)  failed=$((failed + 1)); failed_names+=("$name (exit $rc)") ;;
  esac
  # A file can pass overall while one of its subjects could not run; count those separately so a
  # green summary never hides a subject that asserted nothing.
  sections_skipped=$((sections_skipped + $(grep -c '^skip:' "$OUT_DIR/$name.out")))
  echo
done

echo "=============================================================================="
summary="$passed passed, $skipped skipped, $failed failed"
if [ "$sections_skipped" -ne 0 ]; then
  summary="$summary, $sections_skipped section(s) skipped"
fi
echo "$summary"
if [ "$failed" -ne 0 ]; then
  for name in "${failed_names[@]}"; do
    echo "  FAILED: $name"
  done
  exit 1
fi
