#!/usr/bin/env bash

# Copyright (c) YugaByte, Inc.
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

# A script to quickly revert submodule state to that of origin/master.

set -euo pipefail

# shellcheck source=submodules/yugabyte-bash-common/src/yugabyte-bash-common.sh
. "${BASH_SOURCE%/*}"/../build-support/common-build-env.sh

cd "$YB_SRC_ROOT"

# We are using the fact that our submodule paths do not contain spaces, so the following is safe.
# shellcheck disable=SC2207
submodule_rel_paths=( $(
  git config --file .gitmodules --name-only --get-regexp path | \
    sed 's/^submodule[.]//; s/[.]path$//g'
) )

for submodule_dir in "${submodule_rel_paths[@]}"; do
  submodule_commit_in_head=$(
    git ls-tree HEAD "$submodule_dir" | awk '{print $3}'
  )
  if ! is_valid_git_sha1 "$submodule_commit_in_head"; then
    fatal "Failed to get SHA1 in HEAD for submodule $submodule_dir"
  fi
  log "Submodule commit in HEAD for $submodule_dir: $submodule_commit_in_head"

  if [[ $submodule_dir == "thirdparty" &&
        -d $submodule_dir &&
        ! -d "$submodule_dir/.git" ]]; then
    log "$submodule_dir is not a git repo, removing to replace with submodule"
    ( set -x; rm -rf "$submodule_dir" )
  fi

  if [[ ! -d $submodule_dir ]]; then
    log "Directory $submodule_dir does not exist, doing a submodule update"
    ( set -x; git submodule update --init --recursive -- "$submodule_dir" )
  fi

  (
    set -x
    cd "$submodule_dir"
    git checkout "$submodule_commit_in_head"
  )
done
