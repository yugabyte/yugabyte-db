#!/bin/bash
#
# Copyright 2026 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

# Harness shared by the support bundle shell tests. Source it first; it sets up the temp root and
# the failure counter, so a test file holds only the cases it is about.
#
# Reporting contract, which run_tests.sh depends on:
#   ok   <name>   a passing check          exit 0   every check that ran passed
#   FAIL <name>   a failing check          exit 1   at least one check failed
#   skip: <why>   a section could not run  exit 77  nothing was asserted at all
#
# Exit 77 is the GNU Automake skip convention. It exists because a test that silently passes without
# asserting anything is indistinguishable from a clean run, and this suite has been bitten by that.

set -uo pipefail

SUPPORTBUNDLE_TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEVOPS_BIN="$(cd "$SUPPORTBUNDLE_TEST_DIR/../bin" && pwd)"

TMP_ROOT="$(mktemp -d)"
trap 'rm -rf "$TMP_ROOT"' EXIT

failures=0
checks_run=0
skips=0

pass() {
  checks_run=$((checks_run + 1))
  echo "ok   $1"
}

# Prints the check name, then any further arguments indented beneath it as evidence.
fail() {
  local name=$1
  shift
  echo "FAIL $name"
  local line
  for line in "$@"; do
    sed 's/^/      /' <<< "$line"
  done
  checks_run=$((checks_run + 1))
  failures=$((failures + 1))
}

check() {
  local name=$1 got=$2 want=$3
  if [ "$got" = "$want" ]; then
    pass "$name"
  else
    fail "$name" "got : $got" "want: $want"
  fi
}

# Fixed-string containment. Deliberately not a regex: several call sites match on argv fragments
# that contain characters a regex would read as metacharacters.
check_contains() {
  local name=$1 haystack=$2 needle=$3
  if grep -qF -- "$needle" <<< "$haystack"; then
    pass "$name"
  else
    fail "$name" "expected '$needle' in:" "$haystack"
  fi
}

# Asserts the marker a bundle entrypoint printed, and that a failure is also a non-zero exit --
# either one alone would let the caller draw the wrong conclusion. want_rc is "zero" or "nonzero".
expect_bundle_status() {
  local name=$1 want_status=$2 want_rc=$3 rc=$4 output=$5

  if ! grep -q "BUNDLE_STATUS=$want_status" <<< "$output"; then
    fail "$name" "expected BUNDLE_STATUS=$want_status in:" "$output"
    return
  fi
  if [ "$want_status" = "FAILED" ] && grep -q "BUNDLE_STATUS=OK" <<< "$output"; then
    fail "$name" "reported OK as well as FAILED"
    return
  fi
  if [ "$want_rc" = "zero" ] && [ "$rc" -ne 0 ]; then
    fail "$name" "expected exit 0, got $rc"
    return
  fi
  if [ "$want_rc" = "nonzero" ] && [ "$rc" -eq 0 ]; then
    fail "$name" "expected a non-zero exit, got 0"
    return
  fi
  pass "$name"
}

# Writes an executable stub for <name>, body read from stdin, and prints the directory holding it.
# Each stub gets its own directory on purpose: a single shared one would put every stub on PATH at
# once, so an `awk` that exits 1 would break a case that only meant to intercept `cp`.
#
#   awk_stub="$(stub_command awk <<'STUB'
#   #!/bin/sh
#   exit 1
#   STUB
#   )"
#   PATH="$awk_stub:$PATH" ...
stub_command() {
  local name=$1
  local dir="$TMP_ROOT/stubs/$name"
  mkdir -p "$dir"
  cat > "$dir/$name"
  chmod +x "$dir/$name"
  printf '%s' "$dir"
}

# Records that one section could not run, and returns. Used when a precondition the test cannot
# create is absent -- a section that asserts nothing must not read as a section that passed.
#
# Deliberately not an exit: a file groups several subjects, and one unrunnable subject must not take
# the others' results with it. finish() turns "nothing ran at all" into exit 77.
skip_section() {
  echo "skip: $1"
  shift
  local line
  for line in "$@"; do
    echo "      $line"
  done
  skips=$((skips + 1))
}

finish() {
  local subject=$1
  if [ "$failures" -ne 0 ]; then
    echo "$failures check(s) failed"
    exit 1
  fi
  if [ "$checks_run" -eq 0 ]; then
    echo "no $subject checks ran"
    exit 77
  fi
  if [ "$skips" -ne 0 ]; then
    echo "All $subject checks passed ($skips section(s) skipped)"
  else
    echo "All $subject checks passed"
  fi
}
