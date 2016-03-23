#!/bin/bash

set -euo pipefail

function print_help() {
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
  shift
done

# TODO: determine BUILD_TYPE automatically based on JOB_NAME if not specified.

rm -rf build/debug/test-logs/*

if [ -n "${YB_NUM_TESTS_TO_RUN:-}" ]; then

  if [[ ! "$YB_NUM_TESTS_TO_RUN" =~ ^[0-9]+$ ]]; then
    echo "Invalid number of tests to run: $YB_NUM_TESTS_TO_RUN" >&2
    exit 1
  fi
  export EXTRA_TEST_FLAGS="-I1,$YB_NUM_TESTS_TO_RUN"
fi

export YB_MINIMIZE_VERSION_DEFINES_CHANGES=1
export YB_MINIMIZE_RECOMPILATION=1
export BUILD_PYTHON=0
export BUILD_JAVA=0
export YB_KEEP_BUILD_ARTIFACTS=true

set +e
build-support/jenkins/build-and-test.sh
exit_code=$?
set -e

# Un-gzip build log files for easy viewing in the Jenkins UI.
for f in build/debug/test-logs/*.txt.gz; do
  if [ -f "$f" ]; then
    gzip -d "$f"
  fi
done

exit $exit_code
