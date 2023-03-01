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
  --delete-arc-patch-branches
    Delete branches starting with "arcpatch-D..." (except the current branch) so that the Jenkins
    Phabricator plugin does not give up after three attempts.

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

echo "Build script ${BASH_SOURCE[0]} is running"

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

JOB_NAME=${JOB_NAME:-}
build_type=${BUILD_TYPE:-}
set_build_type_based_on_jenkins_job_name
readonly BUILD_TYPE=$build_type
export BUILD_TYPE

echo "Build type: ${BUILD_TYPE}";

set_compiler_type_based_on_jenkins_job_name

if [ -n "${YB_NUM_TESTS_TO_RUN:-}" ]; then

  if [[ ! "$YB_NUM_TESTS_TO_RUN" =~ ^[0-9]+$ ]]; then
    echo "Invalid number of tests to run: $YB_NUM_TESTS_TO_RUN" >&2
    exit 1
  fi
  export EXTRA_TEST_FLAGS="-I1,$YB_NUM_TESTS_TO_RUN"
fi

export YB_MINIMIZE_VERSION_DEFINES_CHANGES=1
export YB_MINIMIZE_RECOMPILATION=1

export YB_BUILD_JAVA=${YB_BUILD_JAVA:-1}
export YB_BUILD_PYTHON=${YB_BUILD_PYTHON:-0}
export YB_BUILD_CPP=${YB_BUILD_CPP:-1}

set +e
"$YB_BUILD_SUPPORT_DIR"/jenkins/test.sh
exit_code=$?

set -e

exit $exit_code
