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
#   YB_BUILD_CPP
#   Default: 1
#     Build and test C++ code if this is set to 1.
#
#   SKIP_CPP_MAKE
#   Default: 0
#     Skip building C++ code, only run tests if this is set to 1 (useful for debugging).
#
#   YB_BUILD_JAVA
#   Default: 1
#     Build and test java code if this is set to 1.
#
#   YB_BUILD_PYTHON
#   Default: 1
#     Build and test the Python wrapper of the client API.
#
#   DONT_DELETE_BUILD_ROOT
#   Default: 0 (meaning build root will be deleted) on Jenkins, 1 (don't delete) locally.
#     Skip deleting BUILD_ROOT (useful for debugging).
#
# Portions Copyright (c) YugaByte, Inc.

set -euo pipefail

. "${BASH_SOURCE%/*}/../common-test-env.sh"

export TSAN_OPTIONS=""

if [[ $OSTYPE =~ ^darwin ]]; then
  # This is needed to make sure we're using Homebrew-installed CMake on Mac OS X.
  export PATH=/usr/local/bin:$PATH
fi

MAX_NUM_PARALLEL_TESTS=3

YB_BUILD_CPP=${YB_BUILD_CPP:-1}

# gather core dumps
ulimit -c unlimited

BUILD_TYPE=${BUILD_TYPE:-debug}
build_type=$BUILD_TYPE
normalize_build_type
readonly build_type
BUILD_TYPE=$build_type
readonly BUILD_TYPE

set_cmake_build_type_and_compiler_type

set_build_root --no-readonly

set_common_test_paths

# TODO: deduplicate this with similar logic in yb-jenkins-build.sh.
YB_BUILD_JAVA=${YB_BUILD_JAVA:-1}
YB_BUILD_PYTHON=${YB_BUILD_PYTHON:-0}
YB_BUILD_CPP=${YB_BUILD_CPP:-1}
if is_jenkins; then
  # Delete the build root by default on Jenkins.
  DONT_DELETE_BUILD_ROOT=${DONT_DELETE_BUILD_ROOT:-0}
else
  log "Not running on Jenkins, not deleting the build root by default."
  # Don't delete the build root by default.
  DONT_DELETE_BUILD_ROOT=${DONT_DELETE_BUILD_ROOT:-1}
fi
SKIP_CPP_MAKE=${SKIP_CPP_MAKE:-0}

LATEST_BUILD_LINK="$YB_SRC_ROOT/build/latest"
CTEST_OUTPUT_PATH="$BUILD_ROOT"/ctest.log
CTEST_FULL_OUTPUT_PATH="$BUILD_ROOT"/ctest-full.log

# Remove testing artifacts from the previous run before we do anything else. Otherwise, if we fail
# during the "build" step, Jenkins will archive the test logs from the previous run, thinking they
# came from this run, and confuse us when we look at the failed build.

build_root_deleted=false
if [[ $DONT_DELETE_BUILD_ROOT == "0" ]]; then
  if [[ -L $BUILD_ROOT ]]; then
    # If the build root is a symlink, we have to find out what it is pointing to and delete that
    # directory as well.
    build_root_real_path=$( readlink "$BUILD_ROOT" )
    log "BUILD_ROOT ('$BUILD_ROOT') is a symlink to '$build_root_real_path'"
    rm -rf "$build_root_real_path"
    unlink "$BUILD_ROOT"
    build_root_deleted=true
  else
    log "Deleting BUILD_ROOT ('$BUILD_ROOT')."
    rm -rf "$BUILD_ROOT"
    build_root_deleted=true
  fi
fi

if ! $build_root_deleted; then
  log "Skipped deleting BUILD_ROOT ('$BUILD_ROOT'), only deleting $YB_TEST_LOG_ROOT_DIR."
  rm -rf "$YB_TEST_LOG_ROOT_DIR"
fi

if [[ ! -d $BUILD_ROOT ]]; then
  create_dir_on_ephemeral_drive "$BUILD_ROOT" "build/${BUILD_ROOT##*/}"
fi

if [[ -h $BUILD_ROOT ]]; then
  # If we ended up creating BUILD_ROOT as a symlink to an ephemeral drive, now make BUILD_ROOT
  # actually point to the target of that symlink.
  BUILD_ROOT=$( readlink "$BUILD_ROOT" )
fi
readonly BUILD_ROOT
export BUILD_ROOT

TEST_LOG_DIR="$BUILD_ROOT/test-logs"
TEST_TMP_ROOT_DIR="$BUILD_ROOT/test-tmp"

cleanup() {
  if [[ -n ${BUILD_ROOT:-} && $DONT_DELETE_BUILD_ROOT == "0" ]]; then
    log "Running the script to clean up build artifacts..."
    "$YB_SRC_ROOT/build-support/jenkins/post-build-clean.sh"
  fi
}

# If we're running inside Jenkins (the BUILD_ID is set), then install an exit handler which will
# clean up all of our build results.
if is_jenkins; then
  trap cleanup EXIT
fi

export TOOLCHAIN_DIR=/opt/toolchain
if [ -d "$TOOLCHAIN_DIR" ]; then
  PATH=$TOOLCHAIN_DIR/apache-maven-3.0/bin:$PATH
fi

configure_remote_build

should_build_thirdparty=true
if is_src_root_on_nfs && [[ -d $NFS_PARENT_DIR_FOR_SHARED_THIRDPARTY ]]; then
  # TODO: make this option available in yb_build.sh as well.
  set +e
  # We name shared prebuilt thirdparty directories on NFS like this:
  # /n/jenkins/thirdparty/yugabyte-thirdparty-YYYY-MM-DDTHH_MM_SS
  # This is why sorting and taking the last entry makes sense below.
  existing_thirdparty_dir=$(
    ls -d "$NFS_PARENT_DIR_FOR_SHARED_THIRDPARTY/yugabyte-thirdparty-"*/thirdparty | sort | tail -1
  )
  set -e
  if [[ -d $existing_thirdparty_dir ]]; then
    log "Using existing third-party dependencies from $existing_thirdparty_dir"
    export YB_THIRDPARTY_DIR=$existing_thirdparty_dir
    should_build_thirdparty=false
  else
    log "Even though the top-level directory '$NFS_PARENT_DIR_FOR_SHARED_THIRDPARTY'" \
        "exists, we could not find a prebuilt shared third-party directory there (got" \
        "$existing_thirdparty_dir. Falling back to building our own third-party dependencies."
  fi
fi

if "$should_build_thirdparty"; then
  log "Starting third-party dependency build"
  time thirdparty/build-thirdparty.sh
  log "Third-party dependency build finished (see timing information above)"
fi

# We built or found third-party dependencies above. Do not attempt to download or build them in in
# further steps.
export YB_NO_DOWNLOAD_PREBUILT_THIRDPARTY=1
export NO_REBUILD_THIRDPARTY=1

THIRDPARTY_BIN=$YB_SRC_ROOT/thirdparty/installed/bin
export PPROF_PATH=$THIRDPARTY_BIN/pprof

if which ccache >/dev/null ; then
  CLANG=$YB_SRC_ROOT/build-support/ccache-clang/clang
else
  CLANG=$YB_SRC_ROOT/thirdparty/clang-toolchain/bin/clang
fi

# Configure the build
#
# ASAN/TSAN can't build the Python bindings because the exported YB client
# library (which the bindings depend on) is missing ASAN/TSAN symbols.

cd "$BUILD_ROOT"
cmake_cmd_line="cmake ${cmake_opts[@]}"
DO_COVERAGE=0
EXTRA_TEST_FLAGS=""
# TODO: use "case" below.
if [[ $BUILD_TYPE == "asan" ]]; then
  log "Starting ASAN build"
  run_build_cmd $cmake_cmd_line "$YB_SRC_ROOT"
  log "CMake invocation for ASAN build finished (see timing information above)"
  YB_BUILD_PYTHON=0
elif [[ $BUILD_TYPE == "tsan" ]]; then
  log "Starting TSAN build"
  run_build_cmd $cmake_cmd_line -DYB_USE_TSAN=1 "$YB_SRC_ROOT"
  log "CMake invocation for TSAN build finished (see timing information above)"
  EXTRA_TEST_FLAGS+=" -LE no_tsan"
  YB_BUILD_PYTHON=0
elif [[ $BUILD_TYPE == "coverage" ]]; then
  DO_COVERAGE=1
  log "Starting coverage build"
  run_build_cmd $cmake_cmd_line -DYB_GENERATE_COVERAGE=1 "$YB_SRC_ROOT"
  log "CMake invocation for coverage build finished (see timing information above)"
elif [[ $BUILD_TYPE == "lint" ]]; then
  # Create empty test logs or else Jenkins fails to archive artifacts, which
  # results in the build failing.
  mkdir -p Testing/Temporary
  mkdir -p "$TEST_LOG_DIR"

  log "Starting lint build"
  set +e
  time (
    set -e
    $cmake_cmd_line "$YB_SRC_ROOT"
    make lint
  ) 2>&1 | tee "$TEST_LOG_DIR"/lint.log
  exit_code=$?
  set -e
  log "Lint build finished (see timing information above)"
  exit $exit_code
elif [[ $SKIP_CPP_MAKE == "0" ]]; then
  log "Running CMake with CMAKE_BUILD_TYPE set to $cmake_build_type"
  run_build_cmd $cmake_cmd_line "$YB_SRC_ROOT"
  log "Finished running CMake with build type $BUILD_TYPE (see timing information above)"
fi

# Only enable test core dumps for certain build types.
if [[ $BUILD_TYPE != "asan" ]]; then
  # TODO: actually make this take effect. The issue is that we might not be able to set ulimit
  # unless the OS configuration enables us to.
  export YB_TEST_ULIMIT_CORE=unlimited
fi

NUM_PROCS=$(getconf _NPROCESSORS_ONLN)

# Cap the number of parallel tests to run at $MAX_NUM_PARALLEL_TESTS
if [[ $NUM_PROCS -gt $MAX_NUM_PARALLEL_TESTS ]]; then
  NUM_PARALLEL_TESTS=$MAX_NUM_PARALLEL_TESTS
else
  NUM_PARALLEL_TESTS=$NUM_PROCS
fi

declare -i EXIT_STATUS=0

set +e
if [[ -d /tmp/yb-port-locks ]]; then
  # Allow other users to also run minicluster tests on this machine.
  chmod a+rwx /tmp/yb-port-locks
fi
set -e

FAILURES=""

if [[ $YB_BUILD_CPP == "1" ]]; then
  if ! which ctest >/dev/null; then
    fatal "ctest not found, won't be able to run C++ tests"
  fi
fi

# -------------------------------------------------------------------------------------------------
# Build C++ code regardless of YB_BUILD_CPP, because we'll also need it for Java tests.

heading "Building C++ code."
if [[ $SKIP_CPP_MAKE == "0" ]]; then
  remote_opt=""
  if [[ ${YB_REMOTE_BUILD:-} == "1" ]]; then
    remote_opt="--remote"
  fi

  # Delegate the actual C++ build to the yb_build.sh script. Also explicitly specify the --remote
  # flag so that the worker list refresh script can capture it from ps output and bump the number
  # of workers to some minimum value.
  #
  # We're explicitly disabling third-party rebuilding here as we've already built third-party
  # dependencies (or downloaded them, or picked an existing third-party directory) above.
  time run_build_cmd "$YB_SRC_ROOT/yb_build.sh" $remote_opt \
    --no-rebuild-thirdparty \
    --skip-java \
    "$BUILD_TYPE" 2>&1 | \
    filter_boring_cpp_build_output
  if [[ ${PIPESTATUS[0]} -ne 0 ]]; then
    log "C++ build failed!"
    # TODO: perhaps we shouldn't even try to run C++ tests in this case?
    EXIT_STATUS=1
  fi

  log "Finished building C++ code (see timing information above)"
else
  log "Skipped building C++ code, only running tests"
fi

if [[ -h $LATEST_BUILD_LINK ]]; then
  # This helps prevent Jenkins from showing every test twice in test results.
  unlink "$LATEST_BUILD_LINK"
fi

log "Disk usage after C++ build:"
show_disk_usage

# End of the C++ code build.
# -------------------------------------------------------------------------------------------------

set_asan_tsan_options

if [[ $YB_BUILD_JAVA == "1" ]]; then
  # This sets the proper NFS-shared directory for Maven's local repository on Jenkins.
  set_mvn_local_repo

  heading "Building and testing java..."
  if [[ -n ${JAVA_HOME:-} ]]; then
    export PATH=$JAVA_HOME/bin:$PATH
  fi
  pushd "$YB_SRC_ROOT/java"

  ( set -x; mvn clean )

  # Use a unique version to avoid a race with other concurrent jobs on jar files that we install
  # into ~/.m2/repository.
  yb_java_project_version=yugabyte-jenkins-$( date +%Y%m%dT%H%M%S ).$RANDOM.$RANDOM.$$

  (
    set -x
    mvn versions:set -DnewVersion="$yb_java_project_version"
    git add -A .
    git commit -m "Updating version to $yb_java_project_version during testing"
  )

  java_build_cmd_line=( --fail-never -DbinDir="$BUILD_ROOT"/bin )
  if ! time build_yb_java_code_with_retries "${java_build_cmd_line[@]}" \
                                            -DskipTests clean install 2>&1; then
    EXIT_STATUS=1
    FAILURES+=$'Java build failed\n'
  fi
  log "Finished building Java code (see timing information above)"
  popd
fi

if spark_available; then
  if [[ $YB_BUILD_CPP == "1" || $YB_BUILD_JAVA == "1" ]]; then
    log "Will run tests on Spark"
    extra_args=""
    if [[ $YB_BUILD_JAVA == "1" ]]; then
      extra_args+="--java"
    fi
    if [[ $YB_BUILD_CPP == "1" ]]; then
      extra_args+=" --cpp"
    fi
    if ! run_tests_on_spark $extra_args; then
      EXIT_STATUS=1
      FAILURES+=$'Distributed tests on Spark (C++ and/or Java) failed\n'
      log "Some tests that were run on Spark failed"
    fi
    unset extra_args
  else
    log "Neither C++ or Java tests are enabled, nothing to run on Spark."
  fi
else
  # A single-node way of running tests (without Spark).

  if [[ $YB_BUILD_CPP == "1" ]]; then
    log "Run C++ tests in a non-distributed way"
    export GTEST_OUTPUT="xml:$TEST_LOG_DIR/" # Enable JUnit-compatible XML output.

    if ! spark_available; then
      log "Did not find Spark on the system, falling back to a ctest-based way of running tests"
      set +e
      time ctest -j$NUM_PARALLEL_TESTS ${EXTRA_TEST_FLAGS:-} \
          --output-log "$CTEST_FULL_OUTPUT_PATH" \
          --output-on-failure 2>&1 | tee "$CTEST_OUTPUT_PATH"
      if [ $? -ne 0 ]; then
        EXIT_STATUS=1
        FAILURES+=$'C++ tests failed\n'
      fi
      set -e
    fi
    log "Finished running C++ tests (see timing information above)"

    if [[ $DO_COVERAGE == "1" ]]; then
      heading "Generating coverage report..."
      if ! $YB_SRC_ROOT/thirdparty/gcovr-3.0/scripts/gcovr -r $YB_SRC_ROOT --xml \
          > $BUILD_ROOT/coverage.xml ; then
        EXIT_STATUS=1
        FAILURES+=$'Coverage report failed\n'
      fi
    fi
  fi

  if [[ $YB_BUILD_JAVA == "1" ]]; then
    pushd "$YB_SRC_ROOT/java"
    log "Running Java tests in a non-distributed way"
    if ! time build_yb_java_code_with_retries "${java_build_cmd_line[@]}" verify 2>&1; then
      EXIT_STATUS=1
      FAILURES+=$'Java tests failed\n'
    fi
    log "Finished running Java tests (see timing information above)"
    popd
  fi
fi


if [[ $YB_BUILD_CPP == "1" ]] && using_linuxbrew; then
  # -----------------------------------------------------------------------------------------------
  # Test package creation (i.e. relocating all the necessary libraries).  This only works on Linux
  # builds using Linuxbrew.

  packaged_dest_dir=${BUILD_ROOT}__packaged
  rm -rf "$packaged_dest_dir"
  log "Testing creating a distribution in '$packaged_dest_dir'"
  export PYTHONPATH=${PYTHONPATH:-}:$YB_SRC_ROOT/python
  "$YB_SRC_ROOT/python/yb/library_packager.py" \
    --build-dir "$BUILD_ROOT" \
    --dest-dir "$packaged_dest_dir"
  rm -rf "$packaged_dest_dir"
fi

if [[ $YB_BUILD_PYTHON == "1" ]]; then
  show_disk_usage

  heading "Building and testing python."

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
    FAILURES+=$'Python tests failed\n'
  fi
  popd
fi

set -e

if [[ -n $FAILURES ]]; then
  heading "Failure summary"
  echo >&2 "$FAILURES"
fi

exit $EXIT_STATUS
