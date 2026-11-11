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
# Removes old yugabyte-db-thirdparty-* directories from a thirdparty cache, keeping only the
# newest ones of each release and kind, e.g. master's macos-arm64-clang21 and 2025.2's
# almalinux8-x86_64-clang21-full-lto.
#
# Directory names are expected to be
#     yugabyte-db-thirdparty-v[<release>-]<YYYYMMDDHHMMSS>-<git_hash>-<kind>
# where <release> (e.g. 2025.2) is present for release branches. Other entries are left alone,
# except the <name>.removing leftovers of an interrupted --apply.
#
# A removed version is downloaded again by the next build that needs it, so do not run this while
# builds use the versions it removes.
#
# Usage:
#   build-support/clean_old_thirdparty.sh [--dir DIR] [--apply] [--keep N]
#
# Options:
#   --dir DIR    Directory containing yugabyte-db-thirdparty-* entries.
#                Default: /opt/yb-build/thirdparty
#   --apply      Actually delete. Without this flag the script only prints what it would delete
#                (dry-run is the default for safety).
#   --keep N     Keep the N newest entries per release and kind (default: 1).
#   -h, --help   Show this help.

set -euo pipefail

DEFAULT_DIR="/opt/yb-build/thirdparty"
target_dir="$DEFAULT_DIR"
apply=false
keep=1

usage() {
  sed -n '/^# Usage:/,/^$/ p' "$0" | sed 's/^# \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dir)
      target_dir="$2"; shift 2 ;;
    --apply)
      apply=true; shift ;;
    --keep)
      keep="$2"; shift 2 ;;
    -h|--help)
      usage; exit 0 ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2 ;;
  esac
done

if [[ ! -d "$target_dir" ]]; then
  echo "Directory not found: $target_dir" >&2
  exit 1
fi

if ! [[ "$keep" =~ ^[1-9][0-9]*$ ]]; then
  echo "--keep must be a positive integer (got: $keep)" >&2
  exit 2
fi

cd "$target_dir"

# Entries as "<release>/<kind> TAB <timestamp> TAB <name>", where the fixed-width timestamp sorts
# chronologically. A glob rather than GNU find -printf, so that this also works on macOS. The
# <name>.tmp.* directories of downloads in progress are left alone.
shopt -s nullglob
entries=()
leftovers=()
skipped=()
for entry in yugabyte-db-thirdparty-v*/; do
  entry=${entry%/}
  if [[ $entry =~ ^yugabyte-db-thirdparty-v(([0-9.]+)-)?([0-9]{14})-[0-9a-f]+-([A-Za-z0-9_-]+)$ ]]
  then
    entries+=("${BASH_REMATCH[2]}/${BASH_REMATCH[4]}"$'\t'"${BASH_REMATCH[3]}"$'\t'"$entry")
  elif [[ $entry == *.removing ]]; then
    leftovers+=("$entry")
  elif [[ $entry != *.tmp.* ]]; then
    skipped+=("$entry")
  fi
done

# Sorting puts the newest entry last within each release and kind; the last `keep` are kept.
victims=()
keepers=()
if [[ ${#entries[@]} -gt 0 ]]; then
  while IFS= read -r line; do
    case "$line" in
      KEEP$'\t'*)   keepers+=("${line#KEEP$'\t'}") ;;
      DELETE$'\t'*) victims+=("${line#DELETE$'\t'}") ;;
    esac
  done < <(
    printf '%s\n' "${entries[@]}" \
      | LC_ALL=C sort -t$'\t' -k1,1 -k2,2 \
      | awk -F'\t' -v keep="$keep" '
          function flush_group(   i, n, start) {
            n = idx;
            start = (n > keep) ? (n - keep) : 0;
            for (i = 0; i < n; i++) {
              if (i >= start) print "KEEP\t"   names[i];
              else            print "DELETE\t" names[i];
            }
          }
          {
            if (NR > 1 && $1 != prev_kind) {
              flush_group();
              idx = 0;
              delete names;
            }
            names[idx++] = $3;
            prev_kind = $1;
          }
          END { if (NR > 0) flush_group() }
        '
  )
fi

# The ${a[@]+...} form expands an empty array under set -u in bash 3.2 too.
print_list() {
  local title=$1
  shift
  if [[ $# -gt 0 ]]; then
    echo "$title"
    printf '  %s\n' "$@"
    echo
  fi
}

echo "Scanning $target_dir (keeping $keep newest per release and kind)..."
echo
print_list "Leaving alone entries with unexpected names:" ${skipped[@]+"${skipped[@]}"}
print_list "Will keep:" ${keepers[@]+"${keepers[@]}"}
print_list "Will remove leftovers of interrupted removals:" ${leftovers[@]+"${leftovers[@]}"}
print_list "Will remove:" ${victims[@]+"${victims[@]}"}

if [[ ${#victims[@]} -eq 0 && ${#leftovers[@]} -eq 0 ]]; then
  if [[ ${#entries[@]} -eq 0 ]]; then
    echo "No thirdparty versions with recognized names found in $target_dir"
  else
    echo "Nothing to remove."
  fi
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
for entry in ${leftovers[@]+"${leftovers[@]}"}; do
  check_name "$entry"
  rm -rf -- "./$entry"
  echo "  removed $entry"
done
# Builds take an existing directory for an installed version, so each one is renamed first: an
# interrupted removal leaves only a <name>.removing directory, which the next run removes.
for entry in ${victims[@]+"${victims[@]}"}; do
  check_name "$entry"
  mv -- "./$entry" "./$entry.removing"
  rm -rf -- "./$entry.removing"
  echo "  removed $entry"
done
echo "Done."
