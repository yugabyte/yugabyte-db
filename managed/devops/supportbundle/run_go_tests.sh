#!/bin/bash
#
# Copyright 2026 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

# Runs the Go tests of every support bundle helper module and writes a JUnit report per module to
# <module>/target/test-reports/, where CI collects them.
#
#   bash managed/devops/supportbundle/run_go_tests.sh
#
# `sbt test` runs this after the Java tests and fails if it does. Every module runs even when an
# earlier one fails. Exits 1 if any test failed, and 2 if the tests could not run: go is missing,
# or go-junit-report, a module's dependencies or the Go toolchain its go.mod asks for cannot be
# downloaded (they need the Go module proxy or a module cache).

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULES=(cdcstate logparse serverconfredact)

# A run that stops early must not leave the previous run's reports for CI to collect.
for module in "${MODULES[@]}"; do
  rm -f "$SCRIPT_DIR/$module/target/test-reports/"*
done

if ! command -v go >/dev/null; then
  echo "go not found in PATH" >&2
  exit 2
fi

# Same reporter and version as node-agent/build.sh, installed under the ignored target/ directory.
REPORTER_BIN="$SCRIPT_DIR/target/bin"
if ! GOBIN="$REPORTER_BIN" go install github.com/jstemmer/go-junit-report/v2@v2.1.0; then
  echo "failed to install go-junit-report" >&2
  exit 2
fi

# Download up front, so that a download failure is reported as one rather than as failed tests.
for module in "${MODULES[@]}"; do
  if ! (cd "$SCRIPT_DIR/$module" && go mod download); then
    echo "failed to download the Go toolchain or dependencies of supportbundle/$module" >&2
    exit 2
  fi
done

failed=()
for module in "${MODULES[@]}"; do
  echo "Running Go tests in supportbundle/$module..."
  report_dir="$SCRIPT_DIR/$module/target/test-reports"
  mkdir -p "$report_dir"
  output_file="$report_dir/test_output.txt"
  # -count=1: always run, never report a cached pass.
  (cd "$SCRIPT_DIR/$module" && go test -count=1 -v ./...) > "$output_file" 2>&1
  test_rc=$?
  # -iocopy echoes the test output to the log as well as converting it.
  "$REPORTER_BIN/go-junit-report" -set-exit-code -iocopy \
    -out "$report_dir/${module}_test_results.xml" < "$output_file"
  report_rc=$?
  # A build failure can leave no test events, which the reporter alone would pass.
  if [ "$test_rc" -ne 0 ] || [ "$report_rc" -ne 0 ]; then
    failed+=("$module")
  fi
done

if [ "${#failed[@]}" -ne 0 ]; then
  echo "Support bundle Go tests FAILED in: ${failed[*]}" >&2
  exit 1
fi
echo "Support bundle Go tests passed in: ${MODULES[*]}"
