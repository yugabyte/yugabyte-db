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
