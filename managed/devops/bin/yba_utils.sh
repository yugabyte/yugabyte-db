#!/bin/bash
#
# Copyright 2026 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

create_bundle_tar_as_current_user() {
  local_tar_path=$1
  staging_dir=$2

  mkdir -p "$(dirname "$local_tar_path")"
  tar --warning=no-file-changed -czf "$local_tar_path" -C "$staging_dir" .
}

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

estimate_application_logs_size() {
  SUPPORT_BUNDLE_ESTIMATE_ONLY=1 create_application_logs_bundle "$@"
}

# Collect YBA application logs on the platform host (v1 ApplicationLogsComponent parity).
create_application_logs_bundle() {
  log_dir=""
  start_date=""
  end_date=""
  local_tar_path=""
  application_logs_regex=""
  application_logs_sdf=""

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
      --remote_tar_path|--local_tar_path)
        local_tar_path=$2
        shift 2
        ;;
      --application_logs_regex)
        application_logs_regex=$2
        shift 2
        ;;
      --application_logs_sdf)
        application_logs_sdf=$2
        shift 2
        ;;
      *)
        echo "Unknown argument: $1"
        echo "BUNDLE_STATUS=FAILED"
        exit 1
        ;;
    esac
  done

  if [ -z "$log_dir" ] || [ -z "$start_date" ] || [ -z "$end_date" ] || [ -z "$local_tar_path" ]
  then
    echo "Missing required args for create_application_logs_bundle"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  if [ -z "$application_logs_regex" ]
  then
    application_logs_regex='application-log-[0-9]{4}-[0-9]{2}-[0-9]{2}(\.gz)?'
  fi
  if [ -z "$application_logs_sdf" ]
  then
    application_logs_sdf="'application-log-'yyyy-MM-dd"
  fi

  if [ ! -d "$log_dir" ]
  then
    echo "Log directory does not exist: $log_dir"
    echo "BUNDLE_STATUS=EMPTY"
    exit 0
  fi

  python_bin="python3"
  if ! command -v "$python_bin" >/dev/null 2>&1
  then
    python_bin="python"
  fi
  if ! command -v "$python_bin" >/dev/null 2>&1
  then
    echo "Python is required to filter application logs by date"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  tmp_root="$(mktemp -d)"
  cleanup_cmd() {
    rm -rf "$tmp_root"
  }
  # Runs on YBA's SIGTERM at abort. A timeout is a SIGKILL, so YBA also points TMPDIR at a
  # directory it deletes itself.
  trap cleanup_cmd EXIT
  staging_dir="$tmp_root/staging/application_logs"
  mkdir -p "$staging_dir"
  file_list_path="$tmp_root/files.txt"

  if ! "$python_bin" - "$log_dir" "$start_date" "$end_date" \
      "$application_logs_regex" "$application_logs_sdf" "$file_list_path" <<'PY'
import datetime as dt
import re
import sys
from pathlib import Path

log_dir = Path(sys.argv[1])
start_raw = sys.argv[2].strip()
end_raw = sys.argv[3].strip()
regex_pattern = sys.argv[4]
sdf_pattern = sys.argv[5]
output_path = Path(sys.argv[6])

def parse_bound(raw: str) -> dt.datetime:
    raw = raw.strip()
    layouts = (
        "%Y-%m-%dT%H:%M:%SZ",
        "%Y-%m-%dT%H:%M:%S",
        "%Y-%m-%d %H:%M:%S",
        "%Y-%m-%d",
    )
    for layout in layouts:
        try:
            return dt.datetime.strptime(raw, layout)
        except ValueError:
            continue
    raise ValueError(f"invalid date {raw!r}")

def parse_end_bound(raw: str) -> dt.datetime:
    parsed = parse_bound(raw)
    if re.fullmatch(r"\d{4}-\d{2}-\d{2}", raw.strip()):
        return parsed.replace(hour=23, minute=59, second=59, microsecond=999999)
    return parsed

def truncate_to_day(value: dt.datetime) -> dt.datetime:
    return value.replace(hour=0, minute=0, second=0, microsecond=0)

def java_simple_date_format_to_strptime(pattern: str) -> str:
    # Minimal mapping for application log SDF patterns used by YBA.
    mapping = {
        "yyyy": "%Y",
        "MM": "%m",
        "dd": "%d",
        "HH": "%H",
        "mm": "%M",
        "ss": "%S",
    }
    result = []
    i = 0
    while i < len(pattern):
        if pattern[i] == "'":
            end = pattern.find("'", i + 1)
            if end == -1:
                raise ValueError(f"invalid sdf pattern {pattern!r}")
            result.append(pattern[i + 1 : end])
            i = end + 1
            continue
        matched = False
        for token in ("yyyy", "MM", "dd", "HH", "mm", "ss"):
            if pattern.startswith(token, i):
                result.append(mapping[token])
                i += len(token)
                matched = True
                break
        if not matched:
            result.append(pattern[i])
            i += 1
    return "".join(result)

start_date = truncate_to_day(parse_bound(start_raw))
end_date = parse_end_bound(end_raw)
# UTC, because --start_date and --end_date are UTC instants: YBA formats them with
# UNIVERSE_LOGS_INSTANT_FORMAT, which is withZone(ZoneOffset.UTC). Reading the local clock here
# compared two different clocks, and on a host east of UTC local midnight arrives first -- so
# between local midnight and UTC midnight this marker landed past end_date and application.log,
# the live log an operator most wants, was silently left out of the bundle. tzinfo is dropped so
# the value stays naive like the parsed bounds it is compared against.
today = truncate_to_day(dt.datetime.now(dt.timezone.utc).replace(tzinfo=None))

def is_between(check: dt.datetime, start: dt.datetime, end: dt.datetime) -> bool:
    return start <= check <= end

selected = []
if (log_dir / "application.log").is_file() and is_between(today, start_date, end_date):
    selected.append(log_dir / "application.log")

compiled = re.compile(regex_pattern)
strptime_pattern = java_simple_date_format_to_strptime(sdf_pattern)

for entry in sorted(log_dir.iterdir()):
    if not entry.is_file():
        continue
    name = entry.name
    if name == "application.log":
        continue
    if not compiled.fullmatch(name):
        continue
    stem = name[:-3] if name.endswith(".gz") else name
    try:
        file_date = dt.datetime.strptime(stem, strptime_pattern)
    except ValueError:
        continue
    if is_between(file_date, start_date, end_date):
        selected.append(entry)

with output_path.open("w", encoding="utf-8") as handle:
    for path in selected:
        handle.write(f"{path}\n")
PY
  then
    rm -rf "$tmp_root"
    echo "Failed to filter application logs"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi

  if [ ! -s "$file_list_path" ]
  then
    rm -rf "$tmp_root"
    echo "No application logs matched the requested date range."
    echo "BUNDLE_STATUS=EMPTY"
    exit 0
  fi

  if [ "${SUPPORT_BUNDLE_ESTIMATE_ONLY:-}" = "1" ]
  then
    total=0
    while IFS= read -r source_file
    do
      if [ -n "$source_file" ] && [ -f "$source_file" ]
      then
        total=$((total + $(_byte_size_for_path "$source_file")))
      fi
    done < "$file_list_path"
    rm -rf "$tmp_root"
    _emit_estimate_result "$total"
    exit 0
  fi

  while IFS= read -r source_file
  do
    if [ -z "$source_file" ] || [ ! -f "$source_file" ]
    then
      continue
    fi
    dest_file="$staging_dir/$(basename "$source_file")"
    if ! err=$(cp -Lp "$source_file" "$dest_file" 2>&1)
    then
      rm -f -- "$dest_file"
      echo "COPY_FAILURE=${source_file}: ${err//$'\n'/; }"
    fi
  done < "$file_list_path"

  if [ -z "$(find "$staging_dir" -type f -print -quit)" ]
  then
    rm -rf "$tmp_root"
    echo "No application logs copied into bundle staging directory."
    echo "BUNDLE_STATUS=EMPTY"
    exit 0
  fi

  if ! create_bundle_tar_as_current_user "$local_tar_path" "$tmp_root/staging"
  then
    rm -rf "$tmp_root"
    rm -f "$local_tar_path"
    echo "Failed to create application logs bundle tar at $local_tar_path"
    echo "BUNDLE_STATUS=FAILED"
    exit 1
  fi
  rm -rf "$tmp_root"
  echo "BUNDLE_STATUS=OK"
  echo "BUNDLE_PATH=$local_tar_path"
  echo "Created application logs bundle at $local_tar_path"
}

# Dispatch only to a function defined above. YBA pins params[0] to the manifest entrypoint; this
# keeps any other first argument from being run as a command.
if [ "$#" -gt 0 ]; then
  if ! declare -F -- "$1" >/dev/null; then
    echo "Unknown function: $1" >&2
    exit 2
  fi
  "$@"
fi
