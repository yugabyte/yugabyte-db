#!/usr/bin/env bash
#
# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License. You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.
#
# Common variables/functions for linters.

export LC_ALL=C

# Every linter script sources this file with the linted path as $1 before
# touching the file.  A symlink's target is linted under the target's own
# path, so skip symlinks rather than lint the same content again under the
# symlink's name.
if [ -L "$1" ]; then
  exit 0
fi

# Sets the global $merge_base variable to the divergence point between HEAD and
# its upstream tracking branch. If no upstream tracking branch is configured,
# emits a warning and returns 1.
set_merge_base() {
  local upstream branch
  upstream=$(git rev-parse --abbrev-ref --symbolic-full-name @{upstream})

  if [ -n "$upstream" ]; then
    merge_base=$(git merge-base HEAD "${upstream}" || git rev-parse HEAD)
    return
  fi

  branch=$(git rev-parse --abbrev-ref HEAD)
  echo "warning:upstream_not_configured:\
Branch '${branch}' has no upstream tracking branch and committed changes \
will not be linted. Fix by running \
git branch --set-upstream-to=origin/<target-branch> ${branch}::"
  return 1
}

check_ctags() {
  if ! which ctags >/dev/null || \
     ! grep -q "Exuberant Ctags" <<<"$(ctags --version)"; then
    echo "Please install Exuberant Ctags" >/dev/stderr
    if which dnf >/dev/null; then
      echo "HINT: dnf install ctags" >/dev/stderr
    elif which brew >/dev/null; then
      echo "HINT: brew install ctags" >/dev/stderr
    elif which apt >/dev/null; then
      echo "HINT: apt install exuberant-ctags" >/dev/stderr
    fi
    return 1
  fi
}

# Wrappers for the git commands these linters parse.

# Diff a commit against the working tree.  diff-index is a plumbing command, so
# unlike git diff its output is not reshaped by color.ui, color.diff,
# diff.external, or diff.mnemonicPrefix.  It does not detect renames, so a
# renamed file shows up as an addition.
lint_git_diff() {
  git diff-index -p "$@"
}

# git grep has no plumbing counterpart, so pin the settings that reshape its
# output: color.ui and color.grep, grep.lineNumber, grep.column, grep.fullName,
# and grep.patternType, which decides whether the pattern is a basic or
# extended regular expression.  A caller passing -E, -F, -P, or -n later still
# wins, since git takes the last one given.
lint_git_grep() {
  git grep --no-color --no-line-number --no-column --no-full-name -G "$@"
}

# Run ctags over the file names on stdin and print the type tags it finds.
ctags_types() {
  ctags -n -L - --languages=c,c++ --c-kinds=t --c++-kinds=t -f /dev/stdout
}

# Print every type name that ctags can find under src/postgres and in the pggate
# ybc headers, sorted and unique.  The file set is the one the collection recipe
# in src/postgres/src/tools/pgindent/README uses.
all_ctags_types() {
  git ls-files ':(exclude)src/postgres/third-party-extensions' 'src/postgres/*' \
      'src/yb/yql/pggate/*ybc_*.h' \
    | ctags_types \
    | awk '{print $1}' \
    | sort -u
}

yb_typedefs_list=src/postgres/src/tools/pgindent/yb_typedefs.list

# Print the macros that mint Ybc handle type names, joined with | so that the
# result works as a grep -E pattern, such as YB_DEFINE_HANDLE_TYPE.  Read them
# from the header rather than naming them here since the set can grow.
handle_type_macros() {
  local macros pattern
  pattern='#define +YB[A-Z_]*\(name\) typedef struct name \*Ybc##name'
  macros=$(grep -oE "$pattern" src/yb/yql/pggate/ybc_pg_typedefs.h \
             | awk '{print $2}' \
             | sed 's/(name)//' \
             | sort -u \
             | paste -sd'|' -)
  if [ -z "$macros" ]; then
    echo "Found no handle type macros in ybc_pg_typedefs.h" >/dev/stderr
    return 1
  fi
  echo "$macros"
}
