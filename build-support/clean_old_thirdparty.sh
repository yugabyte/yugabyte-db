#!/usr/bin/env bash
# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#
# Removes the thirdparty directories, one per installed thirdparty version, that no build or test
# has used for --days days, from their parent directory.
# A version counts as used when the access time of any of its files falls within that time.
# On Linux, access times show the reads, so the script refuses to run on a noatime filesystem.
# APFS, unless mounted with strictatime, updates an access time only when it is older than the
# modification time. So on macOS, yb_build.sh touches the version's .last_used file instead, and a
# version used only outside yb_build.sh, e.g. from CLion, or only by branches without that, counts
# as used only for --days days after install.
#
# Every yugabyte-db-thirdparty-v* directory counts, including copies renamed by hand. Symbolic
# links, e.g. those of thirdparty installed from a PR artifact, and the <name>.tmp.* directories of
# downloads in progress are left alone.
#
# Usage:
#   build-support/clean_old_thirdparty.sh [--dir DIR] [--days N] [--apply]
#
# Options:
#   --dir DIR    Parent of the thirdparty directories. Default: /opt/yb-build/thirdparty
#   --days N     Remove the versions unused for N days (default: 30).
#   --apply      Delete; without it, only print what would be deleted.
#   -h, --help   Show this help.

set -euo pipefail

# shellcheck source=build-support/common-build-env.sh
. "${BASH_SOURCE[0]%/*}/common-build-env.sh"

target_dir=$LOCAL_THIRDPARTY_DIR_PARENT
apply=false
days=30

usage() {
  sed -n '/^# Usage:/,/^$/ p' "$0" | sed 's/^# \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
  if [[ ($1 == --dir || $1 == --days) && $# -lt 2 ]]; then
    echo "$1 needs a value" >&2
    usage >&2
    exit 2
  fi
  case "$1" in
    --dir)
      target_dir="$2"; shift 2 ;;
    --days)
      days="$2"; shift 2 ;;
    --apply)
      apply=true; shift ;;
    -h|--help)
      usage; exit 0 ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2 ;;
  esac
done

if [[ ! -d "$target_dir" ]]; then
  echo "Thirdparty directory parent not found: $target_dir" >&2
  exit 1
fi

if ! [[ "$days" =~ ^[1-9][0-9]*$ ]]; then
  echo "--days must be a positive integer (got: $days)" >&2
  exit 2
fi

# Use -amin rather than -atime, since BSD find rounds ages up to whole days, so -atime -N would
# match only files used within N-1 days.
minutes=$(( days * 24 * 60 ))

if is_linux; then
  mount_options=$(findmnt -n -o OPTIONS -T "$target_dir")
  if [[ ,$mount_options, == *,noatime,* ]]; then
    echo "$target_dir is on a noatime filesystem, so the versions in use cannot be told apart" >&2
    exit 1
  fi
  # relatime, the default, lets access times lag reads by up to a day.
  minutes=$(( minutes + 24 * 60 ))
fi

# Treat a find error as used, to be safe.
recently_used() {
  local found
  found=$(find "$1" -type f -amin "-$minutes" -print -quit) || return 0
  [[ -n $found ]]
}

cd "$target_dir"

shopt -s nullglob
keepers=()
victims=()
leftovers=()
for entry in yugabyte-db-thirdparty-v*; do
  if [[ -L $entry || ! -d $entry || $entry == *.tmp.* ]]; then
    continue
  elif [[ $entry == *.removing ]]; then
    leftovers+=("$entry")
  elif recently_used "$entry"; then
    keepers+=("$entry")
  else
    victims+=("$entry")
  fi
done

print_list() {
  local title=$1
  shift
  if [[ $# -gt 0 ]]; then
    echo "$title"
    printf '  %s\n' "$@"
    echo
  fi
}

echo "Scanning $target_dir for versions unused for $days days..."
echo
# ${a[@]+"${a[@]}"} expands an empty array under set -u in bash 3.2 too.
print_list "Will keep:" ${keepers[@]+"${keepers[@]}"}
print_list "Will remove leftovers of interrupted removals:" ${leftovers[@]+"${leftovers[@]}"}
print_list "Will remove:" ${victims[@]+"${victims[@]}"}

if [[ ${#victims[@]} -eq 0 && ${#leftovers[@]} -eq 0 ]]; then
  echo "Nothing to remove."
  exit 0
fi

if ! $apply; then
  echo "(dry run) Re-run with --apply to delete the entries listed above."
  exit 0
fi

# Never let a bad name reach rm -rf.
check_name() {
  if [[ $1 != yugabyte-db-thirdparty-v* || $1 == */* || $1 == *..* ]]; then
    echo "Refusing to remove suspicious entry: $1" >&2
    exit 3
  fi
}

echo "Removing $(( ${#leftovers[@]} + ${#victims[@]} )) entries..."
failed=0
for entry in ${leftovers[@]+"${leftovers[@]}"}; do
  check_name "$entry"
  if rm -rf -- "./$entry"; then
    echo "  removed $entry"
  else
    echo "  failed to remove $entry" >&2
    failed=1
  fi
done
# Rename first: builds take any existing directory, even a half-deleted one, as installed.
for entry in ${victims[@]+"${victims[@]}"}; do
  check_name "$entry"
  # A build may have started to use it since the scan.
  if recently_used "$entry"; then
    echo "  kept $entry, which is in use now"
    continue
  fi
  # mv would move it into a leftover that failed to be removed, or follow a symbolic link.
  if [[ -e ./$entry.removing || -L ./$entry.removing ]]; then
    echo "  skipped $entry, since $entry.removing exists" >&2
    failed=1
    continue
  fi
  if mv -- "./$entry" "./$entry.removing" && rm -rf -- "./$entry.removing"; then
    echo "  removed $entry"
  else
    echo "  failed to remove $entry" >&2
    failed=1
  fi
done
if (( failed )); then
  exit 1
fi
echo "Done."
