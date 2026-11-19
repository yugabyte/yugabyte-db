#!/bin/bash
#
# Copyright 2026 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

# Support bundle v2 node script. YBA stages it on the node before running it. node_utils.sh is
# streamed as one argument, which Linux caps at 128 KiB, so SB v2 code goes here, not there.

# Same as in node_utils.sh; support_bundle_utils_test.sh checks that the copies match.
get_paths_and_sizes() {
  remote_dir_path=$1
  shift
  max_depth=$1
  shift
  file_type=$1
  shift
  temp_file_path=$1

  find "$remote_dir_path" -maxdepth "$max_depth" -type "$file_type" \
  -exec ls -ltp {} + | awk '{print $5, $9}' > "$temp_file_path"
}

# This function returns a list of file names and their respective sizes, in a given directory.
# Sorts the list by modification time, with newest first.
get_paths_and_sizes_within_dates() {
  remote_dir_path=$1
  shift
  temp_file_path=$1
  shift
  start_date=$1
  shift
  end_date=$1

  # This displays the size in bytes ($5) and file path ($9) on a new line.
  cd "$remote_dir_path"
  find . -type f -newermt "$start_date" ! -newermt "$end_date" \
  -exec ls -ltp {} + | awk '{print $5, $9}' > "$temp_file_path"
}

# Support bundle v2 estimate-size helpers (stdout contract: ESTIMATE_STATUS / ESTIMATE_SIZE).
_byte_size_for_path() {
  local path=$1
  local size=0
  if [ -f "$path" ] || [ -L "$path" ]
  then
    size=$(stat -c%s "$path" 2>/dev/null || stat -f%z "$path" 2>/dev/null || echo 0)
  elif [ -d "$path" ]
  then
    size=$(du -sb "$path" 2>/dev/null | awk '{print $1}')
    size=${size:-0}
  fi
  printf '%s' "$size"
}

_emit_estimate_result() {
  local total=${1:-0}
  if ! [[ "$total" =~ ^[0-9]+$ ]]
  then
    total=0
  fi
  if [ "$total" -le 0 ]
  then
    echo "ESTIMATE_STATUS=EMPTY"
    echo "ESTIMATE_SIZE=0"
  else
    echo "ESTIMATE_STATUS=OK"
    echo "ESTIMATE_SIZE=$total"
  fi
}

_sum_byte_sizes_for_paths() {
  local total=0
  local path size
  for path in "$@"
  do
    if [ -z "$path" ]
    then
      continue
    fi
    size=$(_byte_size_for_path "$path")
    total=$((total + size))
  done
  printf '%s' "$total"
}

_sum_byte_sizes_from_file_list() {
  local list_file=$1
  local total=0
  local size path
  if [ ! -s "$list_file" ]
  then
    printf '0'
    return 0
  fi
  while read -r size path
  do
    if [ -z "$path" ]
    then
      continue
    fi
    if [[ "$size" =~ ^[0-9]+$ ]]
    then
      total=$((total + size))
    else
      total=$((total + $(_byte_size_for_path "$path")))
    fi
  done < "$list_file"
  printf '%s' "$total"
}

_run_support_bundle_estimate_wrapper() {
  local create_fn=$1
  shift
  SUPPORT_BUNDLE_ESTIMATE_ONLY=1 "$create_fn" "$@"
}

estimate_universe_logs_size() {
  _run_support_bundle_estimate_wrapper create_universe_logs_bundle "$@"
}

estimate_system_logs_size() {
  _run_support_bundle_estimate_wrapper create_system_logs_bundle "$@"
}

estimate_ybc_logs_size() {
  _run_support_bundle_estimate_wrapper create_ybc_logs_bundle "$@"
}

estimate_consensus_meta_size() {
  _run_support_bundle_estimate_wrapper create_consensus_meta_bundle "$@"
}

estimate_tablet_meta_size() {
  _run_support_bundle_estimate_wrapper create_tablet_meta_bundle "$@"
}

estimate_node_agent_size() {
  _run_support_bundle_estimate_wrapper create_node_agent_bundle "$@"
}

estimate_core_files_size() {
  _run_support_bundle_estimate_wrapper create_core_files_bundle "$@"
}


# YBA installs its helpers in <YB home>/bin and passes the YB home as --yb_home_dir, read before
# dispatch; nothing guesses at a home directory.
yb_helper_path() {
  [ -n "${YB_HOME_DIR:-}" ] && printf '%s/bin/%s' "$YB_HOME_DIR" "$1"
}

# yb-log-parse replaces inline Python for log parsing on DB nodes.
resolve_yb_log_parse_bin() {
  # Checked here rather than at each of the five call sites, so a helper that is present but
  # rejected by the smoke test is bypassed by all of them, not just the two that used to look.
  if [ -n "${YB_LOG_PARSE_SKIP:-}" ]
  then
    return 1
  fi
  local candidate=${YB_LOG_PARSE_BIN:-$(yb_helper_path yb-log-parse)}
  if [ -n "$candidate" ] && [ -x "$candidate" ]
  then
    echo "$candidate"
    return 0
  fi
  return 1
}
# Prefers the Go helper but never fails the component for want of it. Every one of its five uses
# -- match, decode-b64, normalize-compact, normalize-sortable, add-days -- already falls back to
# shell or Python, so a node that never received the binary still collects its logs, just more
# slowly. This used to exit 1, which meant UniverseLogs and YbcLogs, the two largest components,
# were lost outright on a node the helper could not be copied to.
prefer_yb_log_parse_bin() {
  local yb_log_parse_bin
  local expected
  local smoke_output
  local smoke_status

  if [ -n "${YB_LOG_PARSE_SKIP:-}" ]
  then
    echo "yb-log-parse helper skipped by YB_LOG_PARSE_SKIP;" \
      "using the shell/Python date handling." >&2
    return 0
  fi

  yb_log_parse_bin="$(resolve_yb_log_parse_bin 2>/dev/null)"
  if [ -z "$yb_log_parse_bin" ]
  then
    expected=${YB_LOG_PARSE_BIN:-$(yb_helper_path yb-log-parse)}
    echo "yb-log-parse helper not found at ${expected:-<no --yb_home_dir>};" \
      "using the shell/Python date handling instead." >&2
    export YB_LOG_PARSE_SKIP=1
    return 0
  fi

  smoke_output=$("$yb_log_parse_bin" add-days 20260101 0 2>&1)
  smoke_status=$?
  if [ "$smoke_status" -ne 0 ] || [ "$smoke_output" != "20260101" ]
  then
    echo "yb-log-parse helper at $yb_log_parse_bin failed validation" \
      "(exit $smoke_status): $smoke_output; using the shell/Python date handling instead." >&2
    export YB_LOG_PARSE_SKIP=1
    return 0
  fi

  echo "Using yb-log-parse helper: $yb_log_parse_bin"
  export YB_LOG_PARSE_BIN="$yb_log_parse_bin"
}

# YB_DUMP_CDC_STATE_BIN overrides the YB home's copy, for tests.
expected_yb_dump_cdc_state_bin() {
  printf '%s' "${YB_DUMP_CDC_STATE_BIN:-$(yb_helper_path yb-dump-cdc-state)}"
}

resolve_yb_dump_cdc_state_bin() {
  local candidate
  candidate="$(expected_yb_dump_cdc_state_bin)"
  if [ -n "$candidate" ] && [ -x "$candidate" ]
  then
    echo "$candidate"
    return 0
  fi
  return 1
}

require_yb_dump_cdc_state_bin() {
  local dump_bin
  local expected
  local smoke_output
  local smoke_status

  dump_bin="$(resolve_yb_dump_cdc_state_bin 2>/dev/null)"
  if [ -z "$dump_bin" ]
  then
    expected="$(expected_yb_dump_cdc_state_bin)"
    echo "yb-dump-cdc-state helper is required but was not found at" \
      "${expected:-<no --yb_home_dir>}." >&2
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  smoke_output=$("$dump_bin" smoke 2>&1)
  smoke_status=$?
  if [ "$smoke_status" -ne 0 ] || [ "$smoke_output" != "ok" ]
  then
    echo "yb-dump-cdc-state helper at $dump_bin failed validation" \
      "(exit $smoke_status): $smoke_output" >&2
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  echo "Using yb-dump-cdc-state helper: $dump_bin"
  export YB_DUMP_CDC_STATE_BIN="$dump_bin"
}

# Redacts every server.conf in the staging dir with the yb-redact-server-conf binary YBA verified
# (--redact_bin). A copy that can't be redacted is deleted here, so an unredacted server.conf never
# leaves the node. Each one is reported on stdout as REDACTION_FAILURE=<path>: <reason>, which YBA
# logs as an error, then one marker:
#   NODE           - every server.conf was redacted
#   FAILED         - one or more were dropped
#   NOT_APPLICABLE - the staging dir holds no server.conf
# Returns non-zero when a copy can't be deleted; the caller must then fail without building a tar.
redact_server_conf_in_staging() {
  staging_dir=$1
  redact_bin=$2
  yb_software_version=$3
  sensitive_gflags_b64=$4
  drop_server_conf=${5:-}

  if [ -z "$staging_dir" ] || [ ! -d "$staging_dir" ]
  then
    echo "REDACTION_STATUS=NOT_APPLICABLE"
    return 0
  fi

  local server_conf_files=()
  while IFS= read -r -d '' conf_file
  do
    server_conf_files+=("$conf_file")
  done < <(find "$staging_dir" -type f -name server.conf -print0)

  if [ ${#server_conf_files[@]} -eq 0 ]
  then
    echo "REDACTION_STATUS=NOT_APPLICABLE"
    return 0
  fi

  # Only the binary YBA verified is used: a stale copy found elsewhere could under-redact.
  local unavailable_reason=""
  if [ "$drop_server_conf" = "true" ]
  then
    unavailable_reason="redaction helper unavailable on this node"
  elif [ -z "$redact_bin" ]
  then
    # YBA sends --redact_bin only to components its manifest marks redactServerConf.
    unavailable_reason="this component doesn't redact server.conf (no --redact_bin)"
  elif [ ! -x "$redact_bin" ]
  then
    unavailable_reason="redaction helper not executable (redact_bin='${redact_bin}')"
  fi

  local conf_file rel_path reason redact_args rc dropped=0
  for conf_file in "${server_conf_files[@]}"
  do
    rel_path="${conf_file#"$staging_dir"/}"
    reason="$unavailable_reason"
    if [ -z "$reason" ]
    then
      redact_args=(redact-file --in-place --file "$conf_file")
      if [ -n "$yb_software_version" ]
      then
        redact_args+=(--yb-software-version "$yb_software_version")
      fi
      if [ -n "$sensitive_gflags_b64" ]
      then
        redact_args+=(--sensitive-gflags-b64 "$sensitive_gflags_b64")
      fi
      if "$redact_bin" "${redact_args[@]}" >&2
      then
        continue
      else
        rc=$?
      fi
      reason="redaction helper failed (exit $rc)"
    fi
    if ! rm -f "$conf_file" || [ -e "$conf_file" ]
    then
      echo "REDACTION_FAILURE=$rel_path: $reason; could not delete the unredacted copy, so" \
        "nothing from this component is collected"
      echo "REDACTION_STATUS=FAILED"
      return 1
    fi
    echo "REDACTION_FAILURE=$rel_path: $reason; dropped from the bundle"
    dropped=$((dropped + 1))
  done

  if [ "$dropped" -gt 0 ]
  then
    echo "REDACTION_STATUS=FAILED"
  else
    echo "REDACTION_STATUS=NODE"
  fi
}

# Redacts the staged server.conf files, then reports EMPTY when dropping them left nothing to tar.
# Fails the component when an unredacted copy could not be deleted.
redact_staged_server_conf_or_exit() {
  staging_dir=$1
  if ! redact_server_conf_in_staging "$@"
  then
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi
  if [ -z "$(find "$staging_dir" -type f -print -quit)" ]
  then
    echo "Nothing left to collect after server.conf redaction."
    echo "BUNDLE_STATUS=EMPTY"
    exit 0
  fi
}

# Takes the server.conf redaction flags YBA appends for components its manifest marks
# redactServerConf, and leaves the other arguments in sb_remaining_args for the caller's own loop.
take_server_conf_redaction_args() {
  redact_bin=""
  yb_software_version=""
  sensitive_gflags_b64=""
  drop_server_conf=""
  sb_remaining_args=()
  while [ "$#" -gt 0 ]
  do
    case "$1" in
      --redact_bin|--yb_software_version|--sensitive_gflags_b64)
        case "$1" in
          --redact_bin) redact_bin=${2:-} ;;
          --yb_software_version) yb_software_version=${2:-} ;;
          --sensitive_gflags_b64) sensitive_gflags_b64=${2:-} ;;
        esac
        shift
        [ "$#" -gt 0 ] && shift
        ;;
      --drop_server_conf)
        drop_server_conf="true"
        shift
        ;;
      *)
        sb_remaining_args+=("$1")
        shift
        ;;
    esac
  done
}

# File lists name paths under a base directory. A path that is absolute or has a '..' segment would
# read, or stage a copy, outside it, so the component fails instead.
require_relative_file_paths() {
  local rel_path
  for rel_path in "$@"
  do
    case "/$rel_path/" in
      //*|*/../*)
        echo "Refusing file path '$rel_path': it must be relative, with no '..' segment"
        echo "BUNDLE_STATUS=FAILED"
        exit 1
        ;;
    esac
  done
}

parse_file_type_and_date_from_name() {
  file_name=$1
  if [[ "$file_name" == *"WARNING"* ]]
  then
    return 1
  fi

  # glog rotated files, e.g.
  # yb-tserver.host.yugabyte.log.ERROR.20260614-171118.18913
  if [[ "$file_name" =~ ^.*\.log\.(INFO|ERROR|FATAL)\.([0-9]{8})-([0-9]{6})\..*$ ]]
  then
    echo "glog-${BASH_REMATCH[1]}|${BASH_REMATCH[2]}${BASH_REMATCH[3]}"
    return 0
  fi

  if [[ "$file_name" =~ ^(.*)([0-9]{8})-([0-9]{6})\..*$ ]] \
    && ([[ "$file_name" == yb-master* ]] || [[ "$file_name" == yb-tserver* ]])
  then
    echo "${BASH_REMATCH[1]}|${BASH_REMATCH[2]}${BASH_REMATCH[3]}"
    return 0
  fi

  if [[ "$file_name" =~ ^(postgresql-)([0-9]{4}-[0-9]{2}-[0-9]{2})_([0-9]{6}).*$ ]]
  then
    echo "${BASH_REMATCH[1]}|${BASH_REMATCH[2]//-/}${BASH_REMATCH[3]}"
    return 0
  fi

  if [[ "$file_name" =~ ^(ysql-conn-mgr-)([0-9]{4}-[0-9]{2}-[0-9]{2})_([0-9]{6}).*$ ]]
  then
    echo "${BASH_REMATCH[1]}|${BASH_REMATCH[2]//-/}${BASH_REMATCH[3]}"
    return 0
  fi

  return 1
}

parse_file_type_and_date_from_ybc_name() {
  file_name=$1
  if [[ "$file_name" == *"WARNING"* ]]
  then
    return 1
  fi

  # yb-controller / yb-controller-server glog names, e.g.
  # yb-controller-server.host.log.INFO.20260613-191738.19169.gz
  if [[ "$file_name" =~ ^(.*yb-controller.*)([0-9]{8}-[0-9]{6}) ]]
  then
    echo "${BASH_REMATCH[1]}|${BASH_REMATCH[2]//-/}"
    return 0
  fi

  return 1
}

parse_file_type_and_date_from_ybc_name_loose() {
  file_name=$1
  if [[ "$file_name" != *yb-controller* ]]
  then
    return 1
  fi
  if [[ "$file_name" == *"WARNING"* ]]
  then
    return 1
  fi
  if [[ "$file_name" =~ ([0-9]{8}-[0-9]{6}) ]]
  then
    echo "yb-controller|${BASH_REMATCH[1]//-/}"
    return 0
  fi
  if [[ "$file_name" =~ ([0-9]{8}) ]]
  then
    echo "yb-controller|${BASH_REMATCH[1]}000000"
    return 0
  fi
  return 1
}

parse_file_type_and_date_from_regexes() {
  file_name=$1
  shift
  regexes=("$@")
  if [ ${#regexes[@]} -eq 0 ]
  then
    return 1
  fi

  yb_log_parse_bin="$(resolve_yb_log_parse_bin 2>/dev/null)"
  if [ -n "$yb_log_parse_bin" ]
  then
    match_args=(match --file-name "$file_name")
    for pattern in "${regexes[@]}"
    do
      match_args+=(--regex "$pattern")
    done
    parse_output=$("$yb_log_parse_bin" "${match_args[@]}" 2>/dev/null)
    if [ $? -eq 0 ] && [ -n "$parse_output" ]
    then
      echo "$parse_output"
      return 0
    fi
  fi

  python_bin="python3"
  if ! command -v "$python_bin" >/dev/null 2>&1
  then
    python_bin="python"
  fi
  if ! command -v "$python_bin" >/dev/null 2>&1
  then
    return 1
  fi

  "$python_bin" - "$file_name" "${regexes[@]}" <<'PY'
import datetime as dt
import re
import sys

file_name = sys.argv[1]
patterns = sys.argv[2:]
LOG_FILE_DATE_FORMATS = (
    "%Y%m%d-%H%M%S",
    "%Y-%m-%d_%H%M%S",
    "%Y-%m-%d",
    "%Y%m%d",
)

def to_sortable_timestamp(group):
    if group is None:
        return ""
    value = group.strip()
    for fmt in LOG_FILE_DATE_FORMATS:
        try:
            parsed = dt.datetime.strptime(value, fmt)
            return parsed.strftime("%Y%m%d%H%M%S")
        except ValueError:
            pass
    return ""

for pattern in patterns:
    if not pattern:
        continue
    try:
        m = re.fullmatch(pattern, file_name)
    except re.error:
        continue
    if not m:
        continue

    file_type = m.group(1) if m.lastindex and m.lastindex >= 1 else ""
    file_timestamp = ""
    for group in m.groups():
        if group is None:
            continue
        normalized_timestamp = to_sortable_timestamp(group)
        if normalized_timestamp:
            file_timestamp = normalized_timestamp
            break

    if file_type and file_timestamp:
        print(f"{file_type}|{file_timestamp}")
        sys.exit(0)

sys.exit(1)
PY
}

decode_base64_string() {
  encoded=$1
  if [ -z "$encoded" ]
  then
    echo ""
    return 0
  fi
  if command -v base64 >/dev/null 2>&1
  then
    printf '%s' "$encoded" | base64 --decode 2>/dev/null
    return $?
  fi

  yb_log_parse_bin="$(resolve_yb_log_parse_bin 2>/dev/null)"
  if [ -n "$yb_log_parse_bin" ]
  then
    decoded_output=$("$yb_log_parse_bin" decode-b64 "$encoded" 2>/dev/null)
    if [ $? -eq 0 ]
    then
      printf '%s' "$decoded_output"
      return 0
    fi
  fi

  if command -v openssl >/dev/null 2>&1
  then
    decoded_output=$(printf '%s' "$encoded" | openssl base64 -d -A 2>/dev/null)
    if [ $? -eq 0 ]
    then
      printf '%s' "$decoded_output"
      return 0
    fi
  fi

  python_bin="python3"
  if ! command -v "$python_bin" >/dev/null 2>&1
  then
    python_bin="python"
  fi
  if ! command -v "$python_bin" >/dev/null 2>&1
  then
    return 1
  fi
  "$python_bin" - "$encoded" <<'PY'
import base64
import sys
print(base64.b64decode(sys.argv[1]).decode("utf-8"), end="")
PY
}

normalize_date_to_compact() {
  raw_value=$1
  if [ -z "$raw_value" ]
  then
    return 1
  fi

  if [ -z "${YB_LOG_PARSE_SKIP:-}" ]
  then
    yb_log_parse_bin="$(resolve_yb_log_parse_bin 2>/dev/null)"
    if [ -n "$yb_log_parse_bin" ]
    then
      compact_output=$("$yb_log_parse_bin" normalize-compact "$raw_value" 2>/dev/null)
      if [ $? -eq 0 ] && [ -n "$compact_output" ]
      then
        echo "$compact_output"
        return 0
      fi
    fi
  fi

  python_bin="python3"
  if ! command -v "$python_bin" >/dev/null 2>&1
  then
    python_bin="python"
  fi
  if ! command -v "$python_bin" >/dev/null 2>&1
  then
    return 1
  fi

  "$python_bin" - "$raw_value" <<'PY'
import datetime as dt
import re
import sys

raw = sys.argv[1].strip()
if not raw:
    sys.exit(1)

if raw.isdigit() and len(raw) == 8:
    try:
        print(dt.datetime.strptime(raw, "%Y%m%d").strftime("%Y%m%d"))
        sys.exit(0)
    except ValueError:
        pass

if raw.isdigit():
    value = int(raw)
    if len(raw) >= 13:
        value = value / 1000.0
    try:
        print(dt.datetime.fromtimestamp(value).strftime("%Y%m%d"))
        sys.exit(0)
    except Exception:
        pass

match = re.search(r"\d{8}", raw)
if match:
    print(match.group(0))
    sys.exit(0)

match = re.search(r"\d{4}-\d{2}-\d{2}", raw)
if match:
    print(match.group(0).replace("-", ""))
    sys.exit(0)

sys.exit(1)
PY
}

normalize_date_to_sortable() {
  raw_value=$1
  if [ -z "$raw_value" ]
  then
    return 1
  fi

  yb_log_parse_bin="$(resolve_yb_log_parse_bin 2>/dev/null)"
  if [ -n "$yb_log_parse_bin" ]
  then
    sortable_output=$("$yb_log_parse_bin" normalize-sortable "$raw_value" 2>/dev/null)
    if [ $? -eq 0 ] && [ -n "$sortable_output" ]
    then
      echo "$sortable_output"
      return 0
    fi
  fi

  python_bin="python3"
  if ! command -v "$python_bin" >/dev/null 2>&1
  then
    python_bin="python"
  fi
  if ! command -v "$python_bin" >/dev/null 2>&1
  then
    return 1
  fi

  "$python_bin" - "$raw_value" <<'PY'
import datetime as dt
import re
import sys

raw = sys.argv[1].strip()
if not raw:
    sys.exit(1)

formats = (
    "%Y-%m-%dT%H:%M:%SZ",
    "%Y-%m-%dT%H:%M:%S",
    "%Y-%m-%d %H:%M:%S",
    "%Y-%m-%d",
    "%Y%m%d-%H%M%S",
    "%Y%m%d",
)

for fmt in formats:
    try:
        parsed = dt.datetime.strptime(raw, fmt)
        print(parsed.strftime("%Y%m%d%H%M%S"))
        sys.exit(0)
    except ValueError:
        pass

if raw.isdigit() and len(raw) == 14:
    print(raw)
    sys.exit(0)

if raw.isdigit() and len(raw) == 8:
    try:
        parsed = dt.datetime.strptime(raw, "%Y%m%d")
        print(parsed.strftime("%Y%m%d%H%M%S"))
        sys.exit(0)
    except ValueError:
        pass

match = re.search(r"(\d{4}-\d{2}-\d{2})[T _](\d{2}):(\d{2}):(\d{2})", raw)
if match:
    day = match.group(1).replace("-", "")
    print(f"{day}{match.group(2)}{match.group(3)}{match.group(4)}")
    sys.exit(0)

match = re.search(r"(\d{8}-\d{6})", raw)
if match:
    print(match.group(1).replace("-", ""))
    sys.exit(0)

match = re.search(r"\d{4}-\d{2}-\d{2}", raw)
if match:
    print(match.group(0).replace("-", "") + "000000")
    sys.exit(0)

match = re.search(r"\d{8}", raw)
if match:
    print(match.group(0) + "000000")
    sys.exit(0)

sys.exit(1)
PY
}

filter_pg_audit_log_file() {
  source_file_path=$1
  target_file_path=$2

  mkdir -p "$(dirname "$target_file_path")"
  if [[ "$source_file_path" == *.log.gz ]]
  then
    if zcat "$source_file_path" 2>/dev/null | awk '!/AUDIT:/' | gzip > "$target_file_path"
    then
      return 0
    fi
  else
    if awk '!/AUDIT:/' "$source_file_path" > "$target_file_path"
    then
      return 0
    fi
  fi

  # No fallback to copying the source. The filter is the only thing keeping AUDIT rows out of a
  # bundle collected with pg audit filtering on, so copying the unfiltered file to the filtered_
  # name would ship every audit row under a name that says they were removed -- inverting the
  # operator's request rather than failing it. Losing the file is what the v1 collector does
  # (UniverseLogsComponent deletes the original whether or not the filter worked), so v2 keeps the
  # same trade: you may lose a log, you never ship audit rows.
  rm -f "$target_file_path"
  echo "WARN: could not filter pg audit log $source_file_path;" \
       "omitting it from the bundle" >&2
  return 1
}

# cp, with the destination last. A failed copy is removed and reported as
# COPY_FAILURE=<source>: <error>, which YBA logs, and the component carries on with the rest.
copy_or_report() {
  local source_file=${*: -2:1} dest=${*: -1} err
  if err=$(cp "$@" 2>&1)
  then
    return 0
  fi
  if [ -f "$dest" ] && [ ! -L "$dest" ]
  then
    rm -f -- "$dest"
  fi
  echo "COPY_FAILURE=${source_file}: ${err//$'\n'/; }"
  return 1
}

add_file_to_staging() {
  source_file_path=$1
  staging_root=$2
  relative_target_path=$3
  filter_pgaudit=$4

  staging_target="$staging_root/$relative_target_path"
  mkdir -p "$(dirname "$staging_target")"
  file_name="$(basename "$source_file_path")"

  if [[ "$filter_pgaudit" == "true" ]] && [[ "$relative_target_path" == tserver/logs/* ]] \
    && [[ "$file_name" == postgresql-* ]]
  then
    filtered_target_path="$(dirname "$staging_target")/filtered_$(basename "$staging_target")"
    # A log that cannot be filtered is dropped with a warning rather than failing the whole
    # component; the rest of the logs are still worth collecting.
    filter_pg_audit_log_file "$source_file_path" "$filtered_target_path" || true
    return 0
  fi

  if [ -L "$source_file_path" ] || [ -f "$source_file_path" ]
  then
    copy_or_report -Lp "$source_file_path" "$staging_target" || true
  fi
}

# Copy into user-owned staging. When use_sudo=true, only read uses sudo (cp to stdout); the shell
# redirect creates dest owned by the current user so tar/rm work without sudo rm/chown.
copy_file_to_staging_as_current_user() {
  source_file=$1
  dest_file=$2
  use_sudo=${3:-false}

  if [ ! -e "$source_file" ]
  then
    return 1
  fi
  mkdir -p "$(dirname "$dest_file")"
  if [ "$use_sudo" = "true" ] && command -v sudo >/dev/null 2>&1
  then
    local err
    if ! err=$(sudo cp -L "$source_file" /dev/stdout 2>&1 > "$dest_file")
    then
      rm -f -- "$dest_file"
      echo "COPY_FAILURE=${source_file}: ${err//$'\n'/; }"
      return 1
    fi
  else
    copy_or_report -Lp "$source_file" "$dest_file"
  fi
}

# Always tar as the current user so bundle artifacts can be removed without sudo rm.
create_bundle_tar_as_current_user() {
  remote_tar_path=$1
  staging_dir=$2

  mkdir -p "$(dirname "$remote_tar_path")"
  tar --warning=no-file-changed -czf "$remote_tar_path" -C "$staging_dir" .
}

# Tars the staging dir, or reports FAILED and stops the entrypoint. Every entrypoint wants this:
# YBA only checks that the tar exists, so a truncated one is copied and shipped as a successful
# component. Treating any non-zero status as fatal is safe only because every entrypoint tars a
# staging directory of cp'd copies -- GNU tar still exits 1 for "file changed as we read it" even
# with --warning=no-file-changed, so tarring a live tree here would fail spuriously.
# A partial tar is removed: on a full disk it holds the space that ran out.
create_bundle_tar_or_fail() {
  remote_tar_path=$1
  staging_dir=$2

  if ! create_bundle_tar_as_current_user "$remote_tar_path" "$staging_dir"
  then
    rm -f "$remote_tar_path"
    echo "Failed to create bundle tar at $remote_tar_path"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi
}

# Collect universe logs fully on the node, including date/regex filtering and packaging.
# This includes:
# - date/regex filtering for other master/tserver logs (with overlap behavior)
# - postgresql-* logs under tserver/logs filtered by embedded filename timestamp
# - date/regex filtering with overlap for *yugabyte.log* under master/logs and tserver/logs
# - initdb logs
# - pg_upgrade logs
# - YNP app.log as ynp/app.log
# - optional pg audit filtering for postgresql-* logs in tserver/logs
create_universe_logs_bundle() {
  mount_path=""
  node_home_dir=""
  master_logs_path=""
  tserver_logs_path=""
  start_date=""
  end_date=""
  ynp_log_path=""
  remote_tar_path=""
  filter_pgaudit_logs="false"
  regex_patterns=()

  while [ "$#" -gt 0 ]
  do
    case "$1" in
      --mount_path)
        mount_path=$2
        shift 2
        ;;
      --node_home_dir)
        node_home_dir=$2
        shift 2
        ;;
      --master_logs_path)
        master_logs_path=$2
        shift 2
        ;;
      --tserver_logs_path)
        tserver_logs_path=$2
        shift 2
        ;;
      --start_date)
        start_date=$2
        shift 2
        ;;
      --end_date)
        end_date=$2
        shift 2
        ;;
      --ynp_log_path)
        ynp_log_path=$2
        shift 2
        ;;
      --remote_tar_path)
        remote_tar_path=$2
        shift 2
        ;;
      --filter_pgaudit_logs)
        filter_pgaudit_logs=$2
        shift 2
        ;;
      --regex)
        regex_patterns+=("$2")
        shift 2
        ;;
      --regex-b64)
        decoded_regex=$(decode_base64_string "$2")
        if [ $? -eq 0 ]
        then
          regex_patterns+=("$decoded_regex")
        fi
        shift 2
        ;;
      *)
        echo "Unknown argument: $1"
        echo "BUNDLE_STATUS=FAILED"
        exit 1
        ;;
    esac
  done

  if [ -z "$node_home_dir" ] || [ -z "$start_date" ] \
    || [ -z "$end_date" ] || [ -z "$remote_tar_path" ]
  then
    echo "Missing required args for create_universe_logs_bundle"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  prefer_yb_log_parse_bin

  if [ -z "$master_logs_path" ]
  then
    master_logs_path="$node_home_dir/master/logs"
  fi
  if [ -z "$tserver_logs_path" ]
  then
    tserver_logs_path="$node_home_dir/tserver/logs"
  fi

  tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/universe-logs.XXXXXX")"
  staging_dir="$tmp_root/staging"
  mkdir -p "$staging_dir"

  cleanup_cmd() {
    rm -rf "$tmp_root"
  }
  trap cleanup_cmd EXIT

  start_date_sortable=$(normalize_date_to_sortable "$start_date")
  if [ $? -ne 0 ] || [ -z "$start_date_sortable" ]
  then
    echo "Invalid start_date format: $start_date"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi
  end_date_sortable=$(normalize_date_to_sortable "$end_date")
  if [ $? -ne 0 ] || [ -z "$end_date_sortable" ]
  then
    echo "Invalid end_date format: $end_date"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi
  echo "Universe logs collection window (sortable): ${start_date_sortable} to ${end_date_sortable}"
  declare -A selected_files_map=()

  select_universe_log_file() {
    source_file=$1
    staging_rel=$2
    selected_files_map["$source_file"]="$staging_rel"
  }

  resolve_listed_file_path() {
    source_dir=$1
    file_path=$2
    if [[ "$file_path" != /* ]]
    then
      echo "$source_dir/$file_path"
    else
      echo "$file_path"
    fi
  }

  # Match v1 SupportBundleUtil.filterFilePathsBetweenDatesByPatterns overlap behavior:
  # for each file type, include logs strictly in [startDate, endDate] plus the newest log
  # before startDate (sorted newest-first per type).
  apply_v1_overlap_date_filter_from_candidates() {
    candidates_file=$1
    if [ ! -s "$candidates_file" ]
    then
      return 0
    fi

    local tmp_by_type="$tmp_root/by-type.$RANDOM.txt"
    cut -d'|' -f1 "$candidates_file" | sort -u > "$tmp_by_type"

    while IFS= read -r current_type
    do
      while IFS='|' read -r file_type file_timestamp_sortable source_file staging_rel
      do
        if [ "$file_timestamp_sortable" -ge "$start_date_sortable" ] \
          && [ "$file_timestamp_sortable" -le "$end_date_sortable" ]
        then
          select_universe_log_file "$source_file" "$staging_rel"
        elif [ "$file_timestamp_sortable" -lt "$start_date_sortable" ]
        then
          select_universe_log_file "$source_file" "$staging_rel"
          break
        fi
      done < <(
        awk -F'|' -v current_type="$current_type" '$1 == current_type {print $0}' \
          "$candidates_file" | sort -t'|' -k2,2r
      )
    done < "$tmp_by_type"
  }

  is_yugabyte_log_file() {
    file_name=$1
    [[ "$file_name" == *yugabyte.log* ]]
  }

  is_handled_outside_date_filtered_collection() {
    file_name=$1
    log_role=$2
    if is_yugabyte_log_file "$file_name"
    then
      return 0
    fi
    if [ "$log_role" = "tserver" ] && [[ "$file_name" == postgresql-* ]]
    then
      return 0
    fi
    return 1
  }

  collect_postgres_logs_in_dir() {
    source_dir=$1
    log_role=$2
    if [ ! -d "$source_dir" ]
    then
      return 0
    fi

    local tmp_list="$tmp_root/list.postgres.$RANDOM.txt"
    local tmp_candidates="$tmp_root/pg-candidates.$RANDOM.txt"
    local selected_count=0
    : > "$tmp_list"
    : > "$tmp_candidates"
    get_paths_and_sizes "$source_dir" 1 f "$tmp_list"

    while IFS= read -r output_line
    do
      if [ -z "$output_line" ]
      then
        continue
      fi
      file_path="${output_line#* }"
      if [ -z "$file_path" ]
      then
        continue
      fi
      source_file=$(resolve_listed_file_path "$source_dir" "$file_path")
      if [ ! -e "$source_file" ] && [ ! -L "$source_file" ]
      then
        continue
      fi

      file_name="$(basename "$source_file")"
      if [[ "$file_name" != postgresql-* ]]
      then
        continue
      fi
      parse_output=$(parse_file_type_and_date_from_name "$file_name")
      if [ $? -ne 0 ] || [ -z "$parse_output" ]
      then
        continue
      fi

      file_type="${parse_output%%|*}"
      file_timestamp_sortable="${parse_output##*|}"
      staging_rel="${log_role}/logs/${file_name}"
      echo "${file_type}|${file_timestamp_sortable}|${source_file}|${staging_rel}" \
        >> "$tmp_candidates"
    done < "$tmp_list"

    if [ ! -s "$tmp_candidates" ]
    then
      echo "Selected 0 postgresql log file(s) from ${source_dir} for collection window" \
        "${start_date_sortable} to ${end_date_sortable}"
      return 0
    fi

    apply_v1_overlap_date_filter_from_candidates "$tmp_candidates"

    selected_count=0
    while IFS='|' read -r _ _ source_file _
    do
      if [ -n "${selected_files_map[$source_file]:-}" ]
      then
        selected_count=$((selected_count + 1))
      fi
    done < "$tmp_candidates"

    echo "Selected ${selected_count} postgresql log file(s) from ${source_dir}" \
      "for collection window ${start_date_sortable} to ${end_date_sortable}"
  }

  # Date-filter rotated *yugabyte.log* files (glog INFO/ERROR/FATAL and yb-master*/yb-tserver*
  # patterns) using the same v1 overlap behavior as postgresql logs.
  collect_date_filtered_yugabyte_logs_in_dir() {
    source_dir=$1
    log_role=$2
    if [ ! -d "$source_dir" ]
    then
      return 0
    fi

    local tmp_list="$tmp_root/list.yugabyte.$RANDOM.txt"
    local tmp_candidates="$tmp_root/yb-candidates.$RANDOM.txt"
    local selected_count=0
    : > "$tmp_list"
    : > "$tmp_candidates"
    get_paths_and_sizes "$source_dir" 1 f "$tmp_list"

    while IFS= read -r output_line
    do
      if [ -z "$output_line" ]
      then
        continue
      fi
      file_path="${output_line#* }"
      if [ -z "$file_path" ]
      then
        continue
      fi
      source_file=$(resolve_listed_file_path "$source_dir" "$file_path")
      if [ ! -e "$source_file" ] && [ ! -L "$source_file" ]
      then
        continue
      fi

      file_name="$(basename "$source_file")"
      if ! is_yugabyte_log_file "$file_name"
      then
        continue
      fi

      parse_output=""
      parse_status=1
      if [ ${#regex_patterns[@]} -gt 0 ]
      then
        parse_output=$(parse_file_type_and_date_from_regexes "$file_name" "${regex_patterns[@]}")
        parse_status=$?
      fi
      if [ "$parse_status" -ne 0 ] || [ -z "$parse_output" ]
      then
        parse_output=$(parse_file_type_and_date_from_name "$file_name")
        parse_status=$?
      fi
      if [ "$parse_status" -ne 0 ] || [ -z "$parse_output" ]
      then
        continue
      fi

      file_type="${parse_output%%|*}"
      file_timestamp_sortable="${parse_output##*|}"
      staging_rel="${log_role}/logs/${file_name}"
      echo "${file_type}|${file_timestamp_sortable}|${source_file}|${staging_rel}" \
        >> "$tmp_candidates"
    done < "$tmp_list"

    if [ ! -s "$tmp_candidates" ]
    then
      echo "Selected 0 yugabyte.log file(s) from ${source_dir} for collection window" \
        "${start_date_sortable} to ${end_date_sortable}"
      return 0
    fi

    apply_v1_overlap_date_filter_from_candidates "$tmp_candidates"

    selected_count=0
    while IFS='|' read -r _ _ source_file _
    do
      if [ -n "${selected_files_map[$source_file]:-}" ]
      then
        selected_count=$((selected_count + 1))
      fi
    done < "$tmp_candidates"

    echo "Selected ${selected_count} yugabyte.log file(s) from ${source_dir}" \
      "for collection window ${start_date_sortable} to ${end_date_sortable}"
  }

  collect_files_from_log_dir() {
    source_dir=$1
    log_role=$2
    skip_handled_logs=${3:-false}
    if [ ! -d "$source_dir" ]
    then
      return 0
    fi

    local tmp_candidates="$tmp_root/candidates.$RANDOM.txt"
    local tmp_list="$tmp_root/list.$RANDOM.txt"
    : > "$tmp_candidates"
    : > "$tmp_list"

    get_paths_and_sizes "$source_dir" 1 f "$tmp_list"

    while IFS= read -r output_line
    do
      if [ -z "$output_line" ]
      then
        continue
      fi
      file_path="${output_line#* }"
      if [ -z "$file_path" ]
      then
        continue
      fi
      if [[ "$file_path" != /* ]]
      then
        source_file="$source_dir/$file_path"
      else
        source_file="$file_path"
      fi
      if [ ! -e "$source_file" ] && [ ! -L "$source_file" ]
      then
        continue
      fi

      file_name="$(basename "$source_file")"
      if [ "$skip_handled_logs" = "true" ] \
        && is_handled_outside_date_filtered_collection "$file_name" "$log_role"
      then
        continue
      fi
      parse_output=""
      parse_status=1
      if [ ${#regex_patterns[@]} -gt 0 ]
      then
        parse_output=$(parse_file_type_and_date_from_regexes "$file_name" "${regex_patterns[@]}")
        parse_status=$?
      fi
      if [ "$parse_status" -ne 0 ] || [ -z "$parse_output" ]
      then
        parse_output=$(parse_file_type_and_date_from_name "$file_name")
        parse_status=$?
      fi
      if [ "$parse_status" -ne 0 ] || [ -z "$parse_output" ]
      then
        continue
      fi

      file_type="${parse_output%%|*}"
      file_timestamp_sortable="${parse_output##*|}"
      staging_rel="${log_role}/logs/${file_name}"
      echo "${file_type}|${file_timestamp_sortable}|${source_file}|${staging_rel}" \
        >> "$tmp_candidates"
    done < "$tmp_list"

    if [ ! -s "$tmp_candidates" ]
    then
      return 0
    fi

    apply_v1_overlap_date_filter_from_candidates "$tmp_candidates"
  }

  collect_files_from_log_dir "$master_logs_path" "master" true
  collect_files_from_log_dir "$tserver_logs_path" "tserver" true
  collect_date_filtered_yugabyte_logs_in_dir "$master_logs_path" "master"
  collect_date_filtered_yugabyte_logs_in_dir "$tserver_logs_path" "tserver"
  collect_postgres_logs_in_dir "$tserver_logs_path" "tserver"

  add_initdb_log_if_present() {
    logs_path=$1
    default_path=$2
    log_role=$3
    for initdb_candidate in "$logs_path/initdb.log" "$default_path/initdb.log"
    do
      if [ -f "$initdb_candidate" ] || [ -L "$initdb_candidate" ]
      then
        select_universe_log_file "$initdb_candidate" "${log_role}/logs/initdb.log"
        return 0
      fi
    done
    return 1
  }

  add_initdb_log_if_present "$master_logs_path" "$node_home_dir/master/logs" "master"
  add_initdb_log_if_present "$tserver_logs_path" "$node_home_dir/tserver/logs" "tserver"

  # The master writes pg_upgrade logs only directly under the first data dir, as v1 assumes; a glob
  # keeps the scan out of yb-data's SST and WAL trees.
  if [ -n "$mount_path" ] && [ -d "$mount_path" ]
  then
    for pg_upgrade_log_file in \
      "$mount_path"/pg_upgrade_data*/pg_upgrade_output.d/*/log/*
    do
      if [ -f "$pg_upgrade_log_file" ] && [ ! -L "$pg_upgrade_log_file" ]
      then
        relative_path="${pg_upgrade_log_file#"$mount_path/"}"
        select_universe_log_file "$pg_upgrade_log_file" "$relative_path"
      fi
    done
  fi

  if [ -n "$ynp_log_path" ] && { [ -f "$ynp_log_path" ] || [ -L "$ynp_log_path" ]; }
  then
    select_universe_log_file "$ynp_log_path" "ynp/app.log"
  fi

  if [ "${SUPPORT_BUNDLE_ESTIMATE_ONLY:-}" = "1" ]
  then
    total=0
    for source_file in "${!selected_files_map[@]}"
    do
      total=$((total + $(_byte_size_for_path "$source_file")))
    done
    _emit_estimate_result "$total"
    exit 0
  fi

  for source_file in "${!selected_files_map[@]}"
  do
    staging_rel="${selected_files_map[$source_file]}"
    if [ -z "$staging_rel" ]
    then
      fallback_name="$(basename "$source_file")"
      staging_rel="misc/$fallback_name"
    fi
    add_file_to_staging "$source_file" "$staging_dir" "$staging_rel" "$filter_pgaudit_logs"
  done

  if [ -z "$(find "$staging_dir" -type f -print -quit)" ]
  then
    echo "No files found for universe logs bundle."
    echo "BUNDLE_STATUS=EMPTY"
    exit 0
  fi

  create_bundle_tar_or_fail "$remote_tar_path" "$staging_dir"
  echo "BUNDLE_STATUS=OK"
  echo "BUNDLE_PATH=$remote_tar_path"
  echo "Created universe logs bundle at $remote_tar_path"
}

compact_date_add_days() {
  compact_date=$1
  delta_days=$2
  if [ -z "$compact_date" ] || [ -z "$delta_days" ]
  then
    return 1
  fi

  if [ -z "${YB_LOG_PARSE_SKIP:-}" ]
  then
    yb_log_parse_bin="$(resolve_yb_log_parse_bin 2>/dev/null)"
    if [ -n "$yb_log_parse_bin" ]
    then
      add_days_output=$("$yb_log_parse_bin" add-days "$compact_date" "$delta_days" 2>/dev/null)
      if [ $? -eq 0 ] && [ -n "$add_days_output" ]
      then
        echo "$add_days_output"
        return 0
      fi
    fi
  fi

  if date --version >/dev/null 2>&1
  then
    gnu_output=$(date -d "${compact_date} ${delta_days} days" +"%Y%m%d" 2>/dev/null)
    if [ $? -eq 0 ] && [ -n "$gnu_output" ]
    then
      echo "$gnu_output"
      return 0
    fi
  fi

  python_bin="python3"
  if ! command -v "$python_bin" >/dev/null 2>&1
  then
    python_bin="python"
  fi
  if ! command -v "$python_bin" >/dev/null 2>&1
  then
    return 1
  fi

  "$python_bin" - "$compact_date" "$delta_days" <<'PY'
import datetime as dt
import sys

base = sys.argv[1].strip()
delta = int(sys.argv[2])
value = dt.datetime.strptime(base, "%Y%m%d").date() + dt.timedelta(days=delta)
print(value.strftime("%Y%m%d"))
PY
}

# Lists files with mtime, size and path, tab separated. get_paths_and_sizes above reports only size
# and path, and mtime is the whole basis on which a rotation's coverage is worked out.
#
# -printf is GNU find. v1 SystemLogsComponent.listVarLogFiles() already ships the identical command
# to every DB node, so GNU findutils is an existing requirement of this feature, not a new one.
#
# -L is the one addition over v1: /var/log/messages is a symlink on some images and a bare -type f
# drops it. Under -L the symlink is typed by its target and %s/%T@ report the target's stat, while
# %p keeps the symlink's own path, so the tar still contains var/log/messages.
get_paths_sizes_and_mtimes() {
  remote_dir_path=$1
  max_depth=$2
  temp_file_path=$3

  : > "$temp_file_path"
  find -L "$remote_dir_path" -maxdepth "$max_depth" -type f \
    -printf '%T@\t%s\t%p\n' 2>/dev/null >> "$temp_file_path"
}

# Emits "<base>\t<mtime>\t<size>\t<path>" for each listed file whose full path matches a pattern,
# where <base> is the pattern's group 1: the live log's file name. Group 1 is what groups a log's
# rotations together, which is why a pattern that captures nothing is refused rather than defaulted.
#
# The patterns arrive verbatim from yb.support_bundle.system_logs_regex_pattern, in Java's flavour.
# grep -E, awk and bash's =~ are all POSIX ERE and reject both (?:...) and \d, and rewriting (?: as
# ( would renumber the groups and quietly redefine group 1 as the directory prefix. Python's re is
# syntax compatible with java.util.regex for everything these patterns use, and one process handles
# the whole listing.
_system_logs_match_listing() {
  listing_file=$1
  shift

  python_bin="python3"
  if ! command -v "$python_bin" >/dev/null 2>&1
  then
    python_bin="python"
  fi
  if ! command -v "$python_bin" >/dev/null 2>&1
  then
    return 1
  fi

  "$python_bin" - "$listing_file" "$@" <<'PY'
import re
import sys

listing_path = sys.argv[1]
patterns = sys.argv[2:]

compiled = []
for pattern in patterns:
    try:
        # Wrapping in a non-capturing group and anchoring with \Z is Matcher.matches() on both
        # python 2 and 3, and adds no group, so group 1 stays the operator's.
        regex = re.compile("(?:" + pattern + r")\Z")
    except Exception as exc:
        sys.stderr.write("Ignoring unusable system logs pattern %r: %s\n" % (pattern, exc))
        continue
    if regex.groups < 1:
        sys.stderr.write(
            "Ignoring system logs pattern %r: it captures no group for the base log name\n"
            % pattern)
        continue
    compiled.append(regex)

if not compiled:
    sys.exit(2)

listing = open(listing_path)
try:
    for line in listing:
        fields = line.rstrip("\n").split("\t", 2)
        if len(fields) < 3:
            continue
        mtime, size, path = fields
        for regex in compiled:
            matched = regex.match(path)
            if matched is None:
                continue
            base = matched.group(1)
            if base:
                sys.stdout.write("%s\t%s\t%s\t%s\n" % (base, mtime, size, path))
            break
finally:
    listing.close()
PY
}

# Last resort for a node with no python at all: the shipped default pattern, hand translated to the
# POSIX ERE bash understands. Deliberately not a general translator - a custom pattern degrades to
# "no /var/log files", warned about by the caller, rather than to a wrong one.
_system_logs_match_listing_default() {
  listing_file=$1
  while IFS="$(printf '\t')" read -r mtime size path
  do
    if [ -z "$path" ]
    then
      continue
    fi
    file_name="${path##*/}"
    if [[ "$file_name" =~ ^(messages|syslog)([.-][0-9]{1,8})?(\.gz)?$ ]]
    then
      printf '%s\t%s\t%s\t%s\n' "${BASH_REMATCH[1]}" "$mtime" "$size" "$path"
    fi
  done < "$listing_file"
}

# Ports SystemLogsComponent.coveringFiles(). Within one log: the live file covers from the newest
# rotation's mtime until now, rotation i covers from rotation i+1's mtime to its own, and the oldest
# rotation is open below. logrotate renames rather than rewrites, and both rename and gzip preserve
# mtime, so a rotated file's mtime is the moment it was rotated: the end of the period it holds.
# Nothing is inferred from the file's name - rotation cadence is local configuration YBA cannot see,
# and one node's /var/log mixes RHEL's weekly dateext with Debian's daily numeric naming.
#
# A file is kept when the period it covers overlaps the window at all, inclusive at both edges, so
# the file straddling the window's start is selected outright rather than by keeping one file past
# the edge the way the name-dated components have to.
#
# The live file is identified by name, not by being the newest: v1 partitions on
# baseName.equals(fileName) before sorting, and so does this.
_system_logs_select_covering() {
  matched_file=$1
  window_start=$2
  window_end=$3
  now_epoch=$4

  LC_ALL=C sort -t"$(printf '\t')" -k1,1 -k2,2nr "$matched_file" \
  | awk -F'\t' -v win_start="$window_start" -v win_end="$window_end" -v now="$now_epoch" '
      function keep(idx, covers_from, covers_to) {
        if (covers_from <= win_end && covers_to >= win_start) {
          printf "%s\t%s\n", g_size[idx], g_path[idx]
        }
      }
      function flush(   i, name, live, rots, covers_from) {
        if (n == 0) {
          return
        }
        live = 0
        rots = 0
        for (i = 1; i <= n; i++) {
          name = g_path[i]
          sub(/^.*\//, "", name)
          if (name == g_base && live == 0) {
            live = i
          } else {
            rots++
            rot[rots] = i
          }
        }
        # Open ended below: epoch 0 stands in for v1s Long.MIN_VALUE, which is indistinguishable
        # for any window a support bundle can ask for.
        for (i = 1; i <= rots; i++) {
          covers_from = (i < rots) ? g_mtime[rot[i + 1]] : 0
          keep(rot[i], covers_from, g_mtime[rot[i]])
        }
        if (live > 0) {
          covers_from = (rots > 0) ? g_mtime[rot[1]] : 0
          keep(live, covers_from, now)
        }
        n = 0
      }
      {
        path = $4
        for (i = 5; i <= NF; i++) {
          path = path "\t" $i
        }
        if ($1 != g_base) {
          flush()
          g_base = $1
        }
        n++
        g_mtime[n] = int($2)
        g_size[n]  = $3
        g_path[n]  = path
      }
      END { flush() }'
}

# Older builders send only --start_date/--end_date at day granularity. Interpreted at UTC, with the
# end date taken as the end of its day: truncating it to midnight drops a rotation that closed later
# that same day, which v1 collects because its window carries a time.
_system_logs_epoch_from_date() {
  raw=$1
  edge=$2
  if date --version >/dev/null 2>&1
  then
    # The date-only form is matched first, before the general parse, because GNU date accepts a bare
    # YYYY-MM-DD and resolves it to midnight. Letting it fall through meant the general parse always
    # won, the edge argument was never consulted, and an end bound lost the 86399 seconds the
    # comment above promises to keep.
    if [[ "$raw" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]]
    then
      if [ "$edge" = "end" ]
      then
        date -u -d "$raw 23:59:59" +%s 2>/dev/null && return 0
      else
        date -u -d "$raw 00:00:00" +%s 2>/dev/null && return 0
      fi
      return 1
    fi
    epoch=$(date -u -d "$raw" +%s 2>/dev/null)
    if [ -n "$epoch" ]
    then
      printf '%s' "$epoch"
      return 0
    fi
    # A value carrying a time that date still could not parse: fall back to its day part.
    day="${raw%%[T ]*}"
    if [ "$edge" = "end" ]
    then
      date -u -d "$day 23:59:59" +%s 2>/dev/null && return 0
    else
      date -u -d "$day 00:00:00" +%s 2>/dev/null && return 0
    fi
  fi
  return 1
}

SYSTEM_LOGS_JOURNAL_ARCHIVE_NAME="journal.log.gz"
SYSTEM_LOGS_JOURNALD_TIMEOUT_SECS=180

# Same three tokens as v1 resolveJournalAccess(). Probing rather than putting `sudo -n` straight in
# the dump is what makes the unprivileged read possible: on a node without passwordless sudo the
# baked-in form yields no journal at all, and on images that ship no rsyslog the journal is the only
# OS evidence there is.
_system_logs_journal_access() {
  use_sudo_flag=$1
  if ! command -v journalctl >/dev/null 2>&1
  then
    echo "JOURNALCTL_MISSING"
  elif [ "$use_sudo_flag" = "true" ] && sudo -n true >/dev/null 2>&1
  then
    echo "SUDO_OK"
  else
    echo "SUDO_DENIED"
  fi
}

# Builds the argv as an array so a sudo path or a timeout binary containing spaces cannot resplit.
# Always non-empty, so this is safe under `set -u` on bash 4.2, where "${empty[@]}" errors.
_system_logs_journalctl_argv() {
  access=$1
  timeout_secs=$2
  journal_argv=()
  if [ -n "$timeout_secs" ] && command -v timeout >/dev/null 2>&1
  then
    journal_argv+=(timeout "$timeout_secs")
  fi
  if [ "$access" = "SUDO_OK" ]
  then
    journal_argv+=(sudo -n)
  fi
  journal_argv+=(journalctl)
}

# Epoch seconds rather than dates: journalctl parses --since/--until in the node's local time zone
# and --utc only changes the output format, so anything else skews the window silently.
#
# pipefail is what makes a half finished dump visible - the pipeline would otherwise report gzip's
# status, and a truncated archive is indistinguishable from a complete one. It is set inside a
# subshell so the caller's shell options are untouched.
#
# timeout bounds journalctl rather than the pipeline: gzip then still drains and closes the archive,
# and pipefail surfaces the 124 instead of the collection appearing to have succeeded.
_system_logs_collect_journal() {
  access=$1
  window_start=$2
  window_end=$3
  dest_file=$4

  _system_logs_journalctl_argv "$access" "$SYSTEM_LOGS_JOURNALD_TIMEOUT_SECS"
  mkdir -p "$(dirname "$dest_file")"
  (
    set -o pipefail
    "${journal_argv[@]}" --since "@$window_start" --until "@$window_end" --no-pager -o short-iso \
      | gzip -c > "$dest_file"
  )
  dump_status=$?
  if [ "$dump_status" -ne 0 ]
  then
    # A dump killed by the timeout, a journal the login user cannot read and a corrupt journal file
    # all land here, and only the node knows which, so say what the exit status was.
    echo "Warning: could not collect the systemd journal (exit $dump_status). The bundle will" \
         "carry no journal for this node." >&2
    rm -f "$dest_file"
    return 1
  fi
  [ -f "$dest_file" ]
}

# Parses "Archived and active journals take up 1.5G in the file system."
_system_logs_journal_disk_usage_bytes() {
  _system_logs_journalctl_argv "$1" 30
  usage_line=$("${journal_argv[@]}" --disk-usage --no-pager 2>/dev/null)
  parsed=$(printf '%s' "$usage_line" \
    | sed -nE 's/.*take up[[:space:]]+([0-9]+(\.[0-9]+)?)[[:space:]]*([KMGTP])?.*/\1 \3/p')
  if [ -z "$parsed" ]
  then
    echo "Warning: could not parse 'journalctl --disk-usage' output: $usage_line" >&2
    printf '0'
    return 0
  fi
  set -- $parsed
  awk -v value="$1" -v unit="${2:-}" 'BEGIN {
    units = "KMGTP"
    exponent = (unit == "") ? 0 : index(units, unit)
    printf "%d", value * (1024 ^ exponent)
  }'
}

# The epoch of the oldest entry the journal still holds, or 0 if it cannot be read. head closes the
# pipe after the first line, so this does not read the whole journal. pipefail is deliberately off:
# journalctl is then killed by SIGPIPE, and pipefail would report a line that was in fact received
# as a failure. -o short-unix prints the epoch directly, so unlike v1 this needs no date parsing.
_system_logs_journal_oldest_epoch() {
  _system_logs_journalctl_argv "$1" 30
  first_line=$("${journal_argv[@]}" --since @0 --no-pager -o short-unix -q 2>/dev/null | head -1)
  epoch=${first_line%% *}
  epoch=${epoch%%.*}
  case "$epoch" in
    ''|*[!0-9]*) printf '0' ;;
    *)           printf '%s' "$epoch" ;;
  esac
}

# Prorates the journal's on disk size by the share of its history the window covers, the same way v1
# getJournaldSizeEstimate() does. There is no cheap exact size for a time window, and measuring one
# would cost as much as collecting it, so this is deliberately rough: it assumes a uniform log rate
# and treats the binary on disk size as a stand in for gzipped text, which errs high. Reporting zero
# would under report the bundle in the UI, which is worse than erring high.
_system_logs_journal_size_estimate() {
  access=$1
  window_start=$2
  window_end=$3

  usage_bytes=$(_system_logs_journal_disk_usage_bytes "$access")
  window_secs=$((window_end - window_start))
  if [ -z "$usage_bytes" ] || [ "$usage_bytes" -le 0 ] || [ "$window_secs" -le 0 ]
  then
    printf '0'
    return 0
  fi

  oldest_epoch=$(_system_logs_journal_oldest_epoch "$access")
  now_epoch=$(date +%s)
  # Unknown history, so report the whole journal rather than under reporting it.
  if [ "$oldest_epoch" -le 0 ] || [ "$now_epoch" -le "$oldest_epoch" ]
  then
    printf '%s' "$usage_bytes"
    return 0
  fi
  span_secs=$((now_epoch - oldest_epoch))
  if [ "$window_secs" -ge "$span_secs" ]
  then
    printf '%s' "$usage_bytes"
    return 0
  fi
  awk -v usage="$usage_bytes" -v w="$window_secs" -v s="$span_secs" \
    'BEGIN { printf "%d", usage * w / s }'
}

# Collects the OS level system logs: the rsyslog catch-all files under --log_dir and, unless turned
# off, the systemd journal for the same window.
#
# Selection is driven by --regex-b64 (group 1 = the live log's file name) and file mtimes, both
# injected by SupportBundleScriptParamResolver from yb.support_bundle.system_logs_regex_pattern and
# yb.support_bundle.collect_journald_logs, so v1 and v2 answer to the same universe scoped settings.
#
# This never reports FAILED for want of a readable file: v1's downloadComponentBetweenDates swallows
# every collection failure and only warns, and a node whose /var/log cannot be read is exactly the
# node where the journal is the only OS evidence left. FAILED is reserved for a tar that could not
# be written.
create_system_logs_bundle() {
  log_dir="/var/log"
  start_date=""
  end_date=""
  start_epoch=""
  end_epoch=""
  remote_tar_path=""
  use_sudo="true"
  collect_journald="true"
  regex_patterns=()

  while [ "$#" -gt 0 ]
  do
    case "$1" in
      --log_dir)
        log_dir=$2
        shift 2
        ;;
      --start_date)
        start_date=$2
        shift 2
        ;;
      --end_date)
        end_date=$2
        shift 2
        ;;
      --start_epoch)
        start_epoch=$2
        shift 2
        ;;
      --end_epoch)
        end_epoch=$2
        shift 2
        ;;
      --remote_tar_path)
        remote_tar_path=$2
        shift 2
        ;;
      --regex)
        regex_patterns+=("$2")
        shift 2
        ;;
      --regex-b64)
        regex_patterns+=("$(decode_base64_string "$2")")
        shift 2
        ;;
      --collect_journald)
        collect_journald=$2
        shift 2
        ;;
      --use_sudo)
        use_sudo=$2
        shift 2
        ;;
      # Accepted and ignored. Selection is regex and mtime driven now; a yba-cli or a manifest from
      # an older build still sends these, and the *) arm below is fatal, so removing these arms
      # would turn a version skew into a failed component rather than an ignored flag.
      --messages_prefix|--rotate_window_days)
        shift 2
        ;;
      *)
        echo "Unknown argument: $1"
        echo "BUNDLE_STATUS=FAILED"
        exit 1
        ;;
    esac
  done

  if [ -z "$remote_tar_path" ]
  then
    echo "Missing required args for create_system_logs_bundle"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  if [ ${#regex_patterns[@]} -eq 0 ]
  then
    regex_patterns=('(?:.*/)?(messages|syslog)(?:[.-]\d{1,8})?(?:\.gz)?')
  fi

  if [ -z "$start_epoch" ] && [ -n "$start_date" ]
  then
    start_epoch=$(_system_logs_epoch_from_date "$start_date" "start")
  fi
  if [ -z "$end_epoch" ] && [ -n "$end_date" ]
  then
    end_epoch=$(_system_logs_epoch_from_date "$end_date" "end")
  fi
  if [ -z "$start_epoch" ] || [ -z "$end_epoch" ]
  then
    echo "Missing or unparseable collection window for create_system_logs_bundle"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi
  now_epoch=$(date +%s)

  tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/system-logs.XXXXXX")"
  cleanup_cmd() {
    rm -rf "$tmp_root"
  }
  trap cleanup_cmd EXIT
  staging_dir="$tmp_root/staging"
  mkdir -p "$staging_dir"

  selected_files=()
  selected_sizes=()
  listed_count=0

  if [ ! -d "$log_dir" ]
  then
    echo "Log directory does not exist: $log_dir. The systemd journal is unaffected."
  else
    list_file="$tmp_root/system-logs-list.txt"
    get_paths_sizes_and_mtimes "$log_dir" 1 "$list_file"
    listed_count=$(wc -l < "$list_file" | tr -d ' ')

    matched_file="$tmp_root/system-logs-matched.txt"
    : > "$matched_file"
    if _system_logs_match_listing "$list_file" "${regex_patterns[@]}" > "$matched_file"
    then
      :
    else
      match_status=$?
      if [ "$match_status" -eq 2 ]
      then
        echo "Warning: no usable system logs pattern, so no file in '$log_dir' will be" \
             "collected. The systemd journal is unaffected." >&2
      else
        echo "python is unavailable; falling back to the default system logs pattern." >&2
        _system_logs_match_listing_default "$list_file" > "$matched_file"
      fi
    fi

    selected_file="$tmp_root/system-logs-selected.txt"
    _system_logs_select_covering "$matched_file" "$start_epoch" "$end_epoch" "$now_epoch" \
      > "$selected_file"

    while IFS="$(printf '\t')" read -r sel_size sel_path
    do
      if [ -z "$sel_path" ]
      then
        continue
      fi
      selected_sizes+=("$sel_size")
      selected_files+=("$sel_path")
    done < "$selected_file"

    if [ ${#selected_files[@]} -eq 0 ]
    then
      # Name the pattern and the listing size: that is what separates "no logs on this node" from
      # "the pattern does not cover this distro's rsyslog naming".
      echo "Warning: no system log file in '$log_dir' matched for the window $start_epoch to" \
           "$end_epoch. $listed_count file(s) were listed. On distros whose rsyslog naming is not" \
           "covered by the pattern this silently yields a bundle with no OS logs." >&2
    fi
  fi

  if [ "${SUPPORT_BUNDLE_ESTIMATE_ONLY:-}" = "1" ]
  then
    total=0
    for sel_size in "${selected_sizes[@]}"
    do
      case "$sel_size" in
        ''|*[!0-9]*) ;;
        *) total=$((total + sel_size)) ;;
      esac
    done
    # The journal is not a file on the node, so it is counted here for the estimate only; it must
    # never reach the copy loop, which stages its input paths verbatim.
    if [ "$collect_journald" = "true" ]
    then
      journal_access=$(_system_logs_journal_access "$use_sudo")
      if [ "$journal_access" != "JOURNALCTL_MISSING" ]
      then
        journal_estimate=$(_system_logs_journal_size_estimate \
          "$journal_access" "$start_epoch" "$end_epoch")
        total=$((total + journal_estimate))
      fi
    fi
    _emit_estimate_result "$total"
    exit 0
  fi

  if [ "$use_sudo" = "true" ] && command -v sudo >/dev/null 2>&1
  then
    # -n rather than -v: -v can block on a password prompt, and v1 never fails this component for
    # want of sudo. Without it the readable files and the journal are still worth collecting.
    if ! sudo -n true >/dev/null 2>&1
    then
      echo "Warning: no passwordless sudo, so system logs are read unprivileged and files the" \
           "login user cannot read will be missing." >&2
      use_sudo="false"
    fi
  fi

  copied=0
  for source_file in "${selected_files[@]}"
  do
    if [ ! -e "$source_file" ]
    then
      continue
    fi
    # Match v1 downloadRootFiles(): sudo tar on absolute paths strips the leading '/' and
    # produces var/log/messages in the archive, not a flat messages file at the tar root.
    relative_path="${source_file#/}"
    dest_file="$staging_dir/$relative_path"
    if copy_file_to_staging_as_current_user "$source_file" "$dest_file" "$use_sudo"
    then
      copied=1
    fi
  done

  # Staged flat at the tar root, next to the var/log tree, so the extracted layout matches what v1
  # produces: journal.log.gz beside var/log/... in the node's bundle directory.
  journal_collected=0
  if [ "$collect_journald" = "true" ]
  then
    journal_access=$(_system_logs_journal_access "$use_sudo")
    case "$journal_access" in
      JOURNALCTL_MISSING)
        echo "journalctl is not available; skipping the systemd journal."
        ;;
      SUDO_DENIED)
        echo "Warning: no passwordless sudo, so the systemd journal is read unprivileged and may" \
             "be missing most entries." >&2
        ;;
    esac
    if [ "$journal_access" != "JOURNALCTL_MISSING" ] \
      && _system_logs_collect_journal "$journal_access" "$start_epoch" "$end_epoch" \
           "$staging_dir/$SYSTEM_LOGS_JOURNAL_ARCHIVE_NAME"
    then
      journal_collected=1
    fi
  fi

  if [ "$copied" -eq 0 ] && [ "$journal_collected" -eq 0 ]
  then
    echo "Warning: collected no OS system logs: neither a matching file in '$log_dir' nor a" \
         "systemd journal dump. The bundle will contain no OS level evidence for this node." >&2
    echo "BUNDLE_STATUS=EMPTY"
    exit 0
  fi

  # Stage with sudo cp to stdout (read-only sudo), then tar as current user so yugabyte can rm.
  create_bundle_tar_or_fail "$remote_tar_path" "$staging_dir"

  echo "BUNDLE_STATUS=OK"
  echo "BUNDLE_PATH=$remote_tar_path"
  echo "Created system logs bundle at $remote_tar_path"
}

append_ybc_log_candidates() {
  log_dir=$1
  candidates_file=$2
  if [ ! -d "$log_dir" ]
  then
    return 0
  fi

  echo "YbcLogs: scanning log_dir=$log_dir"

  while IFS= read -r source_file
  do
    file_name="$(basename "$source_file")"
    copy_source="$source_file"

    parse_output=""
    parse_status=1
    parse_output=$(parse_file_type_and_date_from_regexes "$file_name" "${regex_patterns[@]}")
    parse_status=$?
    if [ "$parse_status" -ne 0 ] || [ -z "$parse_output" ]
    then
      parse_output=$(parse_file_type_and_date_from_ybc_name "$file_name")
      parse_status=$?
    fi
    if [ "$parse_status" -ne 0 ] || [ -z "$parse_output" ]
    then
      parse_output=$(parse_file_type_and_date_from_ybc_name_loose "$file_name")
      parse_status=$?
    fi
    if [ "$parse_status" -ne 0 ] || [ -z "$parse_output" ]
    then
      continue
    fi
    file_type="${parse_output%%|*}"
    file_timestamp_sortable="${parse_output##*|}"
    echo "${file_type}|${file_timestamp_sortable}|${copy_source}" >> "$candidates_file"
  done < <(find "$log_dir" -maxdepth 1 -type f)
}

scan_ybc_log_directories() {
  candidates_file=$1
  declare -A scanned_log_dirs=()

  add_ybc_log_scan_dir() {
    log_dir=$1
    if [ -z "$log_dir" ] || [ -n "${scanned_log_dirs[$log_dir]}" ]
    then
      return 0
    fi
    scanned_log_dirs["$log_dir"]=1
    append_ybc_log_candidates "$log_dir" "$candidates_file"
  }

  add_ybc_log_scan_dir "$ybc_logs_path"
  if [ -n "$ybc_server_conf_path" ]
  then
    ybc_controller_home=$(dirname "$(dirname "$ybc_server_conf_path")")
    add_ybc_log_scan_dir "$ybc_controller_home/logs"
  fi
  add_ybc_log_scan_dir "$node_home_dir/controller/logs"
}

create_ybc_logs_bundle() {
  node_home_dir=""
  ybc_logs_path=""
  ybc_server_conf_path=""
  start_date=""
  end_date=""
  remote_tar_path=""
  regex_patterns=()

  take_server_conf_redaction_args "$@"
  set -- "${sb_remaining_args[@]}"

  while [ "$#" -gt 0 ]
  do
    case "$1" in
      --node_home_dir)
        node_home_dir=$2
        shift 2
        ;;
      --ybc_logs_path)
        ybc_logs_path=$2
        shift 2
        ;;
      --ybc_server_conf_path)
        ybc_server_conf_path=$2
        shift 2
        ;;
      --start_date)
        start_date=$2
        shift 2
        ;;
      --end_date)
        end_date=$2
        shift 2
        ;;
      --remote_tar_path)
        remote_tar_path=$2
        shift 2
        ;;
      --regex)
        regex_patterns+=("$2")
        shift 2
        ;;
      --regex-b64)
        decoded_regex=$(decode_base64_string "$2")
        if [ $? -eq 0 ] && [ -n "$decoded_regex" ]
        then
          regex_patterns+=("$decoded_regex")
        fi
        shift 2
        ;;
      *)
        echo "Unknown argument: $1"
        echo "BUNDLE_STATUS=FAILED"
        exit 1
        ;;
    esac
  done

  if [ -z "$node_home_dir" ] || [ -z "$start_date" ] \
    || [ -z "$end_date" ] || [ -z "$remote_tar_path" ]
  then
    echo "Missing required args for create_ybc_logs_bundle"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  prefer_yb_log_parse_bin

  if [ -z "$ybc_logs_path" ]
  then
    ybc_logs_path="$node_home_dir/controller/logs"
  fi
  if [ -z "$ybc_server_conf_path" ]
  then
    ybc_server_conf_path="$node_home_dir/controller/conf/server.conf"
  fi
  if [ ${#regex_patterns[@]} -eq 0 ]
  then
    regex_patterns+=("((?:.*)(?:yb-)(?:controller(?:-server)?)(?:.*))(\\d{8}-\\d{6})(?:\\..*)?")
  fi

  start_date_sortable=$(normalize_date_to_sortable "$start_date")
  if [ $? -ne 0 ] || [ -z "$start_date_sortable" ]
  then
    echo "Invalid start_date format: $start_date"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi
  end_date_sortable=$(normalize_date_to_sortable "$end_date")
  if [ $? -ne 0 ] || [ -z "$end_date_sortable" ]
  then
    echo "Invalid end_date format: $end_date"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi
  echo "YbcLogs collection window (sortable): ${start_date_sortable} to ${end_date_sortable}"

  tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/ybc-logs.XXXXXX")"
  staging_dir="$tmp_root/staging"
  mkdir -p "$staging_dir"
  cleanup_cmd() {
    rm -rf "$tmp_root"
  }
  trap cleanup_cmd EXIT

  declare -A selected_files_map=()
  tmp_candidates="$tmp_root/candidates.txt"
  tmp_by_type="$tmp_root/by-type.txt"
  : > "$tmp_candidates"
  : > "$tmp_by_type"

  scan_ybc_log_directories "$tmp_candidates"

  echo "YbcLogs: candidate_count=$(wc -l < "$tmp_candidates" | tr -d " ")"

  if [ -s "$tmp_candidates" ]
  then
    cut -d'|' -f1 "$tmp_candidates" | sort -u > "$tmp_by_type"
    while IFS= read -r current_type
    do
      while IFS='|' read -r file_type file_timestamp_sortable source_file
      do
        if [ "$file_timestamp_sortable" -ge "$start_date_sortable" ] \
          && [ "$file_timestamp_sortable" -le "$end_date_sortable" ]
        then
          selected_files_map["$source_file"]=1
        elif [ "$file_timestamp_sortable" -lt "$start_date_sortable" ]
        then
          selected_files_map["$source_file"]=1
          break
        fi
      done < <(
        awk -F'|' -v current_type="$current_type" '$1 == current_type {print $0}' \
          "$tmp_candidates" | sort -t'|' -k2,2r
      )
    done < "$tmp_by_type"
  fi

  if [ -f "$ybc_server_conf_path" ]
  then
    selected_files_map["$ybc_server_conf_path"]=1
  fi

  if [ "${SUPPORT_BUNDLE_ESTIMATE_ONLY:-}" = "1" ]
  then
    total=0
    for source_file in "${!selected_files_map[@]}"
    do
      total=$((total + $(_byte_size_for_path "$source_file")))
    done
    _emit_estimate_result "$total"
    exit 0
  fi

  for source_file in "${!selected_files_map[@]}"
  do
    file_name="$(basename "$source_file")"
    if [[ "$source_file" == "$ybc_logs_path/"* ]] \
      || [[ "$source_file" == *"/controller/logs/"* ]]
    then
      target_rel="controller/logs/$file_name"
    elif [ "$source_file" = "$ybc_server_conf_path" ]
    then
      target_rel="controller/conf/server.conf"
    else
      target_rel="misc/$file_name"
    fi
    staging_target="$staging_dir/$target_rel"
    mkdir -p "$(dirname "$staging_target")"
    copy_or_report -L -p "$source_file" "$staging_target" || true
  done

  echo "YbcLogs: selected_file_count=${#selected_files_map[@]}"

  if [ -z "$(find "$staging_dir" -type f -print -quit)" ]
  then
    echo "No files found for YBC logs bundle."
    echo "REDACTION_STATUS=NOT_APPLICABLE"
    echo "BUNDLE_STATUS=EMPTY"
    exit 0
  fi

  redact_staged_server_conf_or_exit "$staging_dir" "$redact_bin" \
    "$yb_software_version" "$sensitive_gflags_b64" "$drop_server_conf"

  create_bundle_tar_or_fail "$remote_tar_path" "$staging_dir"
  echo "BUNDLE_STATUS=OK"
  echo "BUNDLE_PATH=$remote_tar_path"
  echo "Created YBC logs bundle at $remote_tar_path"
}

create_output_files_bundle() {
  node_home_dir=""
  remote_tar_path=""
  output_files=()

  take_server_conf_redaction_args "$@"
  set -- "${sb_remaining_args[@]}"

  while [ "$#" -gt 0 ]
  do
    case "$1" in
      --node_home_dir)
        node_home_dir=$2
        shift 2
        ;;
      --remote_tar_path)
        remote_tar_path=$2
        shift 2
        ;;
      --output_file)
        output_files+=("$2")
        shift 2
        ;;
      *)
        echo "Unknown argument: $1"
        echo "BUNDLE_STATUS=FAILED"
        exit 1
        ;;
    esac
  done

  if [ -z "$node_home_dir" ] || [ -z "$remote_tar_path" ]
  then
    echo "Missing required args for create_output_files_bundle"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  if [ ${#output_files[@]} -eq 0 ]
  then
    output_files=("master/master.out" "tserver/tserver.out")
  fi

  require_relative_file_paths "${output_files[@]}"

  tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/output-files.XXXXXX")"
  staging_dir="$tmp_root/staging/output_files"
  mkdir -p "$staging_dir"
  cleanup_cmd() {
    rm -rf "$tmp_root"
  }
  trap cleanup_cmd EXIT

  selected_files=()
  for rel_path in "${output_files[@]}"
  do
    source_file="$node_home_dir/$rel_path"
    if [ -f "$source_file" ]
    then
      selected_files+=("$source_file")
    fi
  done

  if [ ${#selected_files[@]} -eq 0 ]
  then
    echo "No files found for output files bundle."
    echo "BUNDLE_STATUS=EMPTY"
    exit 0
  fi

  for source_file in "${selected_files[@]}"
  do
    copy_or_report -p "$source_file" "$staging_dir/$(basename "$source_file")" || true
  done

  redact_staged_server_conf_or_exit "$tmp_root/staging" "$redact_bin" "$yb_software_version" \
    "$sensitive_gflags_b64" "$drop_server_conf"
  create_bundle_tar_or_fail "$remote_tar_path" "$tmp_root/staging"
  echo "BUNDLE_STATUS=OK"
  echo "BUNDLE_PATH=$remote_tar_path"
  echo "Created output files bundle at $remote_tar_path"
}

create_error_files_bundle() {
  node_home_dir=""
  remote_tar_path=""
  error_files=()

  take_server_conf_redaction_args "$@"
  set -- "${sb_remaining_args[@]}"

  while [ "$#" -gt 0 ]
  do
    case "$1" in
      --node_home_dir)
        node_home_dir=$2
        shift 2
        ;;
      --remote_tar_path)
        remote_tar_path=$2
        shift 2
        ;;
      --error_file)
        error_files+=("$2")
        shift 2
        ;;
      *)
        echo "Unknown argument: $1"
        echo "BUNDLE_STATUS=FAILED"
        exit 1
        ;;
    esac
  done

  if [ -z "$node_home_dir" ] || [ -z "$remote_tar_path" ]
  then
    echo "Missing required args for create_error_files_bundle"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  if [ ${#error_files[@]} -eq 0 ]
  then
    error_files=("master/master.err" "tserver/tserver.err")
  fi

  require_relative_file_paths "${error_files[@]}"

  tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/error-files.XXXXXX")"
  staging_dir="$tmp_root/staging/error_files"
  mkdir -p "$staging_dir"
  cleanup_cmd() {
    rm -rf "$tmp_root"
  }
  trap cleanup_cmd EXIT

  selected_files=()
  for rel_path in "${error_files[@]}"
  do
    source_file="$node_home_dir/$rel_path"
    if [ -f "$source_file" ]
    then
      selected_files+=("$source_file")
    fi
  done

  if [ ${#selected_files[@]} -eq 0 ]
  then
    echo "No files found for error files bundle."
    echo "BUNDLE_STATUS=EMPTY"
    exit 0
  fi

  for source_file in "${selected_files[@]}"
  do
    copy_or_report -p "$source_file" "$staging_dir/$(basename "$source_file")" || true
  done

  redact_staged_server_conf_or_exit "$tmp_root/staging" "$redact_bin" "$yb_software_version" \
    "$sensitive_gflags_b64" "$drop_server_conf"
  create_bundle_tar_or_fail "$remote_tar_path" "$tmp_root/staging"
  echo "BUNDLE_STATUS=OK"
  echo "BUNDLE_PATH=$remote_tar_path"
  echo "Created error files bundle at $remote_tar_path"
}

create_gflags_bundle() {
  node_home_dir=""
  remote_tar_path=""
  gflag_files=()

  take_server_conf_redaction_args "$@"
  set -- "${sb_remaining_args[@]}"

  while [ "$#" -gt 0 ]
  do
    case "$1" in
      --node_home_dir)
        node_home_dir=$2
        shift 2
        ;;
      --remote_tar_path)
        remote_tar_path=$2
        shift 2
        ;;
      --gflag_file)
        gflag_files+=("$2")
        shift 2
        ;;
      *)
        echo "Unknown argument: $1"
        echo "BUNDLE_STATUS=FAILED"
        exit 1
        ;;
    esac
  done

  if [ -z "$node_home_dir" ] || [ -z "$remote_tar_path" ]
  then
    echo "Missing required args for create_gflags_bundle"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  if [ ${#gflag_files[@]} -eq 0 ]
  then
    gflag_files=("master/conf/server.conf" "tserver/conf/server.conf")
  fi

  require_relative_file_paths "${gflag_files[@]}"

  tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/gflags.XXXXXX")"
  staging_dir="$tmp_root/staging"
  mkdir -p "$staging_dir"
  cleanup_cmd() {
    rm -rf "$tmp_root"
  }
  trap cleanup_cmd EXIT

  found=0
  for rel_path in "${gflag_files[@]}"
  do
    source_file="$node_home_dir/$rel_path"
    if [ ! -e "$source_file" ]
    then
      continue
    fi
    dest_path="$staging_dir/$rel_path"
    mkdir -p "$(dirname "$dest_path")"
    copy_or_report -p "$source_file" "$dest_path" && found=1
  done

  if [ "$found" -eq 0 ]
  then
    echo "No files found for gflags bundle under node_home_dir=$node_home_dir."
    echo "REDACTION_STATUS=NOT_APPLICABLE"
    echo "BUNDLE_STATUS=EMPTY"
    exit 0
  fi

  redact_staged_server_conf_or_exit "$staging_dir" "$redact_bin" \
    "$yb_software_version" "$sensitive_gflags_b64" "$drop_server_conf"

  create_bundle_tar_or_fail "$remote_tar_path" "$staging_dir"
  echo "BUNDLE_STATUS=OK"
  echo "BUNDLE_PATH=$remote_tar_path"
  echo "Created gflags bundle at $remote_tar_path"
}

create_instance_bundle() {
  node_home_dir=""
  remote_tar_path=""
  instance_files=()

  take_server_conf_redaction_args "$@"
  set -- "${sb_remaining_args[@]}"

  while [ "$#" -gt 0 ]
  do
    case "$1" in
      --node_data_dir)
        node_home_dir=$2
        shift 2
        ;;
      --remote_tar_path)
        remote_tar_path=$2
        shift 2
        ;;
      --instance_file)
        instance_files+=("$2")
        shift 2
        ;;
      *)
        echo "Unknown argument: $1"
        echo "BUNDLE_STATUS=FAILED"
        exit 1
        ;;
    esac
  done

  if [ -z "$node_home_dir" ] || [ -z "$remote_tar_path" ]
  then
    echo "Missing required args for create_instance_bundle"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  if [ ${#instance_files[@]} -eq 0 ]
  then
    instance_files=("master/instance" "tserver/instance")
  fi

  require_relative_file_paths "${instance_files[@]}"

  tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/instance-files.XXXXXX")"
  staging_dir="$tmp_root/staging"
  mkdir -p "$staging_dir"
  cleanup_cmd() {
    rm -rf "$tmp_root"
  }
  trap cleanup_cmd EXIT

  found=0
  for rel_path in "${instance_files[@]}"
  do
    source_file="$node_home_dir/$rel_path"
    if [ ! -f "$source_file" ]
    then
      continue
    fi
    dest_path="$staging_dir/$rel_path"
    mkdir -p "$(dirname "$dest_path")"
    copy_or_report -p "$source_file" "$dest_path" && found=1
  done

  if [ "$found" -eq 0 ]
  then
    echo "No files found for instance bundle."
    echo "BUNDLE_STATUS=EMPTY"
    exit 0
  fi

  redact_staged_server_conf_or_exit "$staging_dir" "$redact_bin" "$yb_software_version" \
    "$sensitive_gflags_b64" "$drop_server_conf"
  create_bundle_tar_or_fail "$remote_tar_path" "$staging_dir"
  echo "BUNDLE_STATUS=OK"
  echo "BUNDLE_PATH=$remote_tar_path"
  echo "Created instance bundle at $remote_tar_path"
}

create_consensus_meta_bundle() {
  node_home_dir=""
  remote_tar_path=""
  consensus_meta_files=()

  take_server_conf_redaction_args "$@"
  set -- "${sb_remaining_args[@]}"

  while [ "$#" -gt 0 ]
  do
    case "$1" in
      --node_data_dir)
        node_home_dir=$2
        shift 2
        ;;
      --remote_tar_path)
        remote_tar_path=$2
        shift 2
        ;;
      --consensus_meta_file)
        consensus_meta_files+=("$2")
        shift 2
        ;;
      *)
        echo "Unknown argument: $1"
        echo "BUNDLE_STATUS=FAILED"
        exit 1
        ;;
    esac
  done

  if [ -z "$node_home_dir" ] || [ -z "$remote_tar_path" ]
  then
    echo "Missing required args for create_consensus_meta_bundle"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  if [ ${#consensus_meta_files[@]} -eq 0 ]
  then
    consensus_meta_files=("master/consensus-meta" "tserver/consensus-meta")
  fi

  require_relative_file_paths "${consensus_meta_files[@]}"

  tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/consensus-meta.XXXXXX")"
  staging_dir="$tmp_root/staging"
  mkdir -p "$staging_dir"
  cleanup_cmd() {
    rm -rf "$tmp_root"
  }
  trap cleanup_cmd EXIT

  # Decide before staging anything. The estimate is measured from the sources below, so copying
  # first produced a staging tree that was written and then abandoned -- once per tablet, per node,
  # on every estimate. It also runs uncapped: the estimate path never goes through the cgroup
  # wrapper, unlike the collection it is predicting.
  found=0
  for rel_path in "${consensus_meta_files[@]}"
  do
    if [ -e "$node_home_dir/$rel_path" ]
    then
      found=1
      break
    fi
  done

  if [ "$found" -eq 0 ]
  then
    echo "No paths found for consensus meta bundle."
    echo "BUNDLE_STATUS=EMPTY"
    exit 0
  fi

  if [ "${SUPPORT_BUNDLE_ESTIMATE_ONLY:-}" = "1" ]
  then
    total=0
    for rel_path in "${consensus_meta_files[@]}"
    do
      source_path="$node_home_dir/$rel_path"
      if [ -e "$source_path" ]
      then
        total=$((total + $(_byte_size_for_path "$source_path")))
      fi
    done
    _emit_estimate_result "$total"
    exit 0
  fi

  for rel_path in "${consensus_meta_files[@]}"
  do
    source_path="$node_home_dir/$rel_path"
    if [ ! -e "$source_path" ]
    then
      continue
    fi
    dest_path="$staging_dir/$rel_path"
    mkdir -p "$(dirname "$dest_path")"
    if [ -d "$source_path" ]
    then
      copy_or_report -a "$source_path" "$dest_path" || true
    else
      copy_or_report -p "$source_path" "$dest_path" || true
    fi
  done

  redact_staged_server_conf_or_exit "$staging_dir" "$redact_bin" "$yb_software_version" \
    "$sensitive_gflags_b64" "$drop_server_conf"
  create_bundle_tar_or_fail "$remote_tar_path" "$staging_dir"
  echo "BUNDLE_STATUS=OK"
  echo "BUNDLE_PATH=$remote_tar_path"
  echo "Created consensus meta bundle at $remote_tar_path"
}

create_tablet_meta_bundle() {
  node_home_dir=""
  remote_tar_path=""
  tablet_meta_files=()

  take_server_conf_redaction_args "$@"
  set -- "${sb_remaining_args[@]}"

  while [ "$#" -gt 0 ]
  do
    case "$1" in
      --node_data_dir)
        node_home_dir=$2
        shift 2
        ;;
      --remote_tar_path)
        remote_tar_path=$2
        shift 2
        ;;
      --tablet_meta_file)
        tablet_meta_files+=("$2")
        shift 2
        ;;
      *)
        echo "Unknown argument: $1"
        echo "BUNDLE_STATUS=FAILED"
        exit 1
        ;;
    esac
  done

  if [ -z "$node_home_dir" ] || [ -z "$remote_tar_path" ]
  then
    echo "Missing required args for create_tablet_meta_bundle"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  if [ ${#tablet_meta_files[@]} -eq 0 ]
  then
    tablet_meta_files=("master/tablet-meta" "tserver/tablet-meta")
  fi

  require_relative_file_paths "${tablet_meta_files[@]}"

  tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/tablet-meta.XXXXXX")"
  staging_dir="$tmp_root/staging"
  mkdir -p "$staging_dir"
  cleanup_cmd() {
    rm -rf "$tmp_root"
  }
  trap cleanup_cmd EXIT

  # Decide before staging anything. The estimate is measured from the sources below, so copying
  # first produced a staging tree that was written and then abandoned -- once per tablet, per node,
  # on every estimate. It also runs uncapped: the estimate path never goes through the cgroup
  # wrapper, unlike the collection it is predicting.
  found=0
  for rel_path in "${tablet_meta_files[@]}"
  do
    if [ -e "$node_home_dir/$rel_path" ]
    then
      found=1
      break
    fi
  done

  if [ "$found" -eq 0 ]
  then
    echo "No paths found for tablet meta bundle."
    echo "BUNDLE_STATUS=EMPTY"
    exit 0
  fi

  if [ "${SUPPORT_BUNDLE_ESTIMATE_ONLY:-}" = "1" ]
  then
    total=0
    for rel_path in "${tablet_meta_files[@]}"
    do
      source_path="$node_home_dir/$rel_path"
      if [ -e "$source_path" ]
      then
        total=$((total + $(_byte_size_for_path "$source_path")))
      fi
    done
    _emit_estimate_result "$total"
    exit 0
  fi

  for rel_path in "${tablet_meta_files[@]}"
  do
    source_path="$node_home_dir/$rel_path"
    if [ ! -e "$source_path" ]
    then
      continue
    fi
    dest_path="$staging_dir/$rel_path"
    mkdir -p "$(dirname "$dest_path")"
    if [ -d "$source_path" ]
    then
      copy_or_report -a "$source_path" "$dest_path" || true
    else
      copy_or_report -p "$source_path" "$dest_path" || true
    fi
  done

  redact_staged_server_conf_or_exit "$staging_dir" "$redact_bin" "$yb_software_version" \
    "$sensitive_gflags_b64" "$drop_server_conf"
  create_bundle_tar_or_fail "$remote_tar_path" "$staging_dir"
  echo "BUNDLE_STATUS=OK"
  echo "BUNDLE_PATH=$remote_tar_path"
  echo "Created tablet meta bundle at $remote_tar_path"
}

create_node_agent_bundle() {
  node_agent_home_dir=""
  remote_tar_path=""
  node_agent_paths=()

  take_server_conf_redaction_args "$@"
  set -- "${sb_remaining_args[@]}"

  while [ "$#" -gt 0 ]
  do
    case "$1" in
      --node_agent_home_dir)
        node_agent_home_dir=$2
        shift 2
        ;;
      --remote_tar_path)
        remote_tar_path=$2
        shift 2
        ;;
      --node_agent_path)
        node_agent_paths+=("$2")
        shift 2
        ;;
      *)
        echo "Unknown argument: $1"
        echo "BUNDLE_STATUS=FAILED"
        exit 1
        ;;
    esac
  done

  if [ -z "$node_agent_home_dir" ] || [ -z "$remote_tar_path" ]
  then
    echo "Missing required args for create_node_agent_bundle"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  if [ ${#node_agent_paths[@]} -eq 0 ]
  then
    node_agent_paths=("logs" "config")
  fi

  require_relative_file_paths "${node_agent_paths[@]}"

  tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/node-agent.XXXXXX")"
  staging_dir="$tmp_root/staging/node-agent"
  mkdir -p "$staging_dir"
  cleanup_cmd() {
    rm -rf "$tmp_root"
  }
  trap cleanup_cmd EXIT

  selected_paths=()
  for rel_path in "${node_agent_paths[@]}"
  do
    source_path="$node_agent_home_dir/$rel_path"
    if [ -e "$source_path" ]
    then
      selected_paths+=("$source_path")
    fi
  done

  if [ ${#selected_paths[@]} -eq 0 ]
  then
    echo "No paths found for node agent bundle."
    echo "BUNDLE_STATUS=EMPTY"
    exit 0
  fi

  if [ "${SUPPORT_BUNDLE_ESTIMATE_ONLY:-}" = "1" ]
  then
    total=0
    for source_path in "${selected_paths[@]}"
    do
      if [ -d "$source_path" ]
      then
        while IFS= read -r file
        do
          total=$((total + $(_byte_size_for_path "$file")))
        done < <(find "$source_path" -maxdepth 1 -type f)
      else
        total=$((total + $(_byte_size_for_path "$source_path")))
      fi
    done
    _emit_estimate_result "$total"
    exit 0
  fi

  for source_path in "${selected_paths[@]}"
  do
    base_name="$(basename "$source_path")"
    target_path="$staging_dir/$base_name"
    if [ -d "$source_path" ]
    then
      mkdir -p "$target_path"
      while IFS= read -r file
      do
        copy_or_report -p "$file" "$target_path/$(basename "$file")" || true
      done < <(find "$source_path" -maxdepth 1 -type f)
    else
      copy_or_report -p "$source_path" "$target_path" || true
    fi
  done

  redact_staged_server_conf_or_exit "$tmp_root/staging" "$redact_bin" "$yb_software_version" \
    "$sensitive_gflags_b64" "$drop_server_conf"
  create_bundle_tar_or_fail "$remote_tar_path" "$tmp_root/staging"
  echo "BUNDLE_STATUS=OK"
  echo "BUNDLE_PATH=$remote_tar_path"
  echo "Created node agent bundle at $remote_tar_path"
}

create_core_files_bundle() {
  node_home_dir=""
  cores_dir=""
  start_date=""
  end_date=""
  remote_tar_path=""
  max_num_recent_cores=1
  max_core_file_size=25000000000

  while [ "$#" -gt 0 ]
  do
    case "$1" in
      --node_home_dir)
        node_home_dir=$2
        shift 2
        ;;
      --cores_dir)
        cores_dir=$2
        shift 2
        ;;
      --start_date)
        start_date=$2
        shift 2
        ;;
      --end_date)
        end_date=$2
        shift 2
        ;;
      --remote_tar_path)
        remote_tar_path=$2
        shift 2
        ;;
      --max_num_recent_cores)
        max_num_recent_cores=$2
        shift 2
        ;;
      --max_core_file_size)
        max_core_file_size=$2
        shift 2
        ;;
      *)
        echo "Unknown argument: $1"
        echo "BUNDLE_STATUS=FAILED"
        exit 1
        ;;
    esac
  done

  if [ -z "$node_home_dir" ] || [ -z "$start_date" ] \
    || [ -z "$end_date" ] || [ -z "$remote_tar_path" ]
  then
    echo "Missing required args for create_core_files_bundle"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  if [ -z "$cores_dir" ]
  then
    cores_dir="$node_home_dir/cores"
  fi
  if [ "$max_num_recent_cores" -le 0 ]
  then
    echo "No core files requested (max_num_recent_cores <= 0)."
    echo "BUNDLE_STATUS=EMPTY"
    exit 0
  fi

  # Match v1 CoreFilesComponent date filtering (yyyy-MM-dd, end date + 1 day).
  start_day="$(printf '%s' "$start_date" | sed -E 's/[T ].*//')"
  end_day="$(printf '%s' "$end_date" | sed -E 's/[T ].*//')"
  end_exclusive="$end_day"
  if end_exclusive_next="$(date -u -d "${end_day} + 1 day" +%Y-%m-%d 2>/dev/null)"
  then
    end_exclusive="$end_exclusive_next"
  elif end_exclusive_next="$(date -d "${end_day} + 1 day" +%Y-%m-%d 2>/dev/null)"
  then
    end_exclusive="$end_exclusive_next"
  fi

  # Resolve cores directory: yb-home/cores (often symlinked to mount/cores), with fallbacks.
  resolve_cores_directory() {
    local primary="$1"
    local yb_home="$2"
    local candidate resolved
    for candidate in "$primary" "$yb_home/cores"
    do
      if [ -z "$candidate" ]
      then
        continue
      fi
      resolved="$(readlink -f "$candidate" 2>/dev/null || printf '%s' "$candidate")"
      if [ -d "$resolved" ]
      then
        printf '%s' "$resolved"
        return 0
      fi
    done
    case "$yb_home" in
      */yb-data)
        candidate="${yb_home%/yb-data}/cores"
        resolved="$(readlink -f "$candidate" 2>/dev/null || printf '%s' "$candidate")"
        if [ -d "$resolved" ]
        then
          printf '%s' "$resolved"
          return 0
        fi
        ;;
    esac
    printf '%s' "${primary:-$yb_home/cores}"
  }
  cores_dir="$(resolve_cores_directory "$cores_dir" "$node_home_dir")"

  list_file="$(mktemp "${TMPDIR:-/tmp}/core-files-list.XXXXXX")"
  cleanup_cmd() {
    rm -f "$list_file"
  }
  trap cleanup_cmd EXIT
  get_paths_and_sizes_within_dates "$cores_dir" "$list_file" "$start_day" "$end_exclusive"

  selected_files=()
  while read -r size relpath
  do
    if [ -z "$relpath" ]
    then
      continue
    fi
    if ! [[ "$size" =~ ^[0-9]+$ ]]
    then
      continue
    fi
    if [ "$size" -gt "$max_core_file_size" ]
    then
      continue
    fi
    filepath="${relpath#./}"
    source_file="$cores_dir/$filepath"
    if [ ! -f "$source_file" ]
    then
      continue
    fi
    selected_files+=("$source_file")
    if [ ${#selected_files[@]} -ge "$max_num_recent_cores" ]
    then
      break
    fi
  done < "$list_file"
  rm -f "$list_file"

  if [ ${#selected_files[@]} -eq 0 ]
  then
    echo "No files found for core files bundle under cores_dir=$cores_dir" \
      "(range ${start_day} to ${end_day})."
    echo "BUNDLE_STATUS=EMPTY"
    exit 0
  fi

  if [ "${SUPPORT_BUNDLE_ESTIMATE_ONLY:-}" = "1" ]
  then
    total=$(_sum_byte_sizes_for_paths "${selected_files[@]}")
    _emit_estimate_result "$total"
    exit 0
  fi

  # A core can be 25 GB, doesn't change once written and needs no redaction, so it is tarred
  # where it is, as v1 does, rather than copied first. The tar holds cores/<file name>.
  core_names=()
  for source_file in "${selected_files[@]}"
  do
    core_names+=("${source_file#"$cores_dir/"}")
  done
  mkdir -p "$(dirname "$remote_tar_path")"
  if ! tar -czf "$remote_tar_path" -C "$cores_dir" --dereference \
    --transform 's,^.*/,,' --transform 's,^,cores/,' -- "${core_names[@]}"
  then
    rm -f "$remote_tar_path"
    echo "Failed to create bundle tar at $remote_tar_path"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi
  echo "BUNDLE_STATUS=OK"
  echo "BUNDLE_PATH=$remote_tar_path"
  echo "Created core files bundle at $remote_tar_path"
}

# Appends a labeled section with command output to a stats file. Command failures are recorded
# but do not abort collection.
append_command_section() {
  section_label=$1
  output_path=$2
  shift 2
  {
    echo "===== ${section_label} ====="
    echo "command: $*"
    echo "collected_at_utc=$(date -u +"%Y-%m-%dT%H:%M:%SZ" 2>/dev/null || date)"
    echo ""
    if command -v "$1" >/dev/null 2>&1
    then
      "$@" 2>&1 || echo "(command exited with status $?)"
    else
      echo "(command not found: $1)"
    fi
    echo ""
  } >> "$output_path"
}

support_bundle_collection_timestamp() {
  date -u +"%Y%m%dT%H%M%SZ" 2>/dev/null || date +"%Y%m%dT%H%M%SZ"
}

# Uses the colonated date format (safe under nested shell/systemd-run) then compacts to
# filename form.
support_bundle_filename_timestamp() {
  date -u +"%Y-%m-%dT%H:%M:%SZ" 2>/dev/null | tr -d ':-' || support_bundle_collection_timestamp
}

append_ps_pmap_for_process() {
  label=$1
  pids=$2
  output_path=$3

  {
    echo "===== ${label} ====="
    if [ -z "$pids" ]
    then
      echo "(no matching process)"
      echo ""
      return 0
    fi

    for pid in $pids
    do
      echo "--- pid=${pid} ---"
      echo "ps summary:"
      ps -p "$pid" -o pid,ppid,user,group,stat,lstart,etime,rss,vsz,%mem,%cpu,nlwp,pri,ni,cmd \
        2>&1 || echo "(ps failed for pid ${pid})"
      echo ""
      echo "pmap:"
      if command -v pmap >/dev/null 2>&1
      then
        pmap -x "$pid" 2>&1 || echo "(pmap failed for pid ${pid})"
      else
        echo "(pmap not found on this node)"
      fi
      echo ""
    done
  } >> "$output_path"
}

collect_top_ps_pmap_stats() {
  remote_tar_path=""
  top_process_limit=10
  mount_path=""
  node_home_dir=""
  yb_home_dir=""

  while [ "$#" -gt 0 ]
  do
    case "$1" in
      --remote_tar_path)
        remote_tar_path=$2
        shift 2
        ;;
      --top_process_limit)
        top_process_limit=$2
        shift 2
        ;;
      --mount_path)
        mount_path=$2
        shift 2
        ;;
      --node_home_dir)
        node_home_dir=$2
        shift 2
        ;;
      --yb_home_dir)
        yb_home_dir=$2
        shift 2
        ;;
      *)
        echo "Unknown argument: $1"
        echo "BUNDLE_STATUS=FAILED"
        exit 1
        ;;
    esac
  done

  if [ -z "$remote_tar_path" ]
  then
    echo "Missing required args for collect_top_ps_pmap_stats"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  if ! [[ "$top_process_limit" =~ ^[0-9]+$ ]] || [ "$top_process_limit" -le 0 ]
  then
    top_process_limit=10
  fi

  ts="$(support_bundle_filename_timestamp)"
  output_file_name="top_ps_pmap_stats_${ts}.txt"

  tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/top-ps-pmap-stats.XXXXXX")"
  staging_dir="$tmp_root/staging/top_ps_pmap_stats"
  mkdir -p "$staging_dir"
  output_path="$staging_dir/$output_file_name"
  cleanup_cmd() {
    rm -rf "$tmp_root"
  }
  trap cleanup_cmd EXIT

  host_name="$(hostname 2>/dev/null || echo unknown)"
  now_utc="$(date -u +"%Y-%m-%dT%H:%M:%SZ" 2>/dev/null || date)"
  {
    echo "host=$host_name"
    echo "collected_at_utc=$now_utc"
    echo "top_process_limit=$top_process_limit"
    echo ""
  } > "$output_path"

  append_command_section "loadavg" "$output_path" cat /proc/loadavg
  append_command_section "uptime" "$output_path" uptime
  append_command_section "top_batch" "$output_path" env COLUMNS=512 top -b -n 1
  {
    echo "===== ps_cpu ====="
    echo "command: ps aux --sort=-%cpu | head -n ${top_process_limit}"
    echo "collected_at_utc=$(date -u +"%Y-%m-%dT%H:%M:%SZ" 2>/dev/null || date)"
    echo ""
    ps aux --sort=-%cpu 2>&1 | head -n "$top_process_limit" \
      || echo "(command exited with status $?)"
    echo ""
  } >> "$output_path"
  {
    echo "===== ps_mem ====="
    echo "command: ps aux --sort=-%mem | head -n ${top_process_limit}"
    echo "collected_at_utc=$(date -u +"%Y-%m-%dT%H:%M:%SZ" 2>/dev/null || date)"
    echo ""
    ps aux --sort=-%mem 2>&1 | head -n "$top_process_limit" \
      || echo "(command exited with status $?)"
    echo ""
  } >> "$output_path"
  if command -v mpstat >/dev/null 2>&1
  then
    append_command_section "mpstat" "$output_path" mpstat -P ALL 1 1
  fi

  {
    echo "===== process ps/pmap ====="
    echo ""
  } >> "$output_path"
  # By process name, not command line, so log tails and shells naming these paths don't match.
  tserver_pids=$(pgrep -x yb-tserver 2>/dev/null || true)
  append_ps_pmap_for_process "yb-tserver" "$tserver_pids" "$output_path"
  append_ps_pmap_for_process "yb-master" "$(pgrep -x yb-master 2>/dev/null || true)" \
    "$output_path"
  # Every backend is named postgres too; the postmaster is the one the tserver started.
  postmaster_pids=""
  if [ -n "$tserver_pids" ]
  then
    postmaster_pids=$(pgrep -x -P "${tserver_pids//$'\n'/,}" postgres 2>/dev/null || true)
  fi
  append_ps_pmap_for_process "postgres" "$postmaster_pids" "$output_path"

  create_bundle_tar_or_fail "$remote_tar_path" "$tmp_root/staging"
  echo "BUNDLE_STATUS=OK"
  echo "BUNDLE_PATH=$remote_tar_path"
  echo "Created top ps/pmap stats bundle at $remote_tar_path"
}

# Appends command output using support-bundle THP format: "command :" / "Output:" blocks.
append_command_output_block() {
  output_path=$1
  cmd_display=$2
  shift 2
  {
    echo "command : ${cmd_display}"
    echo "Output:"
    "$@" 2>&1 || echo "(command exited with status $?)"
    echo ""
  } >> "$output_path"
}

collect_thp_and_timezone_settings() {
  remote_tar_path=""
  mount_path=""
  node_home_dir=""
  yb_home_dir=""

  while [ "$#" -gt 0 ]
  do
    case "$1" in
      --remote_tar_path)
        remote_tar_path=$2
        shift 2
        ;;
      --mount_path)
        mount_path=$2
        shift 2
        ;;
      --node_home_dir)
        node_home_dir=$2
        shift 2
        ;;
      --yb_home_dir)
        yb_home_dir=$2
        shift 2
        ;;
      *)
        echo "Unknown argument: $1"
        echo "BUNDLE_STATUS=FAILED"
        exit 1
        ;;
    esac
  done

  if [ -z "$remote_tar_path" ]
  then
    echo "Missing required args for collect_thp_and_timezone_settings"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  ts="$(support_bundle_filename_timestamp)"
  output_file_name="thp_and_timezone_settings_${ts}.txt"

  tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/thp-timezone-settings.XXXXXX")"
  staging_dir="$tmp_root/staging/thp_and_timezone_settings"
  mkdir -p "$staging_dir"
  output_path="$staging_dir/$output_file_name"
  cleanup_cmd() {
    rm -rf "$tmp_root"
  }
  trap cleanup_cmd EXIT

  host_name="$(hostname 2>/dev/null || echo unknown)"
  now_utc="$(date -u +"%Y-%m-%dT%H:%M:%SZ" 2>/dev/null || date)"
  {
    echo "host=$host_name"
    echo "collected_at_utc=$now_utc"
    echo ""
  } > "$output_path"

  append_command_output_block "$output_path" \
    "cat /sys/kernel/mm/transparent_hugepage/enabled" \
    cat /sys/kernel/mm/transparent_hugepage/enabled

  append_command_output_block "$output_path" \
    "cat /sys/kernel/mm/transparent_hugepage/defrag" \
    cat /sys/kernel/mm/transparent_hugepage/defrag

  append_command_output_block "$output_path" \
    "grep -i huge /proc/meminfo" \
    grep -i huge /proc/meminfo

  if command -v timedatectl >/dev/null 2>&1
  then
    append_command_output_block "$output_path" "timedatectl" timedatectl
  else
    append_command_output_block "$output_path" "timedatectl" \
      sh -c 'echo "(timedatectl not found on this node)"'
  fi

  create_bundle_tar_or_fail "$remote_tar_path" "$tmp_root/staging"
  echo "BUNDLE_STATUS=OK"
  echo "BUNDLE_PATH=$remote_tar_path"
  echo "Created THP and timezone settings bundle at $remote_tar_path"
}

# One find per root, cut off at max_lines: awk exits there, so find stops on its next write
# instead of walking the rest of the tree.
append_dir_listing_for_root() {
  root=$1
  output_path=$2
  max_lines=$3
  {
    echo "===== Directory map: ${root} ====="
    echo "fields: inode mode links uid gid size_bytes mtime path"
    find "$root" -printf '%i %M %n %U %G %s %TY-%Tm-%Td %TH:%TM %p\n' 2>&1 |
      awk -v max="$max_lines" 'NR > max { print "(truncated at " max " lines)"; exit } { print }'
    echo ""
  } >> "$output_path"
}

# SST count and bytes per directory (tablet, .intents, snapshot), data blocks included, from one
# find and no per-file commands.
append_sst_inventory_for_root() {
  root=$1
  output_path=$2
  {
    echo "===== SST inventory: ${root} ====="
    if [ ! -d "$root" ]
    then
      echo "(skipped: directory does not exist)"
      echo ""
      return 0
    fi
    find "$root" -type f \( -name '*.sst' -o -name '*.SST' -o -name '*.sst.sblock.*' \) \
      -printf '%h\t%f\t%s\n' 2>/dev/null |
      awk -F '\t' -v root="$root" '
        {
          dir = $1
          if (dir == root) {
            dir = "."
          } else if (index(dir, root "/") == 1) {
            dir = substr(dir, length(root) + 2)
          }
          if ($2 ~ /\.(sst|SST)$/) {
            count[dir]++
            files++
          }
          bytes[dir] += $3
          total += $3
        }
        END {
          printf "total_sst_files=%d\n", files
          printf "total_sst_bytes=%.0f\n", total
          if (files == 0) {
            print "(no SST files found)"
            exit
          }
          fflush()
          sorter = "LC_ALL=C sort"
          for (dir in bytes) {
            printf "%s sst_count=%d sst_bytes=%.0f\n", dir, count[dir], bytes[dir] | sorter
          }
          close(sorter)
        }'
    echo ""
  } >> "$output_path"
}

collect_data_dir_map() {
  remote_tar_path=""
  mount_path=""
  node_home_dir=""
  yb_home_dir=""
  fs_data_dirs=""
  rocksdb_scan_roots=""
  max_listing_lines=200000

  while [ "$#" -gt 0 ]
  do
    case "$1" in
      --remote_tar_path)
        remote_tar_path=$2
        shift 2
        ;;
      --mount_path)
        mount_path=$2
        shift 2
        ;;
      --node_home_dir)
        node_home_dir=$2
        shift 2
        ;;
      --yb_home_dir)
        yb_home_dir=$2
        shift 2
        ;;
      --fs_data_dirs)
        fs_data_dirs=$2
        shift 2
        ;;
      --rocksdb_scan_roots)
        rocksdb_scan_roots=$2
        shift 2
        ;;
      --max_listing_lines)
        max_listing_lines=$2
        shift 2
        ;;
      *)
        echo "Unknown argument: $1"
        echo "BUNDLE_STATUS=FAILED"
        exit 1
        ;;
    esac
  done

  if [ -z "$remote_tar_path" ]
  then
    echo "Missing required args for collect_data_dir_map"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi
  if ! [[ "$max_listing_lines" =~ ^[1-9][0-9]*$ ]]
  then
    echo "Invalid --max_listing_lines: '$max_listing_lines' (want a positive integer)"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  ts="$(support_bundle_filename_timestamp)"
  output_file_name="data_dir_map_${ts}.txt"

  tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/data-dir-map.XXXXXX")"
  staging_dir="$tmp_root/staging/data_dir_map"
  mkdir -p "$staging_dir"
  output_path="$staging_dir/$output_file_name"
  cleanup_cmd() {
    rm -rf "$tmp_root"
  }
  trap cleanup_cmd EXIT

  host_name="$(hostname 2>/dev/null || echo unknown)"
  now_utc="$(date -u +"%Y-%m-%dT%H:%M:%SZ" 2>/dev/null || date)"
  {
    echo "host=$host_name"
    echo "collected_at_utc=$now_utc"
    echo "mount_path=$mount_path"
    echo "node_home_dir=$node_home_dir"
    echo "yb_home_dir=$yb_home_dir"
    echo "fs_data_dirs=$fs_data_dirs"
    echo "rocksdb_scan_roots=$rocksdb_scan_roots"
    echo "max_listing_lines=$max_listing_lines"
    echo ""
    echo "correlation_note=Map tablet_id to table name using TabletReport/ in this bundle and"
    echo "  tserver/tablet-meta under this node's directory."
    echo ""
  } > "$output_path"

  if [ -n "$node_home_dir" ] && [ -d "$node_home_dir" ]
  then
    append_dir_listing_for_root "$node_home_dir" "$output_path" "$max_listing_lines"
  elif [ -n "$node_home_dir" ]
  then
    {
      echo "===== Directory map (skipped): ${node_home_dir} ====="
      echo "(node_home_dir does not exist on this node)"
      echo ""
    } >> "$output_path"
  fi

  scan_roots=()
  if [ -n "$rocksdb_scan_roots" ]
  then
    old_ifs=$IFS
    IFS=','
    # shellcheck disable=SC2086
    set -- $rocksdb_scan_roots
    for root in "$@"
    do
      trimmed=$(echo "$root" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
      if [ -n "$trimmed" ]
      then
        scan_roots+=("$trimmed")
      fi
    done
    IFS=$old_ifs
  fi

  roots_found=0
  for root in "${scan_roots[@]}"
  do
    if [ ! -d "$root" ]
    then
      {
        echo "===== Directory map (skipped): ${root} ====="
        echo "(directory does not exist on this node)"
        echo ""
      } >> "$output_path"
      continue
    fi
    roots_found=$((roots_found + 1))
    # The first data dir's rocksdb sits under node_home_dir and is already in its listing.
    if [ -n "$node_home_dir" ] && [ -d "$node_home_dir" ] &&
      [[ "${root%/}/" == "${node_home_dir%/}/"* ]]
    then
      continue
    fi
    append_dir_listing_for_root "$root" "$output_path" "$max_listing_lines"
  done

  if [ ${#scan_roots[@]} -eq 0 ]
  then
    {
      echo "===== Directory map ====="
      echo "(no rocksdb_scan_roots provided)"
      echo ""
    } >> "$output_path"
  elif [ "$roots_found" -eq 0 ]
  then
    {
      echo "===== Directory map ====="
      echo "(no rocksdb roots found on this node; master-only or non-tserver node)"
      echo ""
    } >> "$output_path"
  fi

  for root in "${scan_roots[@]}"
  do
    append_sst_inventory_for_root "$root" "$output_path"
  done

  create_bundle_tar_or_fail "$remote_tar_path" "$tmp_root/staging"
  echo "BUNDLE_STATUS=OK"
  echo "BUNDLE_PATH=$remote_tar_path"
  echo "Created data dir map bundle at $remote_tar_path"
}

collect_threadz_memz_rpcz_stats_curl_endpoint() {
  url=$1
  output_path=$2
  connect_timeout=$3
  max_time=$4

  if ! command -v curl >/dev/null 2>&1
  then
    {
      echo "url=${url}"
      echo "(curl not found)"
    } > "$output_path"
    return 127
  fi

  http_code="$(curl -sS --connect-timeout "$connect_timeout" -m "$max_time" \
    -o "$output_path" -w "%{http_code}" "$url" 2>"${output_path}.curl_err")"
  curl_exit=$?
  if [ -s "${output_path}.curl_err" ]
  then
    {
      echo "url=${url}"
      echo "curl_stderr:"
      cat "${output_path}.curl_err"
      echo ""
    } >> "$output_path"
  fi
  rm -f "${output_path}.curl_err"

  if [ "$curl_exit" -eq 0 ] && [ "$http_code" = "200" ]
  then
    return 0
  fi

  {
    echo "url=${url}"
    echo "http_code=${http_code:-unknown}"
    echo "curl_exit_status=${curl_exit}"
    echo ""
  } >> "$output_path"
  return 1
}

collect_threadz_memz_rpcz_stats_fetch_all() {
  local curl_host=$1
  local tserver_port=$2
  local rpcz_path=$3
  local memz_path=$4
  local mem_trackers_path=$5
  local threadz_path=$6
  local connect_timeout=$7
  local max_time=$8
  local base_url="http://${curl_host}:${tserver_port}"

  RPCZ_STATUS=0
  MEMZ_STATUS=0
  MEM_TRACKERS_STATUS=0
  THREADZ_STATUS=0

  collect_threadz_memz_rpcz_stats_curl_endpoint \
    "${base_url}/rpcz?get_wait_state=true" \
    "$rpcz_path" \
    "$connect_timeout" \
    "$max_time" \
    || RPCZ_STATUS=$?

  collect_threadz_memz_rpcz_stats_curl_endpoint \
    "${base_url}/memz?raw" \
    "$memz_path" \
    "$connect_timeout" \
    "$max_time" \
    || MEMZ_STATUS=$?

  collect_threadz_memz_rpcz_stats_curl_endpoint \
    "${base_url}/api/v1/mem-trackers" \
    "$mem_trackers_path" \
    "$connect_timeout" \
    "$max_time" \
    || MEM_TRACKERS_STATUS=$?

  collect_threadz_memz_rpcz_stats_curl_endpoint \
    "${base_url}/threadz?group=all" \
    "$threadz_path" \
    "$connect_timeout" \
    "$max_time" \
    || THREADZ_STATUS=$?
}

collect_threadz_memz_rpcz_stats_success_count() {
  success_count=0
  if [ "$RPCZ_STATUS" -eq 0 ]
  then
    success_count=$((success_count + 1))
  fi
  if [ "$MEMZ_STATUS" -eq 0 ]
  then
    success_count=$((success_count + 1))
  fi
  if [ "$MEM_TRACKERS_STATUS" -eq 0 ]
  then
    success_count=$((success_count + 1))
  fi
  if [ "$THREADZ_STATUS" -eq 0 ]
  then
    success_count=$((success_count + 1))
  fi
  echo "$success_count"
}

collect_threadz_memz_rpcz_stats() {
  remote_tar_path=""
  host_ip=""
  tserver_http_port=""
  curl_connect_timeout=5
  curl_max_time=120

  while [ "$#" -gt 0 ]
  do
    case "$1" in
      --remote_tar_path)
        remote_tar_path=$2
        shift 2
        ;;
      --host_ip)
        host_ip=$2
        shift 2
        ;;
      --tserver_http_port)
        tserver_http_port=$2
        shift 2
        ;;
      --mount_path)
        shift 2
        ;;
      --node_home_dir)
        shift 2
        ;;
      --yb_home_dir)
        shift 2
        ;;
      --curl_connect_timeout)
        curl_connect_timeout=$2
        shift 2
        ;;
      --curl_max_time)
        curl_max_time=$2
        shift 2
        ;;
      *)
        echo "Unknown argument: $1"
        echo "BUNDLE_STATUS=FAILED"
        exit 1
        ;;
    esac
  done

  if [ -z "$host_ip" ]
  then
    host_ip="$(hostname -I 2>/dev/null | awk '{print $1}')"
  fi

  if [ -z "$remote_tar_path" ] || [ -z "$host_ip" ] || [ -z "$tserver_http_port" ]
  then
    echo "Missing required args for collect_threadz_memz_rpcz_stats"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  if ! [[ "$tserver_http_port" =~ ^[0-9]+$ ]] || [ "$tserver_http_port" -le 0 ]
  then
    echo "Invalid tserver_http_port: $tserver_http_port"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  if ! [[ "$curl_connect_timeout" =~ ^[0-9]+$ ]] || [ "$curl_connect_timeout" -le 0 ]
  then
    curl_connect_timeout=5
  fi
  if ! [[ "$curl_max_time" =~ ^[0-9]+$ ]] || [ "$curl_max_time" -le 0 ]
  then
    curl_max_time=120
  fi

  ts="$(support_bundle_filename_timestamp)"
  rpcz_output_file_name="rpcz_${ts}.json"
  memz_output_file_name="memz_${ts}.txt"
  mem_trackers_output_file_name="mem_trackers_${ts}.json"
  threadz_output_file_name="threadz_${ts}.html"
  meta_output_file_name="collection_meta_${ts}.txt"

  tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/threadz-memz-rpcz-stats.XXXXXX")"
  staging_dir="$tmp_root/staging/collect_threadz_memz_rpcz_stats"
  mkdir -p "$staging_dir"
  cleanup_cmd() {
    rm -rf "$tmp_root"
  }
  trap cleanup_cmd EXIT

  host_name="$(hostname 2>/dev/null || echo unknown)"
  now_utc="$(date -u +"%Y-%m-%dT%H:%M:%SZ" 2>/dev/null || date)"
  curl_host_used="$host_ip"
  base_url="http://${curl_host_used}:${tserver_http_port}"

  rpcz_path="$staging_dir/$rpcz_output_file_name"
  memz_path="$staging_dir/$memz_output_file_name"
  mem_trackers_path="$staging_dir/$mem_trackers_output_file_name"
  threadz_path="$staging_dir/$threadz_output_file_name"
  meta_path="$staging_dir/$meta_output_file_name"

  collect_threadz_memz_rpcz_stats_fetch_all \
    "$curl_host_used" \
    "$tserver_http_port" \
    "$rpcz_path" \
    "$memz_path" \
    "$mem_trackers_path" \
    "$threadz_path" \
    "$curl_connect_timeout" \
    "$curl_max_time"

  success_count="$(collect_threadz_memz_rpcz_stats_success_count)"

  if [ "$success_count" -eq 0 ] && [ "$host_ip" != "127.0.0.1" ]
  then
    fallback_host="127.0.0.1"
    collect_threadz_memz_rpcz_stats_fetch_all \
      "$fallback_host" \
      "$tserver_http_port" \
      "$rpcz_path" \
      "$memz_path" \
      "$mem_trackers_path" \
      "$threadz_path" \
      "$curl_connect_timeout" \
      "$curl_max_time"
    success_count="$(collect_threadz_memz_rpcz_stats_success_count)"
    if [ "$success_count" -gt 0 ]
    then
      curl_host_used="$fallback_host"
      base_url="http://${curl_host_used}:${tserver_http_port}"
    fi
  fi

  base_url="http://${curl_host_used}:${tserver_http_port}"

  {
    echo "host=$host_name"
    echo "collected_at_utc=$now_utc"
    echo "host_ip=$host_ip"
    echo "curl_host_used=$curl_host_used"
    echo "tserver_http_port=$tserver_http_port"
    echo "base_url=$base_url"
    echo "rpcz_exit_status=$RPCZ_STATUS"
    echo "memz_exit_status=$MEMZ_STATUS"
    echo "mem_trackers_exit_status=$MEM_TRACKERS_STATUS"
    echo "threadz_exit_status=$THREADZ_STATUS"
    echo "successful_endpoints=$success_count"
  } > "$meta_path"

  create_bundle_tar_or_fail "$remote_tar_path" "$tmp_root/staging"
  echo "BUNDLE_STATUS=OK"
  echo "BUNDLE_PATH=$remote_tar_path"
  if [ "$success_count" -eq 0 ]
  then
    echo "Created threadz/memz/rpcz stats bundle at $remote_tar_path (no HTTP 200 responses;" \
      "see collection_meta and endpoint files)"
  else
    echo "Created threadz/memz/rpcz stats bundle at $remote_tar_path"
  fi
}

collect_heap_snapshot_fetch_all() {
  local curl_host=$1
  local tserver_port=$2
  local master_port=$3
  local collect_tserver=$4
  local collect_master=$5
  local tserver_heap_current_by_count_path=$6
  local tserver_heap_peak_by_count_path=$7
  local tserver_heap_current_by_estimated_bytes_path=$8
  local master_heap_current_by_count_path=$9
  local connect_timeout=${10}
  local max_time=${11}
  local tserver_base_url="http://${curl_host}:${tserver_port}"
  local master_base_url="http://${curl_host}:${master_port}"

  TSERVER_HEAP_CURRENT_BY_COUNT_STATUS=0
  TSERVER_HEAP_PEAK_BY_COUNT_STATUS=0
  TSERVER_HEAP_CURRENT_BY_ESTIMATED_BYTES_STATUS=0
  MASTER_HEAP_CURRENT_BY_COUNT_STATUS=0
  TSERVER_HEAP_SKIPPED=0
  MASTER_HEAP_SKIPPED=0

  if [ "$collect_tserver" = "true" ]
  then
    collect_threadz_memz_rpcz_stats_curl_endpoint \
      "${tserver_base_url}/pprof/heap_snapshot?peak_heap=false&order_by=count" \
      "$tserver_heap_current_by_count_path" \
      "$connect_timeout" \
      "$max_time" \
      || TSERVER_HEAP_CURRENT_BY_COUNT_STATUS=$?

    collect_threadz_memz_rpcz_stats_curl_endpoint \
      "${tserver_base_url}/pprof/heap_snapshot?peak_heap=true&order_by=count" \
      "$tserver_heap_peak_by_count_path" \
      "$connect_timeout" \
      "$max_time" \
      || TSERVER_HEAP_PEAK_BY_COUNT_STATUS=$?

    collect_threadz_memz_rpcz_stats_curl_endpoint \
      "${tserver_base_url}/pprof/heap_snapshot?peak_heap=false&order_by=estimated_bytes" \
      "$tserver_heap_current_by_estimated_bytes_path" \
      "$connect_timeout" \
      "$max_time" \
      || TSERVER_HEAP_CURRENT_BY_ESTIMATED_BYTES_STATUS=$?
  else
    TSERVER_HEAP_SKIPPED=1
    for skipped_tserver_path in \
      "$tserver_heap_current_by_count_path" \
      "$tserver_heap_peak_by_count_path" \
      "$tserver_heap_current_by_estimated_bytes_path"
    do
      {
        echo "tserver_heap_snapshot_skipped=true"
        echo "reason=collect_tserver is not true"
      } > "$skipped_tserver_path"
    done
  fi

  if [ "$collect_master" = "true" ]
  then
    collect_threadz_memz_rpcz_stats_curl_endpoint \
      "${master_base_url}/pprof/heap_snapshot?peak_heap=false&order_by=count" \
      "$master_heap_current_by_count_path" \
      "$connect_timeout" \
      "$max_time" \
      || MASTER_HEAP_CURRENT_BY_COUNT_STATUS=$?
  else
    MASTER_HEAP_SKIPPED=1
    {
      echo "master_heap_snapshot_skipped=true"
      echo "reason=collect_master is not true"
    } > "$master_heap_current_by_count_path"
  fi
}

collect_heap_snapshot_success_count() {
  success_count=0
  if [ "$TSERVER_HEAP_SKIPPED" -eq 0 ]
  then
    if [ "$TSERVER_HEAP_CURRENT_BY_COUNT_STATUS" -eq 0 ]
    then
      success_count=$((success_count + 1))
    fi
    if [ "$TSERVER_HEAP_PEAK_BY_COUNT_STATUS" -eq 0 ]
    then
      success_count=$((success_count + 1))
    fi
    if [ "$TSERVER_HEAP_CURRENT_BY_ESTIMATED_BYTES_STATUS" -eq 0 ]
    then
      success_count=$((success_count + 1))
    fi
  fi
  if [ "$MASTER_HEAP_SKIPPED" -eq 0 ] && [ "$MASTER_HEAP_CURRENT_BY_COUNT_STATUS" -eq 0 ]
  then
    success_count=$((success_count + 1))
  fi
  echo "$success_count"
}

collect_heap_snapshot() {
  remote_tar_path=""
  host_ip=""
  tserver_http_port=""
  master_http_port=""
  collect_master="false"
  # Defaults to true so that callers predating this flag keep collecting the tserver endpoints.
  collect_tserver="true"
  curl_connect_timeout=5
  curl_max_time=120

  while [ "$#" -gt 0 ]
  do
    case "$1" in
      --remote_tar_path)
        remote_tar_path=$2
        shift 2
        ;;
      --host_ip)
        host_ip=$2
        shift 2
        ;;
      --tserver_http_port)
        tserver_http_port=$2
        shift 2
        ;;
      --master_http_port)
        master_http_port=$2
        shift 2
        ;;
      --collect_master)
        collect_master=$2
        shift 2
        ;;
      --collect_tserver)
        collect_tserver=$2
        shift 2
        ;;
      --mount_path)
        shift 2
        ;;
      --node_home_dir)
        shift 2
        ;;
      --yb_home_dir)
        shift 2
        ;;
      --curl_connect_timeout)
        curl_connect_timeout=$2
        shift 2
        ;;
      --curl_max_time)
        curl_max_time=$2
        shift 2
        ;;
      *)
        echo "Unknown argument: $1"
        echo "BUNDLE_STATUS=FAILED"
        exit 1
        ;;
    esac
  done

  if [ -z "$host_ip" ]
  then
    host_ip="$(hostname -I 2>/dev/null | awk '{print $1}')"
  fi

  if [ -z "$remote_tar_path" ] || [ -z "$host_ip" ]
  then
    echo "Missing required args for collect_heap_snapshot"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  if [ "$collect_tserver" = "true" ]
  then
    if ! [[ "$tserver_http_port" =~ ^[0-9]+$ ]] || [ "$tserver_http_port" -le 0 ]
    then
      echo "Invalid tserver_http_port: $tserver_http_port"
      echo "BUNDLE_STATUS=FAILED"
      exit 1
    fi
  fi

  if [ "$collect_master" = "true" ]
  then
    if ! [[ "$master_http_port" =~ ^[0-9]+$ ]] || [ "$master_http_port" -le 0 ]
    then
      echo "Invalid master_http_port: $master_http_port"
      echo "BUNDLE_STATUS=FAILED"
      exit 1
    fi
  fi

  if ! [[ "$curl_connect_timeout" =~ ^[0-9]+$ ]] || [ "$curl_connect_timeout" -le 0 ]
  then
    curl_connect_timeout=5
  fi
  if ! [[ "$curl_max_time" =~ ^[0-9]+$ ]] || [ "$curl_max_time" -le 0 ]
  then
    curl_max_time=120
  fi

  ts="$(support_bundle_filename_timestamp)"
  tserver_heap_current_by_count_output_file_name="tserver_heap_current_by_count_${ts}.html"
  tserver_heap_peak_by_count_output_file_name="tserver_heap_peak_by_count_${ts}.html"
  tserver_heap_current_by_estimated_bytes_output_file_name=\
"tserver_heap_current_by_estimated_bytes_${ts}.html"
  master_heap_current_by_count_output_file_name="master_heap_current_by_count_${ts}.html"
  meta_output_file_name="collection_meta_${ts}.txt"

  tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/heap-snapshot.XXXXXX")"
  staging_dir="$tmp_root/staging/collect_heap_snapshot"
  mkdir -p "$staging_dir"
  cleanup_cmd() {
    rm -rf "$tmp_root"
  }
  trap cleanup_cmd EXIT

  host_name="$(hostname 2>/dev/null || echo unknown)"
  now_utc="$(date -u +"%Y-%m-%dT%H:%M:%SZ" 2>/dev/null || date)"
  curl_host_used="$host_ip"

  tserver_heap_current_by_count_path="$staging_dir/$tserver_heap_current_by_count_output_file_name"
  tserver_heap_peak_by_count_path="$staging_dir/$tserver_heap_peak_by_count_output_file_name"
  tserver_heap_current_by_estimated_bytes_path=\
"$staging_dir/$tserver_heap_current_by_estimated_bytes_output_file_name"
  master_heap_current_by_count_path="$staging_dir/$master_heap_current_by_count_output_file_name"
  meta_path="$staging_dir/$meta_output_file_name"

  collect_heap_snapshot_fetch_all \
    "$curl_host_used" \
    "$tserver_http_port" \
    "$master_http_port" \
    "$collect_tserver" \
    "$collect_master" \
    "$tserver_heap_current_by_count_path" \
    "$tserver_heap_peak_by_count_path" \
    "$tserver_heap_current_by_estimated_bytes_path" \
    "$master_heap_current_by_count_path" \
    "$curl_connect_timeout" \
    "$curl_max_time"

  success_count="$(collect_heap_snapshot_success_count)"
  tserver_base_url="http://${curl_host_used}:${tserver_http_port}"
  master_base_url="http://${curl_host_used}:${master_http_port}"

  {
    echo "host=$host_name"
    echo "collected_at_utc=$now_utc"
    echo "host_ip=$host_ip"
    echo "tserver_http_port=$tserver_http_port"
    echo "master_http_port=$master_http_port"
    echo "collect_master=$collect_master"
    echo "collect_tserver=$collect_tserver"
    echo "tserver_base_url=$tserver_base_url"
    echo "master_base_url=$master_base_url"
    echo "tserver_heap_current_by_count_exit_status=$TSERVER_HEAP_CURRENT_BY_COUNT_STATUS"
    echo "tserver_heap_peak_by_count_exit_status=$TSERVER_HEAP_PEAK_BY_COUNT_STATUS"
    echo "tserver_heap_current_by_estimated_bytes_exit_status="\
"$TSERVER_HEAP_CURRENT_BY_ESTIMATED_BYTES_STATUS"
    echo "master_heap_current_by_count_exit_status=$MASTER_HEAP_CURRENT_BY_COUNT_STATUS"
    echo "tserver_skipped=$TSERVER_HEAP_SKIPPED"
    echo "master_skipped=$MASTER_HEAP_SKIPPED"
    echo "successful_endpoints=$success_count"
  } > "$meta_path"

  create_bundle_tar_or_fail "$remote_tar_path" "$tmp_root/staging"
  echo "BUNDLE_STATUS=OK"
  echo "BUNDLE_PATH=$remote_tar_path"
  if [ "$success_count" -eq 0 ]
  then
    echo "Created heap snapshot bundle at $remote_tar_path (no HTTP 200 responses;" \
      "see collection_meta and endpoint files)"
  else
    echo "Created heap snapshot bundle at $remote_tar_path"
  fi
}

# Failure reasons here, and in require_yb_dump_cdc_state_bin, go to stderr: when a command exits
# non-zero, YBA shows only its stderr, on both node-agent and SSH (NodeAgentClient.getResponse,
# ShellProcessHandler), so a reason printed to stdout never reaches the YBA log. The markers stay
# on stdout, which is what YBA parses when the command succeeds.
collect_cdc_state() {
  remote_tar_path=""
  host_ip=""
  tserver_rpc_port=""
  yb_home_dir=""
  certs_dir_name=""

  while [ "$#" -gt 0 ]
  do
    case "$1" in
      --remote_tar_path)
        remote_tar_path=$2
        shift 2
        ;;
      --host_ip)
        host_ip=$2
        shift 2
        ;;
      --tserver_rpc_port)
        tserver_rpc_port=$2
        shift 2
        ;;
      --yb_home_dir)
        yb_home_dir=$2
        shift 2
        ;;
      --certs_dir_name)
        certs_dir_name=$2
        shift 2
        ;;
      --mount_path)
        shift 2
        ;;
      --node_home_dir)
        shift 2
        ;;
      *)
        echo "Unknown argument: $1" >&2
        echo "BUNDLE_STATUS=FAILED"
        exit 1
        ;;
    esac
  done

  if [ -z "$host_ip" ]
  then
    host_ip="$(hostname -I 2>/dev/null | awk '{print $1}')"
  fi

  if [ -z "$remote_tar_path" ] || [ -z "$host_ip" ] || [ -z "$tserver_rpc_port" ] \
      || [ -z "$yb_home_dir" ]
  then
    echo "Missing required args for collect_cdc_state" >&2
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  if ! [[ "$tserver_rpc_port" =~ ^[0-9]+$ ]] || [ "$tserver_rpc_port" -le 0 ]
  then
    echo "Invalid tserver_rpc_port: $tserver_rpc_port" >&2
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  remote_work_dir="${remote_tar_path%.tar.gz}-work"
  dump_dir="${remote_work_dir}/dump"

  ts="$(support_bundle_filename_timestamp)"
  output_file_name="cdc_state.csv"
  meta_output_file_name="collection_meta_${ts}.txt"

  tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/cdc-state.XXXXXX")"
  staging_dir="$tmp_root/staging/collect_cdc_state"
  mkdir -p "$staging_dir"
  output_path="$staging_dir/$output_file_name"
  meta_path="$staging_dir/$meta_output_file_name"

  cleanup_cmd() {
    rm -rf "$tmp_root"
    rm -rf "$remote_work_dir"
  }
  trap cleanup_cmd EXIT

  # Its own --yb_home_dir serves a run without the leading flag.
  YB_HOME_DIR=${YB_HOME_DIR:-$yb_home_dir}
  require_yb_dump_cdc_state_bin
  dump_bin="$YB_DUMP_CDC_STATE_BIN"

  mkdir -p "$dump_dir"

  host_name="$(hostname 2>/dev/null || echo unknown)"
  now_utc="$(date -u +"%Y-%m-%dT%H:%M:%SZ" 2>/dev/null || date)"
  tserver_address="${host_ip}:${tserver_rpc_port}"

  dump_args=(
    --tserver_address "$tserver_address"
    --output "$output_path"
    --dump_dir "$dump_dir"
    --bin_dir "${yb_home_dir}/tserver/bin"
  )
  # A missing directory would fail yb-ts-cli even where the tserver still accepts plaintext.
  certs_dir_used=none
  if [ -n "$certs_dir_name" ] && [ -d "$certs_dir_name" ]
  then
    dump_args+=(--certs_dir_name "$certs_dir_name")
    certs_dir_used=$certs_dir_name
  elif [ -n "$certs_dir_name" ]
  then
    echo "collect_cdc_state: $certs_dir_name not found; connecting without TLS" >&2
  fi

  "$dump_bin" "${dump_args[@]}"
  binary_exit=$?

  row_count=0
  if [ -f "$output_path" ]
  then
    row_count=$(tail -n +2 "$output_path" 2>/dev/null | grep -c . || true)
  fi

  {
    echo "host=$host_name"
    echo "collected_at_utc=$now_utc"
    echo "host_ip=$host_ip"
    echo "tserver_rpc_port=$tserver_rpc_port"
    echo "tserver_address=$tserver_address"
    echo "yb_home_dir=$yb_home_dir"
    echo "certs_dir_name=$certs_dir_used"
    echo "remote_work_dir=$remote_work_dir"
    echo "dump_binary_path=$dump_bin"
    echo "binary_exit_status=$binary_exit"
    echo "row_count=$row_count"
  } > "$meta_path"

  if [ "$binary_exit" -ne 0 ]
  then
    echo "collect_cdc_state: yb-dump-cdc-state failed with exit $binary_exit" >&2
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  if [ "$row_count" -eq 0 ]
  then
    create_bundle_tar_or_fail "$remote_tar_path" "$tmp_root/staging"
    echo "BUNDLE_STATUS=EMPTY"
    echo "BUNDLE_PATH=$remote_tar_path"
    echo "Created cdc_state bundle at $remote_tar_path (no cdc_state rows on this node)"
    exit 0
  fi

  create_bundle_tar_or_fail "$remote_tar_path" "$tmp_root/staging"
  echo "BUNDLE_STATUS=OK"
  echo "BUNDLE_PATH=$remote_tar_path"
  echo "Created cdc_state bundle at $remote_tar_path ($row_count rows)"
}

# Passed when the command doesn't run under yb-sb-limits-exec.sh, which bounds it otherwise. exec
# keeps the pid, so node agent's group kill still reaches it; without a usable timeout, runs as is.
if [ "${1:-}" = "--run_timeout_secs" ] && [ "$#" -ge 2 ]; then
  run_timeout_secs=$2
  shift 2
  if [[ "$run_timeout_secs" =~ ^[1-9][0-9]*$ ]] && [ -f "${BASH_SOURCE[0]}" ] &&
    timeout -k 1 5 true >/dev/null 2>&1; then
    exec timeout -k 5 "$run_timeout_secs" bash "${BASH_SOURCE[0]}" "$@"
  fi
fi

if [ "${1:-}" = "--yb_home_dir" ] && [ "$#" -ge 2 ]; then
  export YB_HOME_DIR=$2
  shift 2
fi

# The bundle's own directory on the node, which YBA removes when the bundle ends, so a killed run's
# work directories don't outlive it. Left alone when this user can't write there (SystemLogs).
if [ "${1:-}" = "--tmp_dir" ] && [ "$#" -ge 2 ]; then
  if [ -d "$2" ] && [ -w "$2" ]; then
    export TMPDIR=$2
  fi
  shift 2
fi

# Dispatch only to a function defined above. YBA pins params[0] to the manifest entrypoint; this
# keeps any other first argument from being run as a command.
if [ "$#" -gt 0 ]; then
  if ! declare -F -- "$1" >/dev/null; then
    echo "Unknown function: $1" >&2
    exit 2
  fi
  "$@"
fi
