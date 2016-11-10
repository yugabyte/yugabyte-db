#!/usr/bin/env bash

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
# Script which wraps running a test and redirects its output to a
# test log directory.
#
# Path to the test executable or script to be run.
# May be relative or absolute.

# Portions Copyright (c) YugaByte, Inc.

set -euo pipefail

TEST_PATH=${1:-}
if [[ -z $TEST_PATH ]]; then
  fatal "Test path must be specified as the first argument"
fi
shift

if [[ ! -f $TEST_PATH ]]; then
  fatal "Test binary '$TEST_PATH' does not exist"
fi

if [[ -n ${YB_CHECK_TEST_EXISTENCE_ONLY:-} ]]; then
  exit 0
fi

if [[ ! -x $TEST_PATH ]]; then
  fatal "Test binary '$TEST_PATH' is not executable"
fi

if [[ ! -d $PWD ]]; then
  log "Current directory $PWD does not exist, using /tmp as working directory"
  cd /tmp
fi

# Absolute path to the root build directory. The test path is expected to be in a subdirectory
# of it. This works for tests that are in the "bin" directory as well as tests in "rocksdb-build".
BUILD_ROOT=$(cd "$(dirname "$TEST_PATH")"/.. && pwd)
BUILD_ROOT_BASENAME=${BUILD_ROOT##*/}

. "$( dirname "$BASH_SOURCE" )/common-test-env.sh"
set_common_test_paths

TEST_DIR=$(cd "$(dirname "$TEST_PATH")" && pwd)

if [ ! -d "$TEST_DIR" ]; then
  echo "Test directory '$TEST_DIR' does not exist"
  exit 1
fi

TEST_NAME_WITH_EXT=$(basename "$TEST_PATH")
TMP_DIR_NAME_PREFIX=$( echo "$TEST_NAME_WITH_EXT" | tr '.' '_' )
abs_test_binary_path=$TEST_DIR/$TEST_NAME_WITH_EXT

# Remove path and extension, if any.
TEST_NAME=${TEST_NAME_WITH_EXT%%.*}


TEST_DIR_BASENAME="$( basename "$TEST_DIR" )"
if [ "$TEST_DIR_BASENAME" == "rocksdb-build" ]; then
  LOG_PATH_BASENAME_PREFIX=rocksdb_$TEST_NAME
  TMP_DIR_NAME_PREFIX="rocksdb_$TMP_DIR_NAME_PREFIX"
  IS_ROCKSDB=1
else
  LOG_PATH_BASENAME_PREFIX=$TEST_NAME
  IS_ROCKSDB=0
fi

set_asan_tsan_options

tests=()
rel_test_binary="$TEST_DIR_BASENAME/$TEST_NAME"
total_num_tests=0
num_tests=0
num_tests_skipped=0
collect_gtest_tests
if [[ $total_num_tests -gt 0 && $num_tests_skipped -eq $total_num_tests ]]; then
  fatal "Skipped all $total_num_tests tests in $rel_test_binary. Invalid regular expression?" \
        "( YB_GTEST_REGEX=$YB_GTEST_REGEX )."
fi

set +u  # Do not fail on an empty list.
if [[ ${#tests[@]} -eq 0 ]]; then
  fatal "No tests found in $rel_test_binary."
fi
set -u

set_test_log_url_prefix

global_exit_code=0

# Loop over all tests in a gtest binary, or just one element (the whole test binary) for tests that
# we have to run in one shot.
for test_descriptor in "${tests[@]}"; do
  prepare_for_running_test
  run_test_and_process_results
done

cd /tmp
rm -rf "$TEST_TMPDIR"

