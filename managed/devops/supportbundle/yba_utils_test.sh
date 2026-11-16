#!/bin/bash
#
# Copyright 2026 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

# Cases covering devops/bin/yba_utils.sh, the script that collects from the YBA host itself.
#
#   bash managed/devops/supportbundle/yba_utils_test.sh

# shellcheck source=./test_helpers.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/test_helpers.sh"

YBA_UTILS="$DEVOPS_BIN/yba_utils.sh"

APP_LOGS_REGEX='application-log-\d{4}-\d{2}-\d{2}(\.gz)?'
APP_LOGS_SDF="'application-log-'yyyy-MM-dd"

# ===============================================================================================
# Bundle tar status
# ===============================================================================================
# A bundle entrypoint whose tar fails must report BUNDLE_STATUS=FAILED instead of OK. YBA only
# checks that the tar exists, so an entrypoint that ignores tar's exit status ships a truncated
# archive as a successful component.

# A regular file where a directory is needed: tar cannot create the archive, and it fails the same
# way for root, unlike a chmod-based setup.
BLOCKER="$TMP_ROOT/blocker"
printf 'regular file, not a directory\n' > "$BLOCKER"

STATUS_LOG_DIR="$TMP_ROOT/yba-logs"
mkdir -p "$STATUS_LOG_DIR"
printf 'yba log line\n' > "$STATUS_LOG_DIR/application-log-2026-01-15"

run_application_logs_bundle() {
  bash "$YBA_UTILS" create_application_logs_bundle \
    --log_dir "$STATUS_LOG_DIR" \
    --start_date "2026-01-01" \
    --end_date "2026-01-31" \
    --local_tar_path "$1" \
    --application_logs_regex "$APP_LOGS_REGEX" \
    --application_logs_sdf "$APP_LOGS_SDF" 2>&1
}

output="$(run_application_logs_bundle "$TMP_ROOT/out/application-logs.tar.gz")"
expect_bundle_status "YBA entrypoint reports OK when tar succeeds" OK zero "$?" "$output"

output="$(run_application_logs_bundle "$BLOCKER/application-logs.tar.gz")"
expect_bundle_status "YBA entrypoint reports FAILED when tar fails" FAILED nonzero "$?" "$output"

# A tar that writes part of the archive and then fails, as on a disk that fills mid-write.
PARTIAL_TAR_STUB="$(stub_command tar <<'STUB'
#!/bin/sh
while [ $# -gt 0 ]; do
  if [ "$1" = "-czf" ]; then printf 'partial' > "$2"; fi
  shift
done
exit 2
STUB
)"
partial_tar="$TMP_ROOT/out/application-logs-partial.tar.gz"
output="$(export PATH="$PARTIAL_TAR_STUB:$PATH"; run_application_logs_bundle "$partial_tar")"
expect_bundle_status "YBA entrypoint reports FAILED when tar fails part-way" FAILED nonzero "$?" \
  "$output"
check "YBA entrypoint removes a partial tar" \
  "$([ -e "$partial_tar" ] && echo present || echo absent)" absent

# ===============================================================================================
# Log selection is timezone independent
# ===============================================================================================
# --start_date and --end_date are UTC instants: YBA formats them with UNIVERSE_LOGS_INSTANT_FORMAT,
# which is withZone(ZoneOffset.UTC). Deciding whether the live application.log is in range by
# reading the local clock compared two different clocks, and east of UTC local midnight arrives
# first -- so between local midnight and UTC midnight the live log, the one an operator most wants,
# was left out with no warning.

WINDOW_LOG_DIR="$TMP_ROOT/window-logs"
mkdir -p "$WINDOW_LOG_DIR"
printf 'live log line\n' > "$WINDOW_LOG_DIR/application.log"

# The bug needs the host's local date to be ahead of the UTC date, which only happens east of UTC
# and only after local midnight. Pick the smallest real offset that puts us there; below 10:00 UTC
# no real zone is far enough east, so say so rather than pass vacuously.
UTC_HOUR=$(date -u +%-H)
NEEDED=$(( 24 - UTC_HOUR ))
if [ "$NEEDED" -gt 14 ]; then
  skip_section \
    "at ${UTC_HOUR}:00 UTC no real timezone is far enough east to cross local midnight" \
    "(this window is reachable from 10:00 UTC onwards)"
else
  EAST_TZ="Etc/GMT-${NEEDED}"

  START="$(date -u -d '1 day ago' +%Y-%m-%dT00:00:00Z)"
  END="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

  run_under_tz() {
    local tz=$1 out=$2
    TZ="$tz" bash "$YBA_UTILS" create_application_logs_bundle \
      --log_dir "$WINDOW_LOG_DIR" \
      --start_date "$START" \
      --end_date "$END" \
      --local_tar_path "$out" \
      --application_logs_regex "$APP_LOGS_REGEX" \
      --application_logs_sdf "$APP_LOGS_SDF" > /dev/null 2>&1
    tar -tzf "$out" 2>/dev/null | sed 's|.*/||' | sort
  }

  utc_files="$(run_under_tz UTC "$TMP_ROOT/utc.tar.gz")"
  east_files="$(run_under_tz "$EAST_TZ" "$TMP_ROOT/east.tar.gz")"

  echo "window: $START .. $END   (east zone: $EAST_TZ)"

  if grep -q "application.log" <<< "$utc_files"; then
    pass "the live log is collected on a UTC host"
  else
    fail "the live log is missing even on a UTC host"
  fi

  if grep -q "application.log" <<< "$east_files"; then
    pass "the live log is collected east of UTC, past local midnight"
  else
    fail "the live log was dropped on $EAST_TZ; the range check is reading the local clock"
  fi

  if [ "$utc_files" = "$east_files" ]; then
    pass "the selection does not depend on the host timezone"
  else
    fail "selection differs by timezone" \
      "UTC : $(tr '\n' ' ' <<< "$utc_files")" \
      "east: $(tr '\n' ' ' <<< "$east_files")"
  fi
fi

# --- dispatch guard -----------------------------------------------------------------------------
# The script runs its first argument, so anything but a function defined in it must be refused.
# ===============================================================================================
# Temp directory and copy failures
# ===============================================================================================
# The work directory goes under TMPDIR, which YBA points at a directory it deletes itself (a
# timeout is a SIGKILL); an EXIT trap removes it on YBA's SIGTERM at abort. A log that fails to
# copy is reported and left out.

YBA_ST_LOGS="$TMP_ROOT/yba-st-logs"
YBA_ST_STUBS="$TMP_ROOT/yba-st-stubs"
mkdir -p "$YBA_ST_LOGS" "$YBA_ST_STUBS/cp-hang" "$YBA_ST_STUBS/cp-fail" "$TMP_ROOT/yba-tmp"
printf 'yba log one\n' > "$YBA_ST_LOGS/application-log-2026-01-15"
printf 'yba log two\n' > "$YBA_ST_LOGS/application-log-2026-01-16"
cat > "$YBA_ST_STUBS/cp-hang/cp" <<EOF
#!/bin/bash
echo "\$\$" > "$TMP_ROOT/yba-cp.pid"
exec sleep 30
EOF
cat > "$YBA_ST_STUBS/cp-fail/cp" <<EOF
#!/bin/bash
for arg in "\$@"; do
  if [[ "\$arg" == *2026-01-16 ]]; then
    printf 'partial' > "\${@: -1}"
    echo "cp: error reading '\$arg': Input/output error" >&2
    exit 1
  fi
done
exec $(command -v cp) "\$@"
EOF
chmod +x "$YBA_ST_STUBS/cp-hang/cp" "$YBA_ST_STUBS/cp-fail/cp"

run_st_application_logs() {
  bash "$YBA_UTILS" create_application_logs_bundle \
    --log_dir "$YBA_ST_LOGS" \
    --start_date "2026-01-01" \
    --end_date "2026-01-31" \
    --local_tar_path "$1" \
    --application_logs_regex "$APP_LOGS_REGEX" \
    --application_logs_sdf "$APP_LOGS_SDF" 2>&1
}

TMPDIR="$TMP_ROOT/yba-tmp" PATH="$YBA_ST_STUBS/cp-hang:$PATH" \
  run_st_application_logs "$TMP_ROOT/out/app-term.tar.gz" > /dev/null &
yba_job=$!
for _ in $(seq 50); do [ -s "$TMP_ROOT/yba-cp.pid" ] && break; sleep 0.1; done
yba_cp_pid="$(cat "$TMP_ROOT/yba-cp.pid")"
# The script itself: cp's parent may be a command-substitution fork, which has the same command
# line but started later.
yba_script_pid="$(pgrep -o -f "create_application_logs_bundle --log_dir $YBA_ST_LOGS")"
check "temp dir: the work directory is under TMPDIR" \
  "$(find "$TMP_ROOT/yba-tmp" -mindepth 1 -maxdepth 1 | wc -l)" "1"
kill -TERM "$yba_script_pid" 2>/dev/null
wait "$yba_job" 2>/dev/null
kill "$yba_cp_pid" 2>/dev/null
check "temp dir: removed when the script is stopped with SIGTERM" \
  "$(find "$TMP_ROOT/yba-tmp" -mindepth 1 -maxdepth 1 | wc -l)" "0"

output="$(PATH="$YBA_ST_STUBS/cp-fail:$PATH" run_st_application_logs \
  "$TMP_ROOT/out/app-copy.tar.gz")"
check_contains "copy failure: reported with the file and the error" "$output" \
  "COPY_FAILURE=$YBA_ST_LOGS/application-log-2026-01-16: cp: error reading"
check_contains "copy failure: the rest is still collected" "$output" "BUNDLE_STATUS=OK"
check "copy failure: the partial copy is not in the tar" \
  "$(tar -tzf "$TMP_ROOT/out/app-copy.tar.gz" | grep -v '/$' | sed 's|^\./||' | sort |
    tr '\n' ' ')" "application_logs/application-log-2026-01-15 "

guard_out=$(bash "$YBA_UTILS" bash -c 'echo ran' 2>&1)
guard_rc=$?
check "dispatch: a non-function first argument exits 2" "$guard_rc" "2"
check_contains "dispatch: the refusal names it" "$guard_out" "Unknown function: bash"
if grep -q "^ran$" <<< "$guard_out"; then
  fail "dispatch: the non-function was run" "$guard_out"
else
  pass "dispatch: the non-function is not run"
fi
# An option-like argument must not be read as an option to declare and slip through.
for opt in -f -p; do
  opt_out=$(bash "$YBA_UTILS" "$opt" 2>&1)
  opt_rc=$?
  check "dispatch: '$opt' as the first argument exits 2" "$opt_rc" "2"
  check_contains "dispatch: the '$opt' refusal names it" "$opt_out" "Unknown function: $opt"
done
noargs_out=$(bash "$YBA_UTILS" 2>&1)
check "dispatch: no arguments is a no-op" "$?:$noargs_out" "0:"

finish "yba_utils"
