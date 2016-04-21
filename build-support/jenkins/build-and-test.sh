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
# This script is invoked from the Jenkins builds to build YB
# and run all the unit tests.
#
# Environment variables may be used to customize operation:
#   BUILD_TYPE: Default: DEBUG
#     Maybe be one of ASAN|TSAN|DEBUG|RELEASE|COVERAGE|LINT
#
#   YB_ALLOW_SLOW_TESTS
#   Default: 1
#     Runs the "slow" version of the unit tests. Set to 0 to
#     run the tests more quickly.
#
#   RUN_FLAKY_ONLY
#   Default: 0
#     Only runs tests which have failed recently, if this is 1.
#     Used by the kudu-flaky-tests jenkins build.
#
#   YB_FLAKY_TEST_ATTEMPTS
#   Default: 1
#     If more than 1, will fetch the list of known flaky tests
#     from the yb-test jenkins job, and allow those tests to
#     be flaky in this build.
#
#   TEST_RESULT_SERVER
#   Default: none
#     The host:port pair of a server running test_result_server.py.
#     This must be configured for flaky test resistance or test result
#     reporting to work.
#
#   BUILD_CPP
#   Default: 1
#     Build and test C++ code if this is set to 1.
#
#   BUILD_JAVA
#   Default: 1
#     Build and test java code if this is set to 1.
#
#   VALIDATE_CSD
#   Default: 0
#     If 1, runs the CM CSD validator against the YB CSD.
#     This requires access to an internal Cloudera maven repository.
#
#   BUILD_PYTHON
#   Default: 1
#     Build and test the Python wrapper of the client API.
#
#   MVN_FLAGS
#   Default: ""
#     Extra flags which are passed to 'mvn' when building and running Java
#     tests. This can be useful, for example, to choose a different maven
#     repository location.
#
#   EXTRA_MAKE_ARGS
#     Extra arguments to pass to Make
#
#   YB_COMPRESS_TEST_OUTPUT
#   Default: 0
#     Controls whether test output needs to be compressed (gzipped).


# If a commit messages contains a line that says 'DONT_BUILD', exit
# immediately.
DONT_BUILD=$(git show|egrep '^\s{4}DONT_BUILD$')
if [ -n "$DONT_BUILD" ]; then
  echo "*** Build not requested. Exiting."
  exit 1
fi

if [[ ! "${YB_COMPRESS_TEST_OUTPUT:-}" =~ ^[01]?$ ]]; then
  echo "YB_COMPRESS_TEST_OUTPUT is expected to be 0, 1, or undefined," \
       "found: '$YB_COMPRESS_TEST_OUTPUT'" >&2
  exit 1
fi

YB_COMPRESS_TEST_OUTPUT=${YB_COMPRESS_TEST_OUTPUT:-0}

if [ "`uname`" == "Darwin" ]; then
  IS_MAC=1
else
  IS_MAC=0
fi

if [ "$YB_COMPRESS_TEST_OUTPUT" == "1" ]; then
  if [ "$IS_MAC" == "1" ]; then
    CAT_TEST_OUTPUT=gzcat
  else
    CAT_TEST_OUTPUT=zcat
  fi
  TEST_OUTPUT_EXTENSION=txt.gz
else
  CAT_TEST_OUTPUT=cat
  TEST_OUTPUT_EXTENSION=txt
fi

echo "YB_KEEP_BUILD_ARTIFACTS=${YB_KEEP_BUILD_ARTIFACTS:-}"

SKIP_CPP_BUILD=$(git show|egrep '^\s{4}SKIP_CPP_BUILD$')
if [ -n "$SKIP_CPP_BUILD" ]; then
  BUILD_CPP="0"
else
  BUILD_CPP="1"
fi

set -e
# We pipe our build output to a log file with tee.
# This bash setting ensures that the script exits if the build fails.
set -o pipefail
# gather core dumps
ulimit -c unlimited

BUILD_TYPE=${BUILD_TYPE:-DEBUG}
BUILD_TYPE=$(echo "$BUILD_TYPE" | tr a-z A-Z) # capitalize
BUILD_TYPE_LOWER=$(echo "$BUILD_TYPE" | tr A-Z a-z)

# Set up defaults for environment variables.
DEFAULT_ALLOW_SLOW_TESTS=1

# TSAN builds are pretty slow, so don't do SLOW tests unless explicitly
# requested. Setting YB_USE_TSAN influences the thirdparty build.
if [ "$BUILD_TYPE" = "TSAN" ]; then
  DEFAULT_ALLOW_SLOW_TESTS=0
  export YB_USE_TSAN=1
fi

export YB_FLAKY_TEST_ATTEMPTS=${YB_FLAKY_TEST_ATTEMPTS:-1}
export YB_ALLOW_SLOW_TESTS=${YB_ALLOW_SLOW_TESTS:-$DEFAULT_ALLOW_SLOW_TESTS}
export YB_COMPRESS_TEST_OUTPUT=${YB_COMPRESS_TEST_OUTPUT:-1}

BUILD_JAVA=${BUILD_JAVA:-1}
VALIDATE_CSD=${VALIDATE_CSD:-0}
BUILD_PYTHON=${BUILD_PYTHON:-1}

SOURCE_ROOT=$(cd $(dirname "$BASH_SOURCE")/../..; pwd)
BUILD_ROOT="$SOURCE_ROOT/build/$BUILD_TYPE_LOWER"
CTEST_OUTPUT_PATH="$BUILD_ROOT"/ctest.log

if [ "$IS_MAC" == "1" ]; then
  export DYLD_FALLBACK_LIBRARY_PATH="$BUILD_ROOT/rocksdb-build"
  echo "Set DYLD_FALLBACK_LIBRARY_PATH to $DYLD_FALLBACK_LIBRARY_PATH"
fi

# Remove testing artifacts from the previous run before we do anything
# else. Otherwise, if we fail during the "build" step, Jenkins will
# archive the test logs from the previous run, thinking they came from
# this run, and confuse us when we look at the failed build.
rm -rf "$BUILD_ROOT"
mkdir -p "$BUILD_ROOT"

list_flaky_tests() {
  curl -s "http://$TEST_RESULT_SERVER/list_failed_tests?num_days=3&build_pattern=%25kudu-test%25"
  return $?
}

TEST_LOG_DIR="$BUILD_ROOT/test-logs"
TEST_TMP_ROOT_DIR="$BUILD_ROOT/test-tmp"

TEST_LOG_URL_PREFIX=""
if [ -n "${BUILD_URL:-}" ]; then
  BUILD_URL_NO_TRAILING_SLASH=${BUILD_URL%/}
  TEST_LOG_URL_PREFIX="${BUILD_URL_NO_TRAILING_SLASH}/artifact/build/$BUILD_TYPE_LOWER/test-logs"
fi

cleanup() {
  if [ "${YB_KEEP_BUILD_ARTIFACTS:-}" == "true" ]; then
    echo "Not removing build artifacts: YB_KEEP_BUILD_ARTIFACTS is set"
  else
    echo "Cleaning up all build artifacts... (YB_KEEP_BUILD_ARTIFACTS=$YB_KEEP_BUILD_ARTIFACTS)"
    $SOURCE_ROOT/build-support/jenkins/post-build-clean.sh
  fi
}

# If we're running inside Jenkins (the BUILD_ID is set), then install
# an exit handler which will clean up all of our build results.
if [ -n "$BUILD_ID" ]; then
  trap cleanup EXIT
fi

export TOOLCHAIN_DIR=/opt/toolchain
if [ -d "$TOOLCHAIN_DIR" ]; then
  PATH=$TOOLCHAIN_DIR/apache-maven-3.0/bin:$PATH
fi

$SOURCE_ROOT/build-support/enable_devtoolset.sh thirdparty/build-if-necessary.sh

THIRDPARTY_BIN=$(pwd)/thirdparty/installed/bin
export PPROF_PATH=$THIRDPARTY_BIN/pprof

if which ccache >/dev/null ; then
  CLANG=$(pwd)/build-support/ccache-clang/clang
else
  CLANG=$(pwd)/thirdparty/clang-toolchain/bin/clang
fi

# Before running cmake below, clean out any errant cmake state from the source
# tree. We need this to help transition into a world where out-of-tree builds
# are required. Once that's done, the cleanup can be removed.
rm -rf "$SOURCE_ROOT/CMakeCache.txt" "$SOURCE_ROOT/CMakeFiles"

# Configure the build
#
# ASAN/TSAN can't build the Python bindings because the exported YB client
# library (which the bindings depend on) is missing ASAN/TSAN symbols.
cd $BUILD_ROOT
if [ "$BUILD_TYPE" = "ASAN" ]; then
  $SOURCE_ROOT/build-support/enable_devtoolset.sh \
    "env CC=$CLANG CXX=$CLANG++ $THIRDPARTY_BIN/cmake -DYB_USE_ASAN=1 -DYB_USE_UBSAN=1 $SOURCE_ROOT"
  BUILD_TYPE=fastdebug
  BUILD_PYTHON=0
elif [ "$BUILD_TYPE" = "TSAN" ]; then
  $SOURCE_ROOT/build-support/enable_devtoolset.sh \
    "env CC=$CLANG CXX=$CLANG++ $THIRDPARTY_BIN/cmake -DYB_USE_TSAN=1 $SOURCE_ROOT"
  BUILD_TYPE=fastdebug
  EXTRA_TEST_FLAGS="$EXTRA_TEST_FLAGS -LE no_tsan"
  BUILD_PYTHON=0
elif [ "$BUILD_TYPE" = "COVERAGE" ]; then
  DO_COVERAGE=1
  BUILD_TYPE=debug
  $SOURCE_ROOT/build-support/enable_devtoolset.sh "$THIRDPARTY_BIN/cmake -DYB_GENERATE_COVERAGE=1 $SOURCE_ROOT"
elif [ "$BUILD_TYPE" = "LINT" ]; then
  # Create empty test logs or else Jenkins fails to archive artifacts, which
  # results in the build failing.
  mkdir -p Testing/Temporary
  mkdir -p "$TEST_LOG_DIR"

  $SOURCE_ROOT/build-support/enable_devtoolset.sh "$THIRDPARTY_BIN/cmake $SOURCE_ROOT"
  make lint | tee "$TEST_LOG_DIR"/lint.log
  exit $?
fi

# Only enable test core dumps for certain build types.
if [ "$BUILD_TYPE" != "ASAN" ]; then
  export YB_TEST_ULIMIT_CORE=unlimited
fi

# If we are supposed to be resistant to flaky tests, we need to fetch the
# list of tests to ignore
if [ "$YB_FLAKY_TEST_ATTEMPTS" -gt 1 ]; then
  echo Fetching flaky test list...
  export YB_FLAKY_TEST_LIST=$BUILD_ROOT/flaky-tests.txt
  mkdir -p $(dirname $YB_FLAKY_TEST_LIST)
  echo -n > $YB_FLAKY_TEST_LIST
    if [ -n "$TEST_RESULT_SERVER" ] && \
        list_flaky_tests > $YB_FLAKY_TEST_LIST ; then
    echo Will retry flaky tests up to $YB_FLAKY_TEST_ATTEMPTS times:
    cat $YB_FLAKY_TEST_LIST
    echo ----------
  else
    echo Unable to fetch flaky test list. Disabling flaky test resistance.
    export YB_FLAKY_TEST_ATTEMPTS=1
  fi
fi

$SOURCE_ROOT/build-support/enable_devtoolset.sh \
  "$THIRDPARTY_BIN/cmake -DCMAKE_BUILD_TYPE=${BUILD_TYPE} $SOURCE_ROOT"

if [ "$BUILD_CPP" == "1" ]; then
  echo
  echo Building C++ code.
  echo ------------------------------------------------------------
  NUM_PROCS=$(getconf _NPROCESSORS_ONLN)
  make -j$NUM_PROCS $EXTRA_MAKE_ARGS 2>&1 | tee build.log

  # If compilation succeeds, try to run all remaining steps despite any failures.
  set +e

  # Run tests
  export GTEST_OUTPUT="xml:$TEST_LOG_DIR/" # Enable JUnit-compatible XML output.
  if [ "$RUN_FLAKY_ONLY" == "1" ] ; then
    if [ -z "$TEST_RESULT_SERVER" ]; then
      echo Must set TEST_RESULT_SERVER to use RUN_FLAKY_ONLY
      exit 1
    fi
    echo
    echo Running flaky tests only:
    echo ------------------------------------------------------------
    list_flaky_tests | tee build/flaky-tests.txt
    test_regex=$(perl -e '
      chomp(my @lines = <>);
      print join("|", map { "^" . quotemeta($_) . "\$" } @lines);
     ' build/flaky-tests.txt)
    EXTRA_TEST_FLAGS="$EXTRA_TEST_FLAGS -R $test_regex"

    # We don't support detecting java flaky tests at the moment.
    echo Disabling Java build since RUN_FLAKY_ONLY=1
    BUILD_JAVA=0
  fi

  EXIT_STATUS=0
  FAILURES=""

  set +e
  (
    set -x
    $THIRDPARTY_BIN/ctest -j$NUM_PROCS $EXTRA_TEST_FLAGS --output-on-failure 2>&1 | \
      tee "$CTEST_OUTPUT_PATH"
  )
  if [ $? -ne 0 ]; then
    EXIT_STATUS=1
    FAILURES="$FAILURES"$'C++ tests failed\n'
  fi

  if [ -n "${BUILD_URL:-}" ]; then
    echo
    echo "Failed test logs:"
    for failed_test_name in $(
      egrep "[0-9]+/[0-9]+ +Test +#[0-9]+: +[A-Za-z0-9_-]+ .*Failed" "$CTEST_OUTPUT_PATH" | \
        awk '{print $4}'
    ); do
      # E.g.
      # https://jenkins.dev.yugabyte.com/job/yugabyte-ubuntu/116/artifact/build/debug/test-logs/block_manager_util-test.txt
      echo "  - $TEST_LOG_URL_PREFIX/$failed_test_name.txt"
    done
    echo
  fi

  if [ "$DO_COVERAGE" == "1" ]; then
    echo
    echo Generating coverage report...
    echo ------------------------------------------------------------
    if ! $SOURCE_ROOT/thirdparty/gcovr-3.0/scripts/gcovr -r $SOURCE_ROOT --xml \
        > $BUILD_ROOT/coverage.xml ; then
      EXIT_STATUS=1
      FAILURES="$FAILURES"$'Coverage report failed\n'
    fi
  fi
fi

if [ "$BUILD_JAVA" == "1" ]; then
  echo
  echo Building and testing java...
  echo ------------------------------------------------------------
  # Make sure we use JDK7
  export JAVA_HOME=$JAVA7_HOME
  export PATH=$JAVA_HOME/bin:$PATH
  pushd $SOURCE_ROOT/java
  export TSAN_OPTIONS="$TSAN_OPTIONS suppressions=$SOURCE_ROOT/build-support/tsan-suppressions.txt history_size=7"
  set -x
  VALIDATE_CSD_FLAG=""
  if [ "$VALIDATE_CSD" == "1" ]; then
    VALIDATE_CSD_FLAG="-PvalidateCSD"
  fi
  if ! mvn $MVN_FLAGS -PbuildCSD \
      $VALIDATE_CSD_FLAG \
      -Dsurefire.rerunFailingTestsCount=3 \
      -Dfailsafe.rerunFailingTestsCount=3 \
      clean verify ; then
    EXIT_STATUS=1
    FAILURES="$FAILURES"$'Java build/test failed\n'
  fi
  set +x
  popd
fi


if [ "$BUILD_PYTHON" == "1" ]; then
  echo
  echo Building and testing python.
  echo ------------------------------------------------------------

  # Failing to compile the Python client should result in a build failure
  set -e
  export YB_HOME=$SOURCE_ROOT
  export YB_BUILD=$BUILD_ROOT
  pushd $SOURCE_ROOT/python

  # Create a sane test environment
  rm -Rf "$YB_BUILD/py_env"
  virtualenv $YB_BUILD/py_env
  source $YB_BUILD/py_env/bin/activate
  pip install --upgrade pip
  CC=$CLANG CXX=$CLANG++ pip install --disable-pip-version-check -r requirements.txt

  # Delete old Cython extensions to force them to be rebuilt.
  rm -Rf build kudu_python.egg-info kudu/*.so

  # Assuming we run this script from base dir
  CC=$CLANG CXX=$CLANG++ python setup.py build_ext
  set +e
  if ! python setup.py test \
      --addopts="kudu --junit-xml=$YB_BUILD/test-logs/python_client.xml" \
      2> $YB_BUILD/test-logs/python_client.log ; then
    EXIT_STATUS=1
    FAILURES="$FAILURES"$'Python tests failed\n'
  fi
  popd
fi

if [ $EXIT_STATUS != 0 ]; then
  echo
  echo Tests failed, making sure we have XML files for all tests.
  echo ------------------------------------------------------------

  # Tests that crash do not generate JUnit report XML files.
  # We go through and generate a kind of poor-man's version of them in those cases.
  for GTEST_OUTFILE in $TEST_LOG_DIR/*.${TEST_OUTPUT_EXTENSION}; do
    if [[ "$GTEST_OUTFILE" =~ __(raw|stack_trace_filter_err).txt ]]; then
      continue
    fi
    TEST_EXE=$(basename "$GTEST_OUTFILE" .${TEST_OUTPUT_EXTENSION} )
    GTEST_XMLFILE="$TEST_LOG_DIR/$TEST_EXE.xml"
    if [ ! -f "$GTEST_XMLFILE" ]; then
      echo "JUnit report missing:" \
           "generating fake JUnit report file from $GTEST_OUTFILE and saving it to $GTEST_XMLFILE"
      $CAT_TEST_OUTPUT "$GTEST_OUTFILE" | \
        $SOURCE_ROOT/build-support/parse_test_failure.py -x >"$GTEST_XMLFILE"
      if [ "$IS_MAC" == "1" ] && \
         $CAT_TEST_OUTPUT "$GTEST_OUTFILE" | grep "Referenced from: " >/dev/null; then
        MISSING_LIBRARY_REFERENCED_FROM=$(
          $CAT_TEST_OUTPUT "$GTEST_OUTFILE" | grep "Referenced from: " | \
            head -1 | awk '{print $NF}'
        )
        echo
        echo "$GTEST_OUTFILE says there is a missing library" \
             "referenced from $MISSING_LIBRARY_REFERENCED_FROM"
        echo "Output from otool -L '$MISSING_LIBRARY_REFERENCED_FROM':"
        otool -L "$MISSING_LIBRARY_REFERENCED_FROM"
        echo
      fi

    fi
  done
fi

# If all tests passed, ensure that they cleaned up their test output.
#
# TODO: Python is currently leaking a tmp directory sometimes (KUDU-1301).
# Temporarily disabled until that's fixed.
#
# if [ $EXIT_STATUS == 0 ]; then
#   TEST_TMPDIR_CONTENTS=$(ls $TEST_TMPDIR)
#   if [ -n "$TEST_TMPDIR_CONTENTS" ]; then
#     echo "All tests passed, yet some left behind their test output:"
#     for SUBDIR in $TEST_TMPDIR_CONTENTS; do
#       echo $SUBDIR
#     done
#     EXIT_STATUS=1
#   fi
# fi

set -e

if [ -n "$FAILURES" ]; then
  echo Failure summary
  echo ------------------------------------------------------------
  echo $FAILURES
fi

exit $EXIT_STATUS
