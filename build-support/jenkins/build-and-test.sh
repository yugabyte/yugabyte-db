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
#   DONT_DELETE_BUILD_ROOT
#   Default: 0 (meaning build root will be deleted) on Jenkins, 1 (don't delete) locally.
#     Skip deleting BUILD_ROOT (useful for debugging).
#
#   YB_TRACK_REGRESSIONS
#   Default: 0
#     Track regressions by re-running failed tests multiple times on the previous git commit.
#     The implementation of this feature is unfinished.
#
#   YB_COMPILE_ONLY
#   Default: 0
#     Compile the code and build a package, but don't run tests.
#
# Portions Copyright (c) YugaByte, Inc.

set -euo pipefail

. "${BASH_SOURCE%/*}/../common-test-env.sh"

# -------------------------------------------------------------------------------------------------
# Functions

build_cpp_code() {
  # Save the source root just in case, but this should not be necessary as we will typically run
  # this function in a separate process in case it is building code in a non-standard location
  # (i.e. in a separate directory where we rollback the last commit for regression tracking).
  local old_yb_src_root=$YB_SRC_ROOT

  expect_num_args 1 "$@"
  set_yb_src_root "$1"

  heading "Building C++ code in $YB_SRC_ROOT."
  if [[ $SKIP_CPP_MAKE == "0" ]]; then
    remote_opt=""
    if [[ ${YB_REMOTE_BUILD:-} == "1" ]]; then
      # This helps with our background script resizing the build cluster, because it looks at all
      # running build processes with the "--remote" option as of 08/2017.
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

  LATEST_BUILD_LINK="$YB_SRC_ROOT/build/latest"
  if [[ -h $LATEST_BUILD_LINK ]]; then
    # This helps prevent Jenkins from showing every test twice in test results.
    ( set -x; unlink "$LATEST_BUILD_LINK" )
  fi

  # Restore the old source root. See the comment at the top.
  set_yb_src_root "$old_yb_src_root"
}

cleanup() {
  if [[ -n ${BUILD_ROOT:-} && $DONT_DELETE_BUILD_ROOT == "0" ]]; then
    log "Running the script to clean up build artifacts..."
    "$YB_BUILD_SUPPORT_DIR/jenkins/post-build-clean.sh"
  fi
}

# =================================================================================================
# Main script
# =================================================================================================

cd "$YB_SRC_ROOT"

if ! "$YB_BUILD_SUPPORT_DIR/common-build-env-test.sh"; then
  fatal "Test of the common build environment failed, cannot proceed."
fi

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

check_python_script_syntax

# TODO: deduplicate this with similar logic in yb-jenkins-build.sh.
YB_BUILD_JAVA=${YB_BUILD_JAVA:-1}
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
YB_COMPILE_ONLY=${YB_COMPILE_ONLY:-0}

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

if is_jenkins; then
  log "Deleting yb-test-logs from all subdirectories of $YB_BUILD_PARENT_DIR so that Jenkins " \
      "does not get confused with old JUnit-style XML files."
  ( set -x; rm -rf "$YB_BUILD_PARENT_DIR"/*/yb-test-logs )
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

# If we're running inside Jenkins (the BUILD_ID is set), then install an exit handler which will
# clean up all of our build results.
if is_jenkins; then
  trap cleanup EXIT
fi

export TOOLCHAIN_DIR=/opt/toolchain
if [[ -d $TOOLCHAIN_DIR ]]; then
  PATH=$TOOLCHAIN_DIR/apache-maven-3.0/bin:$PATH
fi

configure_remote_build

should_build_thirdparty=true
parent_dir_for_shared_thirdparty=""
if is_src_root_on_nfs; then
  parent_dir_for_shared_thirdparty=$NFS_PARENT_DIR_FOR_SHARED_THIRDPARTY
fi

if [[ -d $parent_dir_for_shared_thirdparty ]]; then
  # TODO: make this option available in yb_build.sh as well.
  set +e
  # We name shared prebuilt thirdparty directories on NFS like this:
  # /n/jenkins/thirdparty/yugabyte-thirdparty-YYYY-MM-DDTHH_MM_SS
  # This is why sorting and taking the last entry makes sense below.
  set +e
  existing_thirdparty_dirs=( $(
    ls -d "$parent_dir_for_shared_thirdparty/yugabyte-thirdparty-"*/thirdparty | sort --reverse
  ) )
  set -e
  if [[ ${#existing_thirdparty_dirs[@]} -gt 0 ]]; then
    for existing_thirdparty_dir in "${existing_thirdparty_dirs[@]}"; do
      if [[ ! -d $existing_thirdparty_dir ]]; then
        log "Warning: third-party directory '$existing_thirdparty_dir' not found, skipping."
        continue
      fi
      if [[ -e $existing_thirdparty_dir/.yb_thirdparty_do_not_use ]]; then
        log "Skipping '$existing_thirdparty_dir' because of a 'do not use' flag file."
        continue
      fi
      if [[ -d $existing_thirdparty_dir ]]; then
        log "Using existing third-party dependencies from $existing_thirdparty_dir"
        if is_jenkins; then
          log "Cleaning the old dedicated third-party dependency build in '$YB_SRC_ROOT/thirdparty'"
          unset YB_THIRDPARTY_DIR
          "$YB_SRC_ROOT/thirdparty/clean_thirdparty.sh" --all
        fi
        export YB_THIRDPARTY_DIR=$existing_thirdparty_dir
        should_build_thirdparty=false
        break
      fi
    done
  fi
  if "$should_build_thirdparty"; then
    log "Even though the top-level directory '$parent_dir_for_shared_thirdparty'" \
        "exists, we could not find a prebuilt shared third-party directory there that exists " \
        "and does not have a 'do not use' flag file inside. Falling back to building our own " \
        "third-party dependencies."
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
  CLANG=$YB_BUILD_SUPPORT_DIR/ccache-clang/clang
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
elif [[ $BUILD_TYPE == "tsan" ]]; then
  log "Starting TSAN build"
  run_build_cmd $cmake_cmd_line -DYB_USE_TSAN=1 "$YB_SRC_ROOT"
  log "CMake invocation for TSAN build finished (see timing information above)"
  EXTRA_TEST_FLAGS+=" -LE no_tsan"
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

# Cap the number of parallel tests to run at $MAX_NUM_PARALLEL_TESTS
detect_num_cpus
if [[ $YB_NUM_CPUS -gt $MAX_NUM_PARALLEL_TESTS ]]; then
  NUM_PARALLEL_TESTS=$MAX_NUM_PARALLEL_TESTS
else
  NUM_PARALLEL_TESTS=$YB_NUM_CPUS
fi

declare -i EXIT_STATUS=0

set +e
if [[ -d /tmp/yb-port-locks ]]; then
  # Allow other users to also run minicluster tests on this machine.
  chmod a+rwx /tmp/yb-port-locks
fi
set -e

FAILURES=""

if [[ $YB_BUILD_CPP == "1" ]] && ! which ctest >/dev/null; then
  fatal "ctest not found, won't be able to run C++ tests"
fi

# -------------------------------------------------------------------------------------------------
# Build C++ code regardless of YB_BUILD_CPP, because we'll also need it for Java tests.

if [[ ${YB_TRACK_REGRESSIONS:-} == "1" ]]; then

  cd "$YB_SRC_ROOT"
  if ! git diff-index --quiet HEAD --; then
    fatal "Uncommitted changes found in '$YB_SRC_ROOT', cannot proceed."
  fi
  git_original_commit=$( git rev-parse --abbrev-ref HEAD )

  # Set up a separate directory that is one commit behind and launch a C++ build there in parallel
  # with the main C++ build.

  # TODO: we can probably do this in parallel with running the first batch of tests instead of in
  # parallel with compilation, so that we deduplicate compilation of almost identical codebases.

  YB_SRC_ROOT_REGR=${YB_SRC_ROOT}_regr
  heading "Preparing directory for regression tracking: $YB_SRC_ROOT_REGR"

  if [[ -e $YB_SRC_ROOT_REGR ]]; then
    log "Removing the existing contents of '$YB_SRC_ROOT_REGR'"
    time run_build_cmd rm -rf "$YB_SRC_ROOT_REGR"
    if [[ -e $YB_SRC_ROOT_REGR ]]; then
      log "Failed to remove '$YB_SRC_ROOT_REGR' right away"
      sleep 0.5
      if [[ -e $YB_SRC_ROOT_REGR ]]; then
        fatal "Failed to remove '$YB_SRC_ROOT_REGR'"
      fi
    fi
  fi

  log "Cloning '$YB_SRC_ROOT' to '$YB_SRC_ROOT_REGR'"
  time run_build_cmd git clone "$YB_SRC_ROOT" "$YB_SRC_ROOT_REGR"
  if [[ ! -d $YB_SRC_ROOT_REGR ]]; then
    log "Directory $YB_SRC_ROOT_REGR did not appear right away"
    sleep 0.5
    if [[ ! -d $YB_SRC_ROOT_REGR ]]; then
      fatal "Directory ''$YB_SRC_ROOT_REGR' still does not exist"
    fi
  fi

  cd "$YB_SRC_ROOT_REGR"
  git checkout "$git_original_commit^"
  git_commit_after_rollback=$( git rev-parse --abbrev-ref HEAD )
  log "Rolling back commit '$git_commit_after_rollback', currently at '$git_original_commit'"
  heading "Top commits in '$YB_SRC_ROOT_REGR' after reverting one commit:"
  git log -n 2

  (
    build_cpp_code "$PWD" 2>&1 | \
      while read output_line; do \
        echo "[base version build] $output_line"
      done
  ) &
  build_cpp_code_regr_pid=$!

  cd "$YB_SRC_ROOT"
fi
build_cpp_code "$YB_SRC_ROOT"

if [[ ${YB_TRACK_REGRESSIONS:-} == "1" ]]; then
  log "Waiting for building C++ code one commit behind (at $git_commit_after_rollback)" \
      "in $YB_SRC_ROOT_REGR"
  wait "$build_cpp_code_regr_pid"
fi

log "Disk usage after C++ build:"
show_disk_usage

# End of the C++ code build.
# -------------------------------------------------------------------------------------------------

set_asan_tsan_options

if [[ $YB_BUILD_JAVA == "1" ]]; then
  # This sets the proper NFS-shared directory for Maven's local repository on Jenkins.
  set_mvn_parameters

  heading "Building and testing java..."
  if [[ -n ${JAVA_HOME:-} ]]; then
    export PATH=$JAVA_HOME/bin:$PATH
  fi
  pushd "$YB_SRC_ROOT/java"

  ( set -x; mvn clean )

  # Use a unique version to avoid a race with other concurrent jobs on jar files that we install
  # into ~/.m2/repository.
  random_id=$( date +%Y%m%dT%H%M%S )_$RANDOM$RANDOM$RANDOM
  yb_java_project_version=yugabyte-jenkins-$random_id

  yb_new_group_id=org.yb$random_id
  find . -name "pom.xml" \
         -exec sed -i "s#<groupId>org[.]yb</groupId>#<groupId>$yb_new_group_id</groupId>#g" {} \;

  commit_msg="Updating version to $yb_java_project_version and groupId to $yb_new_group_id "
  commit_msg+="during testing"
  (
    set -x
    mvn versions:set -DnewVersion="$yb_java_project_version"
    git add -A .
    git commit -m "$commit_msg"
  )
  unset commit_msg

  java_build_cmd_line=( --fail-never -DbinDir="$BUILD_ROOT"/bin )
  if ! time build_yb_java_code_with_retries "${java_build_cmd_line[@]}" \
                                            -DskipTests clean install 2>&1; then
    EXIT_STATUS=1
    FAILURES+=$'Java build failed\n'
  fi
  log "Finished building Java code (see timing information above)"
  popd
fi

# -------------------------------------------------------------------------------------------------
# Run tests, either on Spark or locally.
# If YB_COMPILE_ONLY is set to 1, we skip running all tests (Java and C++).

if [[ $YB_COMPILE_ONLY != "1" ]]; then
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
fi

# Finished running tests.

log "Testing creating a distribution package"
"$YB_SRC_ROOT/yb_release" --force --skip_build

set -e

if [[ -n $FAILURES ]]; then
  heading "Failure summary"
  echo >&2 "$FAILURES"
fi

exit $EXIT_STATUS
