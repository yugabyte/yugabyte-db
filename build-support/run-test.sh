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
# If YB_COMPRESS_TEST_OUTPUT is set to 1 (0 by default), then the logs will be
# gzip-compressed while they are written.
#
# If YB_REPORT_TEST_RESULTS is non-zero, then tests are reported to the
# central test server.

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

if [ ! -x "$TEST_PATH" ]; then
  echo "Test binary '$TEST_PATH' is not executable" >&2
  exit 1
fi

YB_COMPRESS_TEST_OUTPUT=${YB_COMPRESS_TEST_OUTPUT:-0}
if [[ ! "${YB_COMPRESS_TEST_OUTPUT:-}" =~ ^[01]?$ ]]; then
  echo "YB_COMPRESS_TEST_OUTPUT is expected to be 0, 1, or undefined," \
       "found: '$YB_COMPRESS_TEST_OUTPUT'" >&2
  exit 1
fi

if [ ! -d "$PWD" ]; then
  echo "Current directory $PWD does not exist, using /tmp as working directory" >&2
  cd /tmp
fi

# Absolute path to the root build directory. The test path is expected to be one or two levels
# within it. This works for tests that are in the "bin" directory as well as tests in
# "rocksdb-build".
BUILD_ROOT=$(cd "$(dirname "$TEST_PATH")"/.. && pwd)

. "$( dirname "$BASH_SOURCE" )/common-test-env.sh"

if [ "$(uname)" == "Darwin" ]; then
  # Stack trace address to line number conversion is disabled on Mac OS X as of 04/04/2016/
  # See https://yugabyte.atlassian.net/browse/ENG-37
  STACK_TRACE_FILTER=cat
else
  STACK_TRACE_FILTER="$SOURCE_ROOT"/build-support/stacktrace_addr2line.pl
fi

TEST_TMP_DIR_ROOT=$BUILD_ROOT/test-tmp
TEST_LOG_DIR=$BUILD_ROOT/test-logs
mkdir -p "$TEST_LOG_DIR"

TEST_DEBUG_DIR="$BUILD_ROOT/test-debug"
mkdir -p "$TEST_DEBUG_DIR"

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

if [ "$( basename "$TEST_DIR" )" == "rocksdb-build" ]; then
  LOG_PATH_PREFIX="$TEST_LOG_DIR/rocksdb_$TEST_NAME"
  TMP_DIR_NAME_PREFIX="rocksdb_$TMP_DIR_NAME_PREFIX"
else
  LOG_PATH_PREFIX="$TEST_LOG_DIR/$TEST_NAME"
fi

LOG_PATH_TXT=$LOG_PATH_PREFIX.txt
XML_FILE_PATH=$LOG_PATH_PREFIX.xml

# Remove both the compressed and uncompressed output, so the developer
# doesn't accidentally get confused and read output from a prior test
# run.
rm -f "$LOG_PATH_TXT" "$LOG_PATH_TXT.gz"

if [ "$YB_COMPRESS_TEST_OUTPUT" == "1" ]; then
  pipe_cmd=gzip
  LOG_PATH=$LOG_PATH_TXT.gz
else
  pipe_cmd=cat
  LOG_PATH=$LOG_PATH_TXT
fi

# Suppressions require symbolization. We'll default to using the symbolizer in
# thirdparty.
if [ -z "${ASAN_SYMBOLIZER_PATH:-}" ]; then
  export ASAN_SYMBOLIZER_PATH=$SOURCE_ROOT/thirdparty/clang-toolchain/bin/llvm-symbolizer
fi

# Configure TSAN (ignored if this isn't a TSAN build).
#
# Deadlock detection (new in clang 3.5) is disabled because:
# 1. The clang 3.5 deadlock detector crashes in some YB unit tests. It
#    needs compiler-rt commits c4c3dfd, 9a8efe3, and possibly others.
# 2. Many unit tests report lock-order-inversion warnings; they should be
#    fixed before reenabling the detector.
TSAN_OPTIONS="${TSAN_OPTIONS:-} detect_deadlocks=0"
TSAN_OPTIONS="$TSAN_OPTIONS suppressions=$SOURCE_ROOT/build-support/tsan-suppressions.txt"
TSAN_OPTIONS="$TSAN_OPTIONS history_size=7"
TSAN_OPTIONS="$TSAN_OPTIONS external_symbolizer_path=$ASAN_SYMBOLIZER_PATH"
export TSAN_OPTIONS

# Enable leak detection even under LLVM 3.4, where it was disabled by default.
# This flag only takes effect when running an ASAN build.
ASAN_OPTIONS="${ASAN_OPTIONS:-} detect_leaks=1"
export ASAN_OPTIONS

# Set up suppressions for LeakSanitizer
LSAN_OPTIONS="${LSAN_OPTIONS:-} suppressions=$SOURCE_ROOT/build-support/lsan-suppressions.txt"
export LSAN_OPTIONS

# Set a 15-minute timeout for tests run via 'make test'.
# This keeps our jenkins builds from hanging in the case that there's
# a deadlock or anything.
YB_TEST_TIMEOUT=${YB_TEST_TIMEOUT:-900}

# Allow for collecting core dumps.
YB_TEST_ULIMIT_CORE=${YB_TEST_ULIMIT_CORE:-0}
ulimit -c "$YB_TEST_ULIMIT_CORE"

# Run the actual test.

export TEST_TMPDIR="$TEST_TMP_DIR_ROOT/${TMP_DIR_NAME_PREFIX}"
# Ensure that the test data directory is usable.
mkdir -p "$TEST_TMPDIR"
if [ ! -w "$TEST_TMPDIR" ]; then
  echo "Error: Test output directory ($TEST_TMPDIR) is not writable on $(hostname)" \
    "by user $(whoami)" >&2
  exit 1
fi

# gtest won't overwrite old junit test files, resulting in a build failure
# even when retries are successful.
rm -f "$XML_FILE_PATH"

echo "Running $TEST_NAME with timeout $YB_TEST_TIMEOUT sec, redirecting output into $LOG_PATH"
RAW_LOG_PATH=${LOG_PATH_PREFIX}__raw.txt
if [[ "$TEST_NAME" =~ \
      ^(bloom|compact_on_deletion_collector|cuckoo_table_reader|dynamic_bloom|merge|prefix)_test$ ]]
then
  TIMEOUT_ARG=""
else
  TIMEOUT_ARG="--test_timeout_after $YB_TEST_TIMEOUT"
fi

set +e
$ABS_TEST_PATH "$@" $TIMEOUT_ARG \
  "--gtest_output=xml:$XML_FILE_PATH" >"$RAW_LOG_PATH" 2>&1
STATUS=$?
set -e
if [ ! -f "$XML_FILE_PATH" ]; then
  echo "$ABS_TEST_PATH failed to generate $XML_FILE_PATH, exit code: $STATUS" >&2
  STATUS=1
fi

STACK_TRACE_FILTER_ERR_PATH="${LOG_PATH_PREFIX}__stack_trace_filter_err.txt"

if [ "$STACK_TRACE_FILTER" == "cat" ]; then
  # Don't pass the binary name as an argument to the cat command.
  "$STACK_TRACE_FILTER" <"$RAW_LOG_PATH" 2>"$STACK_TRACE_FILTER_ERR_PATH" | \
    $pipe_cmd >"$LOG_PATH"
else
  "$STACK_TRACE_FILTER" "$ABS_TEST_PATH" <"$RAW_LOG_PATH" 2>"$STACK_TRACE_FILTER_ERR_PATH" | \
    $pipe_cmd >"$LOG_PATH"
fi

if [ $? -ne 0 ]; then
  # Stack trace filtering or compression failed, create an uncompressed output file with the
  # error message.
  echo "Failed to run command '$STACK_TRACE_FILTER' piped to '$pipe_cmd'" | tee "$LOG_PATH_TXT"
  echo >>"$LOG_PATH_TXT"
  echo "Standard error from '$STACK_TRACE_FILTER'": >>"$LOG_PATH_TXT"
  cat "$STACK_TRACE_FILTER_ERR_PATH" >>"$LOG_PATH_TXT"
  echo >>"$LOG_PATH_TXT"

  echo "Raw output:" >>"$LOG_PATH_TXT"
  echo >>"$LOG_PATH_TXT"
  cat "$RAW_LOG_PATH" >>"$LOG_PATH_TXT"
fi
rm -f "$RAW_LOG_PATH" "$STACK_TRACE_FILTER_ERR_PATH"

# TSAN doesn't always exit with a non-zero exit code due to a bug:
# mutex errors don't get reported through the normal error reporting infrastructure.
# So we make sure to detect this and exit 1.
#
# Additionally, certain types of failures won't show up in the standard JUnit
# XML output from gtest. We assume that gtest knows better than us and our
# regexes in most cases, but for certain errors we delete the resulting xml
# file and let our own post-processing step regenerate it.
export GREP=$(which egrep)
if zgrep --silent "ThreadSanitizer|Leak check.*detected leaks" "$LOG_PATH" ; then
  echo "ThreadSanitizer or leak check failures in $LOG_PATH"
  STATUS=1
  rm -f "$XML_FILE_PATH"
fi

if [ -n "${YB_REPORT_TEST_RESULTS:-}" ]; then
  echo Reporting results
  $SOURCE_ROOT/build-support/report-test.sh "$ABS_TEST_PATH" "$LOG_PATH" "$STATUS" &

  # On success, we'll do "best effort" reporting, and disown the subprocess.
  # On failure, we want to upload the failed test log. So, in that case,
  # wait for the report-test.sh job to finish, lest we accidentally run
  # a test retry and upload the wrong log.
  if [ "$STATUS" -eq "0" ]; then
    disown
  else
    wait
  fi
fi

# If we have a LeakSanitizer report, and XML reporting is configured, add a new test
# case result to the XML file for the leak report. Otherwise Jenkins won't show
# us which tests had LSAN errors.
if zgrep --silent "ERROR: LeakSanitizer: detected memory leaks" $LOG_PATH ; then
    echo Test had memory leaks. Editing XML
    perl -p -i -e '
    if (m#</testsuite>#) {
      print "<testcase name=\"LeakSanitizer\" status=\"run\" classname=\"LSAN\">\n";
      print "  <failure message=\"LeakSanitizer failed\" type=\"\">\n";
      print "    See txt log file for details\n";
      print "  </failure>\n";
      print "</testcase>\n";
    }' $XML_FILE_PATH
fi

# Capture and compress core file and binary.
set +e
COREFILES=$(ls | grep ^core)
set -e
if [ -n "$COREFILES" ]; then
  echo Found core dump. Saving executable and core files.
  gzip < "$ABS_TEST_PATH" > "$TEST_DEBUG_DIR/$TEST_NAME.gz"
  for COREFILE in $COREFILES; do
    gzip < "$COREFILE" > "$TEST_DEBUG_DIR/$TEST_NAME.$COREFILE.gz"
  done
  set +e
  # Pull in any .so files as well.
  for LIB in $(ldd $ABS_TEST_PATH | grep $BUILD_ROOT | awk '{print $3}'); do
    LIB_NAME=$(basename $LIB)
    gzip < "$LIB" > "$TEST_DEBUG_DIR/$LIB_NAME.gz" || exit $?
  done
  set -e
fi

popd >/dev/null
rm -Rf "$TEST_WORKDIR"
if [ -z "$( ls -A "$TEST_TMPDIR" )" ]; then
  rmdir "$TEST_TMPDIR"
fi

exit "$STATUS"
