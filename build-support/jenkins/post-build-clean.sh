#!/bin/bash

#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
# The following only applies to changes made to this file as part of YugaByte development.
#
# Portions Copyright (c) YugaByte, Inc.
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
# Script which runs on Jenkins slaves after the build/test to clean up
# disk space used by build artifacts. Our build tends to "make clean"
# before running anyway, so doing the post-build cleanup shouldn't
# hurt our build times. It does, however, save a fair amount of disk
# space in the Jenkins workspace disk. This can help prevent our EC2
# slaves from filling up and causing spurious failures.

# Note that we use simple shell commands instead of "make clean"
# or "mvn clean". This is more foolproof even if something ends
# up partially compiling, etc.

# Portions Copyright (c) YugaByte, Inc.

set -euo pipefail

# shellcheck source=build-support/common-test-env.sh
. "${BASH_SOURCE%/*}"/../common-test-env.sh

show_stats() {
  expect_num_args 1 "$@"
  local before_or_after=$1

  log_empty_line
  log "Disk usage in '$real_build_root_path' $before_or_after cleanup:"
  du -sh "$real_build_root_path"
  log "Number of XML files in '$real_build_root_path' $before_or_after cleanup:"
  find -L "$real_build_root_path" -name "*.xml" | wc -l
  log_empty_line
}

show_num_xml_files() {
  find "$real_build_root_path" -name "*.xml" | wc -l
}

ensure_build_root_exists

BUILD_ROOT=$( cd "$BUILD_ROOT" && pwd )
readonly BUILD_ROOT

set_common_test_paths
set_real_build_root_path

cd "$real_build_root_path"

show_stats "before"

if [[ ${YB_KEEP_BUILD_OUTPUT:-} != "1" ]]; then
  # Get rid of most of the build artifacts.
  (
    set -x
    rm -rf bin lib src
    find . -perm -u+x -type f -wholename './tests-*/*' -exec rm -f {} \;
  )
fi

# Get rid of temporary directories such as
#   yb-test-logs/bin__raft_consensus-itest/
#   RaftConsensusITest_TestChangeConfigRejectedUnlessNoopReplicated.tmp
# (the above path is split between multiple lines) relative to the build root.
#
# We are following symlinks here, because these tmp directories are behind one level of symlinks
# E.g. in the above example yb-test-logs/bin__raft_consensus-itest may be a symlink to a directory
# on an ephemeral drive.
find -L "$YB_TEST_LOG_ROOT_DIR" -mindepth 2 -maxdepth 2 -name "*.tmp" -type d -exec rm -rf "{}" \;

show_stats "after"
