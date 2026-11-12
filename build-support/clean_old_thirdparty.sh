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
# Removes the versions in a thirdparty cache that the local branches and worktrees of this
# repository are not expected to need. Each of them is attributed to the origin branch it is based
# on: origin/master or a release branch such as origin/2026.1, whichever shares the newest commit
# with it, or a hotfix branch of that release branch, such as origin/2026.1.2, that shares a newer
# one. The versions pinned by the --keep most recent versions of the thirdparty_archives*.yml files
# in build-support on those origin branches are kept. So are the versions that worktree HEADs pin,
# since they are being built, those of local commits that change the pins themselves, and the
# versions that the build/latest of each worktree was built with. All other versions are removed,
# so a stale local branch keeps only its base branch's current pins, and deleting the last local
# branch based on a release branch lets that branch's versions go. Other build roots, e.g. old
# sanitizer builds, lose their version once it is no longer pinned.
#
# The cache is shared by all checkouts on the machine, but only this repository's branches and
# worktrees count, so the versions that only a separate clone uses are removed too.
#
# A removed version is downloaded again by the next build that needs it, so do not run this while
# builds use the versions it removes. Symbolic links, e.g. those of thirdparty installed from a PR
# artifact, and the <name>.tmp.* directories of downloads in progress are left alone.
#
# Usage:
#   build-support/clean_old_thirdparty.sh [--dir DIR] [--keep N] [--apply]
#
# Options:
#   --dir DIR    Thirdparty cache directory. Default: /opt/yb-build/thirdparty
#   --keep N     Keep the versions of the N most recent pins of each base branch (default: 1).
#   --apply      Actually delete. Without this flag the script only prints what it would delete
#                (dry-run is the default for safety).
#   -h, --help   Show this help.

set -euo pipefail

target_dir=/opt/yb-build/thirdparty
apply=false
keep=1

usage() {
  sed -n '/^# Usage:/,/^$/ p' "$0" | sed 's/^# \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dir)
      target_dir="$2"; shift 2 ;;
    --keep)
      keep="$2"; shift 2 ;;
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
  echo "Directory not found: $target_dir" >&2
  exit 1
fi

if ! [[ "$keep" =~ ^[1-9][0-9]*$ ]]; then
  echo "--keep must be a positive integer (got: $keep)" >&2
  exit 2
fi

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
# Builds concatenate the archives of both files.
pins_files=(build-support/thirdparty_archives.yml build-support/thirdparty_archives_manual.yml)

git_repo() {
  git -C "$repo_dir" "$@"
}

# Prints the sorted tags that a commit pins.
pins_at() {
  local file
  for file in "${pins_files[@]}"; do
    git_repo show "$1:$file" 2>/dev/null | sed -n 's/^ *tag: *//p' || true
  done | sort
}

# Prints the commit time of the merge base of two commits, if they have one.
merge_base_time() {
  local merge_base
  if merge_base=$(git_repo merge-base "$1" "$2" 2>/dev/null); then
    git_repo show -s --format=%ct "$merge_base"
  fi
}

release_branches=$(git_repo for-each-ref --format='%(refname:short)' refs/remotes/origin \
  | grep -E '^origin/(master|[0-9]+\.[0-9]+)$' || true)
# Without base branches only the worktree pins would be kept.
if ! grep -qx origin/master <<< "$release_branches"; then
  echo "No origin/master in $repo_dir, so the base branches cannot be found" >&2
  exit 1
fi

# Prints the origin branch that a commit is based on, if any. Hotfix branches are compared only
# with their own release branch, to keep the number of merge-base calls small.
base_of() {
  local commit=$1 base= base_time=-1 candidate time
  for candidate in $release_branches; do
    time=$(merge_base_time "$commit" "$candidate")
    # On a tie the commit predates the cut of the release branch, so it is based on master.
    if [[ -n $time ]] &&
       { (( time > base_time )) ||
         { (( time == base_time )) && [[ $candidate == origin/master ]]; }; }; then
      base=$candidate
      base_time=$time
    fi
  done
  if [[ -n $base && $base != origin/master ]]; then
    for candidate in $(git_repo for-each-ref --format='%(refname:short)' "refs/remotes/$base.*"); do
      time=$(merge_base_time "$commit" "$candidate")
      if [[ -n $time ]] && (( time > base_time )); then
        base=$candidate
        base_time=$time
      fi
    done
  fi
  echo "$base"
}

worktree_heads=$(git_repo worktree list --porcelain | sed -n 's/^HEAD //p' | sort -u)
local_commits=$(
  { git_repo for-each-ref --format='%(objectname)' refs/heads; echo "$worktree_heads"; } \
    | sort -u | grep . || true
)

echo "Finding the base branches of $(grep -c . <<< "$local_commits" || true) local branch and" \
     "worktree commits of $repo_dir..."
bases=
own_pins_commits=
for commit in $local_commits; do
  base=$(base_of "$commit")
  if [[ -n $base ]]; then
    bases+="$base"$'\n'
  fi
  if grep -Fxq -- "$commit" <<< "$worktree_heads" ||
     { [[ -n $base ]] &&
       [[ -n $(git_repo log -1 --format=%H "$base..$commit" -- "${pins_files[@]}") ]]; }; then
    own_pins_commits+="$commit"$'\n'
  fi
done

# A thirdparty version is installed as yugabyte-db-thirdparty-<tag>, with a <tag> of the pins files.
# Reverts repeat older versions of the files, so only distinct versions count.
pinned_tags=$(
  {
    for base in $(sort -u <<< "$bases"); do
      taken=0
      seen=
      for commit in $(git_repo log --format=%H "$base" -- "${pins_files[@]}"); do
        tags=$(pins_at "$commit")
        key=$(tr '\n' ' ' <<< "$tags")
        if grep -Fxq -- "$key" <<< "$seen"; then
          continue
        fi
        seen+="$key"$'\n'
        echo "$tags"
        taken=$((taken + 1))
        if (( taken == keep )); then
          break
        fi
      done
    done
    for commit in $own_pins_commits; do
      pins_at "$commit"
    done
    # A build root links its binaries to the version in its thirdparty_path.txt, which differs from
    # the pins after a pull until the next build.
    git_repo worktree list --porcelain | sed -n 's/^worktree //p' | while IFS= read -r worktree; do
      if [[ -f $worktree/build/latest/thirdparty_path.txt ]]; then
        basename "$(< "$worktree/build/latest/thirdparty_path.txt")"
      fi
    done | sed -n 's/^yugabyte-db-thirdparty-//p'
  } | sort -u | grep . || true
)

# Without pins every version would be removed.
if [[ -z $pinned_tags ]]; then
  echo "Found no pinned thirdparty versions in $repo_dir, not removing anything" >&2
  exit 1
fi

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
  elif grep -Fxq -- "${entry#yugabyte-db-thirdparty-}" <<< "$pinned_tags"; then
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

echo
echo "Base branches, with the number of commits based on each:"
{ grep . <<< "$bases" | sort | uniq -c | sort -rn | awk '{printf "  %s (%s)\n", $2, $1}'; } || true
echo
echo "Scanning $target_dir, keeping the versions of the $keep most recent pins of each, the pins" \
     "of $(grep -c . <<< "$own_pins_commits" || true) worktree HEADs and commits that change the" \
     "pins, and the versions of the worktrees' build/latest..."
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
for entry in ${leftovers[@]+"${leftovers[@]}"}; do
  check_name "$entry"
  rm -rf -- "./$entry"
  echo "  removed $entry"
done
# Rename first: builds take any existing directory, even a half-deleted one, as installed.
for entry in ${victims[@]+"${victims[@]}"}; do
  check_name "$entry"
  # Leftover directories are gone, so this is a symbolic link or a file, which mv would follow or
  # trip over.
  if [[ -e ./$entry.removing || -L ./$entry.removing ]]; then
    echo "Refusing to rename $entry onto the existing $entry.removing" >&2
    exit 3
  fi
  mv -- "./$entry" "./$entry.removing"
  rm -rf -- "./$entry.removing"
  echo "  removed $entry"
done
echo "Done."
