#!/usr/bin/env bash

set -euo pipefail

# shellcheck source=build-support/common-test-env.sh
. "${BASH_SOURCE%/*}/../build-support/common-test-env.sh"

readonly YB_SPARK_DIR=/opt/yb-build/spark/current

show_help() {
  cat <<-EOT
---------------------------------------------------------------------------------------------------
Help for the ${0##*/} wrapper script
---------------------------------------------------------------------------------------------------

A wrapper around the run_tests_on_spark.py script, predominantly for local testing.

Usage: ${0##*/} [<options>] [<run_tests_on_spark_args>]

  Run tests on Spark Tool.
Options:
  --help, -h
    Show help.
  --delete-old-results
    Delete old test results from the build directory.
  --aggregate-only
    Only aggregate test results without running tests.
  --start-local-spark
    Start the local Spark master and worker.

  <run_tests_on_spark_args>
    These arguments are passed to the run_tests_on_spark.py script.

---------------------------------------------------------------------------------------------------
Help for the run_tests_on_spark.py script
---------------------------------------------------------------------------------------------------

EOT
  "${YB_SRC_ROOT}/python/yugabyte/run_tests_on_spark.py" --help
}

start_spark_master() {
  log "Attempting to start Spark master"
  set +e
  "${YB_SPARK_DIR}/sbin/start-master.sh"
  start_master_exit_code=$?
  set -e
  if [[ $start_master_exit_code != 0 ]]; then
    log "start-master.sh returned exit code $start_master_exit_code. It is possible that the" \
        "master is already running."
  fi
}

start_spark_worker() {
  log "Attempting to start Spark worker"
  set +e
  "${YB_SPARK_DIR}/sbin/start-worker.sh" "$spark_url"
  start_worker_exit_code=$?
  set -e
  if [[ $start_worker_exit_code != 0 ]]; then
    log "start-worker.sh returned exit code $start_worker_exit_code. It is possible that the" \
        "worker is already running."
  fi
}

detect_spark_master_url() {
  # shellcheck disable=SC2012
  spark_master_log_path=$( ls -t /opt/yb-build/spark/current/logs/*.master.Master*.out | head -1 )

  if [[ ! -f ${spark_master_log_path} ]]; then
    fatal "Spark master log file not found at ${spark_master_log_path}"
  fi

  spark_url=$(
    grep -Po "(?<=Starting Spark master at )spark://[^ ]+:[0-9]+$" "$spark_master_log_path"
  )
  if [[ ! "$spark_url" =~ ^spark://[^\ ]+:[0-9]+$ ]]; then
    fatal "No valid Spark URL not found in $spark_master_log_path." \
          "Found URL: $spark_url"
  fi
}

# -------------------------------------------------------------------------------------------------

run_tests_on_spark_args=()
predefined_build_root=""
delete_old_results=false
start_local_spark=false
aggregate_only=false

if [[ $# -eq 0 ]]; then
  show_help >&2
  exit 1
fi

while [[ $# -gt 0 ]]; do
  case ${1//_/-} in
    -h|--help)
      show_help >&2
      exit 1
    ;;
    --spark-dir)
      YB_SPARK_DIR=$1
    ;;
    --build-root)
      BUILD_ROOT=$2
      predefined_build_root=$2
      shift
    ;;
    --delete-old-results)
      delete_old_results=true
    ;;
    --start-local-spark)
      start_local_spark=true
    ;;
    --aggregate-only)
      aggregate_only=true
    ;;
    *)
      run_tests_on_spark_args+=( "$1" )
    ;;
  esac
  shift
done

if [[ -z ${BUILD_ROOT:-} ]]; then
  fatal "--build-root is not specified"
fi

if [[ ! -d ${YB_SPARK_DIR} ]]; then
  fatal "Spark directory ${YB_SPARK_DIR} not found. Specify --spark-dir or set YB_SPARK_DIR."
fi

if [[ ${start_local_spark} == "true" ]]; then
  start_spark_master
fi

detect_spark_master_url

if [[ ${start_local_spark} == "true" ]]; then
  start_spark_worker
fi

handle_predefined_build_root_quietly=true
handle_predefined_build_root

if [[ ${delete_old_results} == "true" ]]; then
  if [[ ${aggregate_only} == "true" ]]; then
    log "Ignoring --delete-old-results because --aggregate-only is specified."
  else
    log "Deleting old test results from $BUILD_ROOT"
    (
      set -x
      rm -rf "${BUILD_ROOT}/yb-test-logs"
      find "${BUILD_ROOT}" -type f \
          -and \( \
          -name "*.log" -or \
          -name "*_test_report.json" -or \
          -name "*_build_report.json" -or \
          -name "*_build_report.json.gz" -or \
          -name "*.fatal_failure_details.*.txt" \
          \) \
          -delete
      find "${YB_SRC_ROOT}/java" \
          -wholename "${YB_SRC_ROOT}/java/*/target/surefire-reports/*" -and \( \
            -name "*_test_report.json" -or \
            -name "TEST-*.xml" -or  \
            -name "*-output.txt" -or \
            -name "*.fatal_failure_details.*.txt" \
          \) \
          -delete
      rm -f "${YB_SRC_ROOT}/test_results.json" \
            "${YB_SRC_ROOT}/test_failures.json"
    )
  fi
fi

if [[ ${aggregate_only} == "true" ]]; then
  log "Not running tests because --aggregate-only is specified."
else
  (
    set -x
    "${YB_SPARK_DIR}/bin/spark-submit" \
    "${YB_SRC_ROOT}/python/yugabyte/run_tests_on_spark.py" \
    --spark-master-url "$spark_url" \
    --build-root "${BUILD_ROOT}" \
    --save_report_to_build_dir \
    "${run_tests_on_spark_args[@]}"
  )
fi

activate_virtualenv
set_pythonpath
(
  set -x
  "$YB_SCRIPT_PATH_AGGREGATE_TEST_REPORTS" \
      --yb-src-root "${YB_SRC_ROOT}" \
      --output-dir "${YB_SRC_ROOT}" \
      --build-type "${build_type}" \
      --compiler-type "${YB_COMPILER_TYPE}" \
      --build-root "${BUILD_ROOT}"

  "$YB_SCRIPT_PATH_ANALYZE_TEST_RESULTS" \
      --aggregated-json-test-results "${YB_SRC_ROOT}/test_results.json" \
      --run-tests-on-spark-report "${BUILD_ROOT}/full_build_report.json.gz" \
)
