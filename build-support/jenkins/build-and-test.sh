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
#   BUILD_TYPE: Default: debug
#     Maybe be one of asan|tsan|debug|release|coverage|lint
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

MAX_NUM_PARALLEL_TESTS=8

# If a commit messages contains a line that says 'DONT_BUILD', exit
# immediately.
DONT_BUILD=$(git show|egrep '^\s{4}DONT_BUILD$')
if [ -n "$DONT_BUILD" ]; then
  echo "*** Build not requested. Exiting."
  exit 1
fi

if [ "`uname`" == "Darwin" ]; then
  IS_MAC=1
else
  IS_MAC=0
fi

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

BUILD_TYPE=${BUILD_TYPE:-debug}
BUILD_TYPE=$(echo "$BUILD_TYPE" | tr A-Z a-z)

if [ "$BUILD_TYPE" = "tsan" ]; then
  export YB_USE_TSAN=1
fi

BUILD_JAVA=${BUILD_JAVA:-1}
VALIDATE_CSD=${VALIDATE_CSD:-0}
BUILD_PYTHON=${BUILD_PYTHON:-1}

. "${0%/*}/../common-test-env.sh"

set_build_root "$BUILD_TYPE"

LATEST_BUILD_LINK="$YB_SRC_ROOT/build/latest"
CTEST_OUTPUT_PATH="$BUILD_ROOT"/ctest.log

if [ "$IS_MAC" == "1" ]; then
  export DYLD_FALLBACK_LIBRARY_PATH="$BUILD_ROOT/rocksdb-build"
  export DYLD_FALLBACK_LIBRARY_PATH+=":$YB_SRC_ROOT/thirdparty/gflags-2.1.2/lib"
  echo "Set DYLD_FALLBACK_LIBRARY_PATH to $DYLD_FALLBACK_LIBRARY_PATH"
fi

# Remove testing artifacts from the previous run before we do anything
# else. Otherwise, if we fail during the "build" step, Jenkins will
# archive the test logs from the previous run, thinking they came from
# this run, and confuse us when we look at the failed build.
rm -rf "$BUILD_ROOT"
mkdir -p "$BUILD_ROOT"

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
    $YB_SRC_ROOT/build-support/jenkins/post-build-clean.sh
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

log "Starting third-party dependency build"
time $YB_SRC_ROOT/build-support/enable_devtoolset.sh thirdparty/build-if-necessary.sh
log "Third-party dependency build finished (see timing information above)"

THIRDPARTY_BIN=$(pwd)/thirdparty/installed/bin
export PPROF_PATH=$THIRDPARTY_BIN/pprof

if which ccache >/dev/null ; then
  CLANG=$(pwd)/build-support/ccache-clang/clang
else
  CLANG=$(pwd)/thirdparty/clang-toolchain/bin/clang
fi

enable_devtoolset_script=$YB_SRC_ROOT/build-support/enable_devtoolset.sh
# Configure the build
#
# ASAN/TSAN can't build the Python bindings because the exported YB client
# library (which the bindings depend on) is missing ASAN/TSAN symbols.
cd "$BUILD_ROOT"
if [ "$BUILD_TYPE" = "asan" ]; then
  log "Starting ASAN build"
  time "$enable_devtoolset_script" \
    "env CC=$CLANG CXX=$CLANG++ $THIRDPARTY_BIN/cmake -DYB_USE_ASAN=1 -DYB_USE_UBSAN=1 $YB_SRC_ROOT"
  log "ASAN build finished (see timing information above)"
  BUILD_TYPE=fastdebug
  BUILD_PYTHON=0
elif [ "$BUILD_TYPE" = "tsan" ]; then
  log "Starting TSAN build"
  time "$enable_devtoolset_script" \
    "env CC=$CLANG CXX=$CLANG++ $THIRDPARTY_BIN/cmake -DYB_USE_TSAN=1 $YB_SRC_ROOT"
  log "TSAN build finished (see timing information above)"
  BUILD_TYPE=fastdebug
  EXTRA_TEST_FLAGS="$EXTRA_TEST_FLAGS -LE no_tsan"
  BUILD_PYTHON=0
elif [ "$BUILD_TYPE" = "coverage" ]; then
  DO_COVERAGE=1
  BUILD_TYPE=debug
  log "Starting coverage build"
  time "$enable_devtoolset_script" "$THIRDPARTY_BIN/cmake -DYB_GENERATE_COVERAGE=1 $YB_SRC_ROOT"
  log "Coverage build finished (see timing information above)"
elif [ "$BUILD_TYPE" = "lint" ]; then
  # Create empty test logs or else Jenkins fails to archive artifacts, which
  # results in the build failing.
  mkdir -p Testing/Temporary
  mkdir -p "$TEST_LOG_DIR"

  log "Starting lint build"
  set +e
  time (
    set -e
    "$enable_devtoolset_script" "$THIRDPARTY_BIN/cmake $YB_SRC_ROOT"
    make lint
  ) | tee "$TEST_LOG_DIR"/lint.log
  exit_code=$?
  log "Lint build finished (see timing information above)"
  exit $exit_code
  set -e
fi

# Only enable test core dumps for certain build types.
if [ "$BUILD_TYPE" != "asan" ]; then
  # TODO: actually make this take effect. The issue is that we might not be able to set ulimit
  # unless the OS configuration enables us to.
  export YB_TEST_ULIMIT_CORE=unlimited
fi

log "Running CMake with build type $BUILD_TYPE"
time $YB_SRC_ROOT/build-support/enable_devtoolset.sh \
  "$THIRDPARTY_BIN/cmake -DCMAKE_BUILD_TYPE=${BUILD_TYPE} $YB_SRC_ROOT"
log "Finished running CMake with build type $BUILD_TYPE (see timing information above)"

NUM_PROCS=$(getconf _NPROCESSORS_ONLN)

# Cap the number of parallel tests to run at $MAX_NUM_PARALLEL_TESTS
if [ "$NUM_PROCS" -gt "$MAX_NUM_PARALLEL_TESTS" ]; then
  NUM_PARALLEL_TESTS=$MAX_NUM_PARALLEL_TESTS
else
  NUM_PARALLEL_TESTS=$NUM_PROCS
fi

if [ "$BUILD_CPP" == "1" ]; then
  echo
  echo Building C++ code.
  echo ------------------------------------------------------------
  time make -j$NUM_PROCS $EXTRA_MAKE_ARGS 2>&1 | tee build.log
  log "Finished building C++ code (see timing information above)"

  if [ -h "$LATEST_BUILD_LINK" ]; then
    # This helps prevent Jenkins from showing every test twice in test results.
    unlink "$LATEST_BUILD_LINK"
  fi

  "$YB_SRC_ROOT"/build-support/fix_rpath.py --build-root "$BUILD_ROOT"

  # If compilation succeeds, try to run all remaining steps despite any failures.
  set +e

  # Run tests
  export GTEST_OUTPUT="xml:$TEST_LOG_DIR/" # Enable JUnit-compatible XML output.

  EXIT_STATUS=0
  FAILURES=""

  log "Starting ctest"
  set +e
  time (
    set -x
    time $THIRDPARTY_BIN/ctest -j$NUM_PARALLEL_TESTS $EXTRA_TEST_FLAGS --output-on-failure 2>&1 | \
      tee "$CTEST_OUTPUT_PATH"
  )
  if [ $? -ne 0 ]; then
    EXIT_STATUS=1
    FAILURES="$FAILURES"$'C++ tests failed\n'
  fi
  set -e
  log "Finished running ctest (see timing information above)"

  if [ "$DO_COVERAGE" == "1" ]; then
    echo
    echo Generating coverage report...
    echo ------------------------------------------------------------
    if ! $YB_SRC_ROOT/thirdparty/gcovr-3.0/scripts/gcovr -r $YB_SRC_ROOT --xml \
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
  pushd $YB_SRC_ROOT/java
  export TSAN_OPTIONS="$TSAN_OPTIONS suppressions=$YB_SRC_DIR/build-support/tsan-suppressions.txt history_size=7"
  set -x
  VALIDATE_CSD_FLAG=""
  if [ "$VALIDATE_CSD" == "1" ]; then
    VALIDATE_CSD_FLAG="-PvalidateCSD"
  fi
  if ! mvn -DskipTests $MVN_FLAGS -PbuildCSD \
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
  export YB_HOME=$YB_SRC_ROOT
  export YB_BUILD=$BUILD_ROOT
  pushd $YB_SRC_ROOT/python

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

set -e

if [ -n "$FAILURES" ]; then
  echo Failure summary
  echo ------------------------------------------------------------
  echo $FAILURES
fi

exit $EXIT_STATUS
