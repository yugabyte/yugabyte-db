#!/usr/bin/env bash

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
# This script is invoked from the Jenkins builds to build YB and run all the unit tests.
#
# Environment variables may be used to customize operation:
#   BUILD_TYPE: Default: debug
#     Maybe be one of asan|tsan|debug|release|coverage|lint
#
#   YB_BUILD_CPP
#   Default: 1
#     Build and test C++ code if this is set to 1.
#
#   YB_SKIP_BUILD
#   Default: 0
#     Skip building C++ and Java code, only run tests if this is set to 1 (useful for debugging).
#     This option is actually handled by yb_build.sh.
#
#   YB_BUILD_JAVA
#   Default: 1
#     Build and test java code if this is set to 1.
#
#   DONT_DELETE_BUILD_ROOT
#   Default: 0 (meaning build root will be deleted) on Jenkins, 1 (don't delete) locally.
#     Skip deleting BUILD_ROOT (useful for debugging).
#
#   YB_COMPILE_ONLY
#   Default: 0
#     Compile the code and build a package, but don't run tests.
#
#   YB_RUN_AFFECTED_TESTS_ONLY
#   Default: 0
#     Try to auto-detect the set of C++ tests to run for the current set of changes relative to
#     origin/master.

#
# Portions Copyright (c) YugaByte, Inc.

set -euo pipefail

echo "Build script ${BASH_SOURCE[0]} is running"

# shellcheck source=build-support/common-test-env.sh
. "${BASH_SOURCE%/*}/../common-test-env.sh"

# -------------------------------------------------------------------------------------------------
# Functions

cleanup() {
  if [[ -n ${BUILD_ROOT:-} && ${DONT_DELETE_BUILD_ROOT} == "0" ]]; then
    log "Running the script to clean up build artifacts..."
    "$YB_BUILD_SUPPORT_DIR/jenkins/post-build-clean.sh"
  fi
}

# =================================================================================================
# Main script
# =================================================================================================

log "Running with Bash version $BASH_VERSION"

cd "$YB_SRC_ROOT"
if ! "$YB_BUILD_SUPPORT_DIR/common-build-env-test.sh"; then
  fatal "Test of the common build environment failed, cannot proceed."
fi

activate_virtualenv
set_pythonpath

# shellcheck source=build-support/jenkins/common-lto.sh
. "${BASH_SOURCE%/*}/common-lto.sh"

# -------------------------------------------------------------------------------------------------
# Build root setup and build directory cleanup
# -------------------------------------------------------------------------------------------------

# shellcheck disable=SC2119
set_build_root

set_common_test_paths

# As soon as we know build root, we need to do the necessary workspace cleanup.
if is_jenkins; then
  # Delete the build root by default on Jenkins.
  DONT_DELETE_BUILD_ROOT=${DONT_DELETE_BUILD_ROOT:-0}
else
  log "Not running on Jenkins, not deleting the build root by default."
  # Don't delete the build root by default.
  DONT_DELETE_BUILD_ROOT=${DONT_DELETE_BUILD_ROOT:-1}
fi

export BUILD_ROOT

# -------------------------------------------------------------------------------------------------
# End of build root setup and build directory cleanup
# -------------------------------------------------------------------------------------------------

export YB_SKIP_LLVM_TOOLCHAIN_SYMLINK_CREATION=1

# We need to set this prior to the first invocation of yb_build.sh.
export YB_SKIP_FINAL_LTO_LINK=1

find_or_download_ysql_snapshots
find_or_download_thirdparty
validate_thirdparty_dir
detect_toolchain
log_thirdparty_and_toolchain_details
find_make_or_ninja_and_update_cmake_opts

log "YB_USE_NINJA=$YB_USE_NINJA"
log "YB_NINJA_PATH=${YB_NINJA_PATH:-undefined}"

set_java_home

export YB_DISABLE_LATEST_SYMLINK=1
remove_latest_symlink

if is_jenkins; then
  log "Running on Jenkins, will re-create the Python virtualenv"
  # YB_RECREATE_VIRTUALENV is used in common-build-env.sh.
  # shellcheck disable=SC2034
  YB_RECREATE_VIRTUALENV=1
fi

log "Running with PATH: ${PATH}"

set +e
for python_command in python python2 python2.7 python3; do
  log "Location of $python_command: $( which "$python_command" )"
done
set -e

log "Running Python tests"
time run_python_tests
log "Finished running Python tests (see timing information above)"

log "Running a light-weight lint script on our Java code"
time lint_java_code
log "Finished running a light-weight lint script on the Java code"

# TODO: deduplicate this with similar logic in yb-jenkins-build.sh.
YB_BUILD_JAVA=${YB_BUILD_JAVA:-1}
YB_BUILD_CPP=${YB_BUILD_CPP:-1}

if [[ -z ${YB_RUN_AFFECTED_TESTS_ONLY:-} ]] && is_jenkins_phabricator_build; then
  log "YB_RUN_AFFECTED_TESTS_ONLY is not set, and this is a Jenkins Phabricator test." \
      "Setting YB_RUN_AFFECTED_TESTS_ONLY=1 automatically."
  export YB_RUN_AFFECTED_TESTS_ONLY=1
fi
export YB_RUN_AFFECTED_TESTS_ONLY=${YB_RUN_AFFECTED_TESTS_ONLY:-0}
log "YB_RUN_AFFECTED_TESTS_ONLY=${YB_RUN_AFFECTED_TESTS_ONLY}"

export YB_SKIP_BUILD=${YB_SKIP_BUILD:-0}
if [[ ${YB_SKIP_BUILD} == "1" ]]; then
  export NO_REBUILD_THIRDPARTY=1
fi

YB_SKIP_CPP_COMPILATION=${YB_SKIP_CPP_COMPILATION:-0}
YB_COMPILE_ONLY=${YB_COMPILE_ONLY:-0}

CTEST_OUTPUT_PATH="${BUILD_ROOT}"/ctest.log
CTEST_FULL_OUTPUT_PATH="${BUILD_ROOT}"/ctest-full.log

TEST_LOG_DIR="${BUILD_ROOT}/test-logs"

# If we're running inside Jenkins (the BUILD_ID is set), then install an exit handler which will
# clean up all of our build results.
if is_jenkins; then
  trap cleanup EXIT
fi

configure_remote_compilation

export NO_REBUILD_THIRDPARTY=1

THIRDPARTY_BIN=$YB_SRC_ROOT/thirdparty/installed/bin
export PPROF_PATH=$THIRDPARTY_BIN/pprof

# Configure the build
#

cd "$BUILD_ROOT"

if [[ $YB_RUN_AFFECTED_TESTS_ONLY == "1" ]]; then
  (
    set -x
    # Remove the compilation command file, even if we have not deleted the build root.
    rm -f "$BUILD_ROOT/compile_commands.json"
  )
fi

# Only enable test core dumps for certain build types.
if [[ ${BUILD_TYPE} != "asan" ]]; then
  # TODO: actually make this take effect. The issue is that we might not be able to set ulimit
  # unless the OS configuration enables us to.
  export YB_TEST_ULIMIT_CORE=unlimited
fi

detect_num_cpus

declare -i EXIT_STATUS=0

set +e
if [[ -d /tmp/yb-port-locks ]]; then
  # Allow other users to also run minicluster tests on this machine.
  chmod a+rwx /tmp/yb-port-locks
fi
set -e

FAILURES=""

if [[ ${YB_BUILD_CPP} == "1" ]] && ! which ctest >/dev/null; then
  fatal "ctest not found, won't be able to run C++ tests"
fi

export YB_SKIP_INITIAL_SYS_CATALOG_SNAPSHOT=1

# -------------------------------------------------------------------------------------------------
# Running initdb
# -------------------------------------------------------------------------------------------------

export YB_SKIP_INITIAL_SYS_CATALOG_SNAPSHOT=0

# -------------------------------------------------------------------------------------------------
# Dependency graph analysis allowing to determine what tests to run.
# -------------------------------------------------------------------------------------------------

if [[ $YB_RUN_AFFECTED_TESTS_ONLY == "1" ]]; then
  if ! ( set -x
         "$YB_SCRIPT_PATH_DEPENDENCY_GRAPH" \
           --build-root "${BUILD_ROOT}" \
           self-test \
           --rebuild-graph ); then
    # Trying to diagnose this error:
    # https://gist.githubusercontent.com/mbautin/c5c6f14714f7655c10620d8e658e1f5b/raw
    log "dependency_graph.py failed, listing all pb.{h,cc} files in the build directory"
    ( set -x; find "$BUILD_ROOT" -name "*.pb.h" -or -name "*.pb.cc" )
    fatal "Dependency graph construction failed"
  fi
fi

# Save the current HEAD commit in case we build Java below and add a new commit. This is used for
# the following purposes:
# - So we can upload the release under the correct commit, from Jenkins, to then be picked up from
#   itest, from the snapshots bucket.
# - For picking up the changeset corresponding the the current diff being tested and detecting what
#   tests to run in Phabricator builds. If we just diff with origin/master, we'll always pick up
#   pom.xml changes we've just made, forcing us to always run Java tests.
current_git_commit=$(git rev-parse HEAD)

export YB_MVN_LOCAL_REPO=$BUILD_ROOT/m2_repository

# -------------------------------------------------------------------------------------------------
# Run tests, either on Spark or locally.
# If YB_COMPILE_ONLY is set to 1, we skip running all tests (Java and C++).

set_sanitizer_runtime_options

# To reduce Jenkins archive size, let's gzip Java logs and delete per-test-method logs in case
# of no test failures.
export YB_GZIP_PER_TEST_METHOD_LOGS=1
export YB_GZIP_TEST_LOGS=1
export YB_DELETE_SUCCESSFUL_PER_TEST_METHOD_LOGS=1

if [[ ${YB_COMPILE_ONLY} != "1" ]]; then
  if spark_available; then
    if [[ ${YB_BUILD_CPP} == "1" || ${YB_BUILD_JAVA} == "1" ]]; then
      log "Will run tests on Spark"
      run_tests_extra_args=()
      if [[ ${YB_BUILD_JAVA} == "1" ]]; then
        run_tests_extra_args+=( "--java" )
      fi
      if [[ ${YB_BUILD_CPP} == "1" ]]; then
        run_tests_extra_args+=( "--cpp" )
      fi
      if [[ ${YB_RUN_AFFECTED_TESTS_ONLY} == "1" ]]; then
        test_conf_path="${BUILD_ROOT}/test_conf.json"
        # YB_GIT_COMMIT_FOR_DETECTING_TESTS allows overriding the commit to use to detect the set
        # of tests to run. Useful when testing this script.
        (
          set -x
          "$YB_SCRIPT_PATH_DEPENDENCY_GRAPH" \
              --build-root "${BUILD_ROOT}" \
              --git-commit "${YB_GIT_COMMIT_FOR_DETECTING_TESTS:-$current_git_commit}" \
              --output-test-config "${test_conf_path}" \
              affected
        )
        run_tests_extra_args+=( "--test_conf" "${test_conf_path}" )
        unset test_conf_path
      fi
      if is_linux || (is_mac && ! is_src_root_on_nfs); then
        log "Will create an archive for Spark workers with all the code instead of using NFS."
        run_tests_extra_args+=( "--send_archive_to_workers" )
      fi
      # Workers use /private path, which caused mis-match when check is done by yb_dist_tests that
      # YB_MVN_LOCAL_REPO is in source tree. So unsetting value here to allow default.
      if is_mac; then
        unset YB_MVN_LOCAL_REPO
      fi

      NUM_REPETITIONS="${YB_NUM_REPETITIONS:-1}"
      log "NUM_REPETITIONS is set to ${NUM_REPETITIONS}"
      if [[ ${NUM_REPETITIONS} -gt 1 ]]; then
        log "Repeating each test ${NUM_REPETITIONS} times"
        run_tests_extra_args+=( "--num_repetitions" "${NUM_REPETITIONS}" )
      fi

      set +u  # because extra_args can be empty
      if ! run_tests_on_spark "${run_tests_extra_args[@]}"; then
        set -u
        EXIT_STATUS=1
        FAILURES+=$'Distributed tests on Spark (C++ and/or Java) failed\n'
        log "Some tests that were run on Spark failed"
      fi
      set -u
      unset extra_args
    else
      log "Neither C++ or Java tests are enabled, nothing to run on Spark."
    fi
  else
    # A single-node way of running tests (without Spark).

    if [[ ${YB_BUILD_CPP} == "1" ]]; then
      log "Run C++ tests in a non-distributed way"
      export GTEST_OUTPUT="xml:${TEST_LOG_DIR}/" # Enable JUnit-compatible XML output.

      if ! spark_available; then
        log "Did not find Spark on the system, falling back to a ctest-based way of running tests"
        set +e
        # We don't double-quote EXTRA_TEST_FLAGS on purpose, to allow specifying multiple flags.
        # shellcheck disable=SC2086
        time ctest "-j${NUM_PARALLEL_TESTS}" ${EXTRA_TEST_FLAGS:-} \
            --output-log "${CTEST_FULL_OUTPUT_PATH}" \
            --output-on-failure 2>&1 | tee "${CTEST_OUTPUT_PATH}"
        ctest_exit_code=$?
        set -e
        if [[ $ctest_exit_code -ne 0 ]]; then
          EXIT_STATUS=1
          FAILURES+=$'C++ tests failed with exit code ${ctest_exit_code}\n'
        fi
      fi
      log "Finished running C++ tests (see timing information above)"
    fi

    if [[ ${YB_BUILD_JAVA} == "1" ]]; then
      set_test_invocation_id
      log "Running Java tests in a non-distributed way"
      if ! time run_all_java_test_methods_separately; then
        EXIT_STATUS=1
        FAILURES+=$'Java tests failed\n'
      fi
      log "Finished running Java tests (see timing information above)"
      # shellcheck disable=SC2119
      kill_stuck_processes
    fi
  fi
fi

# Finished running tests.
remove_latest_symlink

log "Aggregating test reports"
"$YB_SCRIPT_PATH_AGGREGATE_TEST_REPORTS" \
      --yb-src-root "${YB_SRC_ROOT}" \
      --output-dir "${YB_SRC_ROOT}" \
      --build-type "${build_type}" \
      --compiler-type "${YB_COMPILER_TYPE}" \
      --build-root "${BUILD_ROOT}"

log "Analyzing test results"
test_results_from_junit_xml_path=${YB_SRC_ROOT}/test_results.json
test_results_from_spark_path=${BUILD_ROOT}/full_build_report.json.gz

if [[ -f $test_results_from_junit_xml_path &&
      -f $test_results_from_spark_path ]]; then
  (
    set -x
    "$YB_SCRIPT_PATH_ANALYZE_TEST_RESULTS" \
          "--aggregated-json-test-results=$test_results_from_junit_xml_path" \
          "--run-tests-on-spark-report=$test_results_from_spark_path" \
          "--archive-dir=$YB_SRC_ROOT" \
          "--successful-tests-out-path=$YB_SRC_ROOT/successful_tests.txt" \
          "--test-list-out-path=$YB_SRC_ROOT/test_list.txt"
  )
else
  if [[ ! -f $test_results_from_junit_xml_path ]]; then
    log "File $test_results_from_junit_xml_path does not exist"
  fi
  if [[ ! -f $test_results_from_spark_path ]]; then
    log "File $test_results_from_spark_path does not exist"
  fi
  log "Not running $YB_SCRIPT_PATH_ANALYZE_TEST_RESULTS"
fi

if [[ -n ${FAILURES} ]]; then
  heading "Failure summary"
  echo >&2 "${FAILURES}"
fi

exit ${EXIT_STATUS}
