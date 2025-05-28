#!/bin/bash

#
# Copyright (c) YugaByte, Inc.
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
set -euo pipefail

# shellcheck source=build-support/common-test-env.sh
. "${0%/*}/../common-test-env.sh"


print_help() {
  cat <<-EOT
Usage: ${0##*} <options>
Options:
  -h, --help
    Show help

Environment variables:
  JOB_NAME
    Jenkins job name.
  BUILD_TYPE
    Passed directly to build-and-test.sh. The default value is determined based on the job name
    if this environment variable is not specified or if the value is "auto".
  YB_NUM_TESTS_TO_RUN
    Maximum number of tests ctest should run before exiting. Used for testing Jenkins scripts.
EOT
}

echo "Jenkins Test script ${BASH_SOURCE[0]} is running"

while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help)
      print_help
      exit 0
    ;;
    *)
      echo "Invalid option: $1" >&2
      exit 1
  esac
done

JOB_NAME=${JOB_NAME:-}

if [ -n "${YB_NUM_TESTS_TO_RUN:-}" ]; then
  if [[ ! "$YB_NUM_TESTS_TO_RUN" =~ ^[0-9]+$ ]]; then
    echo "Invalid number of tests to run: $YB_NUM_TESTS_TO_RUN" >&2
    exit 1
  fi
  EXTRA_TEST_FLAGS="-I1,$YB_NUM_TESTS_TO_RUN"
fi

export YB_BUILD_JAVA=${YB_BUILD_JAVA:-1}
export YB_BUILD_CPP=${YB_BUILD_CPP:-1}

# =================================================================================================
# Main script
# =================================================================================================

log "Running with Bash version $BASH_VERSION"

cd "$YB_SRC_ROOT"

activate_virtualenv
set_pythonpath

# shellcheck source=build-support/jenkins/common-lto.sh
. "${BASH_SOURCE%/*}/common-lto.sh"

# -------------------------------------------------------------------------------------------------
# Build root setup
# -------------------------------------------------------------------------------------------------

# shellcheck disable=SC2119
set_build_root

set_common_test_paths

export BUILD_ROOT

# -------------------------------------------------------------------------------------------------

find_or_download_ysql_snapshots
find_or_download_thirdparty
detect_toolchain
log_thirdparty_and_toolchain_details

set_java_home

log "Running with PATH: ${PATH}"

export YB_RUN_AFFECTED_TESTS_ONLY=${YB_RUN_AFFECTED_TESTS_ONLY:-0}
log "YB_RUN_AFFECTED_TESTS_ONLY=${YB_RUN_AFFECTED_TESTS_ONLY}"

YB_COMPILE_ONLY=${YB_COMPILE_ONLY:-0}

CTEST_OUTPUT_PATH="${BUILD_ROOT}"/ctest.log
CTEST_FULL_OUTPUT_PATH="${BUILD_ROOT}"/ctest-full.log

TEST_LOG_DIR="${BUILD_ROOT}/test-logs"

export NO_REBUILD_THIRDPARTY=1

THIRDPARTY_BIN=$YB_SRC_ROOT/thirdparty/installed/bin
export PPROF_PATH=$THIRDPARTY_BIN/pprof

# Check for available YBC - shoud be no-op if done in the build stage
prep_ybc_testing
if [[ -d "$YB_SRC_ROOT/build/ybc" ]]; then
  export YB_TEST_YB_CONTROLLER=${YB_TEST_YB_CONTROLLER:-1}
  log "Setting YB_TEST_YB_CONTROLLER=$YB_TEST_YB_CONTROLLER"
else
  log "Did not find YBC binaries. Not setting YB_TEST_YB_CONTROLLER."
fi

cd "$BUILD_ROOT"

# Only enable test core dumps for certain build types.
if [[ ${BUILD_TYPE} != "asan" ]]; then
  # TODO: actually make this take effect. The issue is that we might not be able to set ulimit
  # unless the OS configuration enables us to.
  export YB_TEST_ULIMIT_CORE=unlimited
fi

detect_num_cpus

declare -i EXIT_STATUS=0

FAILURES=""

if [[ ${YB_ENABLE_YSQL_CONN_MGR:-} == "1" ]]; then
  export YB_ENABLE_YSQL_CONN_MGR_IN_TESTS=true
fi

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
        # The build-stage should have already generated a conf file based on modified files.
        test_conf_path="${BUILD_ROOT}/test_conf.json"
        # If the conf file is missing or we have a filter specified, we need to re-run.
        if [[ ! -f "${test_conf_path}" || -n "${YB_TEST_EXECUTION_FILTER_RE:-}" ]]; then
          current_git_commit=$(git rev-parse HEAD)
          ( set -x
            "$YB_SCRIPT_PATH_DEPENDENCY_GRAPH" \
                --build-root "${BUILD_ROOT}" \
                --git-commit "${YB_GIT_COMMIT_FOR_DETECTING_TESTS:-$current_git_commit}" \
                --output-test-config "${test_conf_path}" \
                affected
          )
        fi
        run_tests_extra_args+=( "--test_conf" "${test_conf_path}" )
      fi

      run_tests_extra_args+=( "--send_archive_to_workers" )

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
      unset run_tests_extra_args
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
planned_tests_path=${BUILD_ROOT}/planned_tests.json

if [[ -f $test_results_from_junit_xml_path &&
      -f $test_results_from_spark_path &&
      $NUM_REPETITIONS == 1 ]]; then
  (
    set -x
    "$YB_SCRIPT_PATH_ANALYZE_TEST_RESULTS" \
          "--aggregated-json-test-results=$test_results_from_junit_xml_path" \
          "--planned-tests=$planned_tests_path" \
          "--run-tests-on-spark-report=$test_results_from_spark_path" \
          "--archive-dir=$YB_SRC_ROOT" \
          "--successful-tests-out-path=$YB_SRC_ROOT/test_successes.txt" \
          "--test-list-out-path=$YB_SRC_ROOT/test_list.txt" \
          "--analysis-out-path=$YB_SRC_ROOT/test_analysis.txt"
  )
else
  if [[ ! -f $test_results_from_junit_xml_path ]]; then
    log "File $test_results_from_junit_xml_path does not exist"
  fi
  if [[ ! -f $test_results_from_spark_path ]]; then
    log "File $test_results_from_spark_path does not exist"
  fi
  if [[ $NUM_REPETITIONS != 1 ]]; then
    log "Analyze script cannot handle multiple repetitions."
  fi
  log "Not running $YB_SCRIPT_PATH_ANALYZE_TEST_RESULTS"
fi

if [[ -n ${FAILURES} ]]; then
  heading "Failure summary"
  echo >&2 "${FAILURES}"
fi

exit ${EXIT_STATUS}
