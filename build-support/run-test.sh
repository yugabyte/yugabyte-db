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
if [ -z "$TEST_PATH" ]; then
  echo "Test path must be specified as the first argument" >&2
  exit 1
fi
shift

if [ ! -f "$TEST_PATH" ]; then
  echo "Test binary '$TEST_PATH' does not exist" >&2
  exit 1
fi

if [ -n "${YB_CHECK_TEST_EXISTENCE_ONLY:-}" ]; then
  exit 0
fi

if [ ! -x "$TEST_PATH" ]; then
  echo "Test binary '$TEST_PATH' is not executable" >&2
  exit 1
fi

if [ ! -d "$PWD" ]; then
  echo "Current directory $PWD does not exist, using /tmp as working directory" >&2
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

# Suppressions require symbolization. We'll default to using the symbolizer in
# thirdparty.
if [ -z "${ASAN_SYMBOLIZER_PATH:-}" ]; then
  export ASAN_SYMBOLIZER_PATH=$YB_SRC_ROOT/thirdparty/clang-toolchain/bin/llvm-symbolizer
fi

# Configure TSAN (ignored if this isn't a TSAN build).
#
# Deadlock detection (new in clang 3.5) is disabled because:
# 1. The clang 3.5 deadlock detector crashes in some YB unit tests. It
#    needs compiler-rt commits c4c3dfd, 9a8efe3, and possibly others.
# 2. Many unit tests report lock-order-inversion warnings; they should be
#    fixed before reenabling the detector.
TSAN_OPTIONS="${TSAN_OPTIONS:-} detect_deadlocks=0"
TSAN_OPTIONS="$TSAN_OPTIONS suppressions=$YB_SRC_ROOT/build-support/tsan-suppressions.txt"
TSAN_OPTIONS="$TSAN_OPTIONS history_size=7"
TSAN_OPTIONS="$TSAN_OPTIONS external_symbolizer_path=$ASAN_SYMBOLIZER_PATH"
export TSAN_OPTIONS

# Enable leak detection even under LLVM 3.4, where it was disabled by default.
# This flag only takes effect when running an ASAN build.
ASAN_OPTIONS="${ASAN_OPTIONS:-} detect_leaks=1"
export ASAN_OPTIONS

# Set up suppressions for LeakSanitizer
LSAN_OPTIONS="${LSAN_OPTIONS:-} suppressions=$YB_SRC_ROOT/build-support/lsan-suppressions.txt"
export LSAN_OPTIONS

tests=()
rel_test_binary="$TEST_DIR_BASENAME/$TEST_NAME"
num_tests=0
collect_gtest_tests

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
