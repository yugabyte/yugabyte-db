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
ABS_TEST_PATH=$TEST_DIR/$TEST_NAME_WITH_EXT

# Remove path and extension, if any.
TEST_NAME=${TEST_NAME_WITH_EXT%%.*}

# We run each test in its own subdir to avoid core file related races.
TEST_WORKDIR="$BUILD_ROOT/test-work/$TEST_NAME"
mkdir -p "$TEST_WORKDIR"
pushd "$TEST_WORKDIR" >/dev/null || exit 1
rm -f *

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

set +u  # Don't fail on an empty list.
if [[ ${#tests[@]} -eq 0 ]]; then
  fatal "No tests found in $rel_test_binary."
fi
set -u

set_test_log_url_prefix

global_exit_code=0
for test_descriptor in "${tests[@]}"; do
  prepare_for_running_test
  run_test_and_process_results
  if [ ]; then
    # TODO: look into these after TSAN/ASAN are enabled.

    # TSAN doesn't always exit with a non-zero exit code due to a bug:
    # mutex errors don't get reported through the normal error reporting infrastructure.
    # So we make sure to detect this and exit 1.
    #
    # Additionally, certain types of failures won't show up in the standard JUnit
    # XML output from gtest. We assume that gtest knows better than us and our
    # regexes in most cases, but for certain errors we delete the resulting xml
    # file and let our own post-processing step regenerate it.
    export GREP=$(which egrep)
    if grep --silent "ThreadSanitizer|Leak check.*detected leaks" "$test_log_path" ; then
      echo "ThreadSanitizer or leak check failures in '$test_log_path'"
      global_exit_code=1
      rm -f "$xml_output_file"
    fi

    # If we have a LeakSanitizer report, and XML reporting is configured, add a new test
    # case result to the XML file for the leak report. Otherwise Jenkins won't show
    # us which tests had LSAN errors.
    if grep --silent "ERROR: LeakSanitizer: detected memory leaks" "$test_log_path" ; then
      echo Test had memory leaks. Editing XML
      perl -p -i -e '
      if (m#</testsuite>#) {
        print "<testcase name=\"LeakSanitizer\" status=\"run\" classname=\"LSAN\">\n";
        print "  <failure message=\"LeakSanitizer failed\" type=\"\">\n";
        print "    See txt log file for details\n";
        print "  </failure>\n";
        print "</testcase>\n";
      }' $xml_output_file
    fi
  fi
done # looping over all tests in a gtest binary, or just one element (the whole test binary).

popd >/dev/null
if [ -d "$TEST_WORKDIR" ]; then
  rm -Rf "$TEST_WORKDIR"
fi

if [ -d "$TEST_TMPDIR" ]; then
  if [ -z "$( ls -A "$TEST_TMPDIR" )" ]; then
    rmdir "$TEST_TMPDIR"
  fi
fi

exit "$global_exit_code"
