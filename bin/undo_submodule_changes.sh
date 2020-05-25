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

. "${BASH_SOURCE%/*}"/../build-support/common-build-env.sh

cd "$YB_SRC_ROOT"
for submodule_dir in submodules/* thirdparty; do
  submodule_commit_in_master=$(
    git ls-tree HEAD "$submodule_dir" | awk '{print $3}'
  )
  if ! is_valid_git_sha1 "$submodule_commit_in_master"; then
    fatal "Failed to get SHA1 in master for submodule $submodule_dir"
  fi
  echo "$submodule_commit_in_master"
  ( set -x; cd "$submodule_dir"; git checkout "$submodule_commit_in_master" )
done
