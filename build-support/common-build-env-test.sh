#!/usr/bin/env bash

# A "unit test" for Bash libraries used in build/test scripts.

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
set -euo pipefail

# shellcheck source=build-support/common-build-env.sh
. "${0%/*}/common-build-env.sh"

assert_equals() {
  if [[ $# -lt 2 ]]; then
    fatal "assert_equals requires at least 2 arguments"
  fi
  expected_value=$1
  actual_value=$2
  shift 2
  local extra_details="$*"
  if [[ -n $extra_details ]]; then
    extra_details=" $extra_details"
  fi

  if [[ "$expected_value" != "$actual_value" ]]; then
    fatal "Assertion failed." \
          "Expected: '$expected_value', got: '$actual_value'.$extra_details"
  fi
}

# -------------------------------------------------------------------------------------------------
# Testing detecting build type by Jenkins job name.
# -------------------------------------------------------------------------------------------------

test_build_type_detection_by_jenkins_job_name() {
  expect_num_args 2 "$@"
  local expected_build_type=$1
  local jenkins_job_name=$2
  (
    unset YB_COMPILER_TYPE
    unset build_type
    JOB_NAME="$jenkins_job_name"
    set_build_type_based_on_jenkins_job_name
    assert_equals "$expected_build_type" "$build_type" "Jenkins job name: $jenkins_job_name"
  )
}

test_build_type_detection_by_jenkins_job_name asan my-asan-job
test_build_type_detection_by_jenkins_job_name asan Asan-my-job
test_build_type_detection_by_jenkins_job_name asan my-job-ASAN
test_build_type_detection_by_jenkins_job_name debug my-debug-job
test_build_type_detection_by_jenkins_job_name debug deBuG-my-job
test_build_type_detection_by_jenkins_job_name debug my-job-debug
test_build_type_detection_by_jenkins_job_name fastdebug my-fastdebug-job
test_build_type_detection_by_jenkins_job_name fastdebug fasTdeBug-my-job
test_build_type_detection_by_jenkins_job_name fastdebug my-job-fastdebug
test_build_type_detection_by_jenkins_job_name release my-relEase-job
test_build_type_detection_by_jenkins_job_name release releasE-job
test_build_type_detection_by_jenkins_job_name release my-job-RELEASE
test_build_type_detection_by_jenkins_job_name tsan my-tsan-job
test_build_type_detection_by_jenkins_job_name tsan TSAN-my-job
test_build_type_detection_by_jenkins_job_name tsan my-job-tsan

test_build_type_detection_by_jenkins_job_name debug random-job-name
test_build_type_detection_by_jenkins_job_name debug releasewithoutwordseparator-job-name
test_build_type_detection_by_jenkins_job_name debug job-name-noseparatorfastdebug

# -------------------------------------------------------------------------------------------------
# Test determining compiler type based on the Jenkins job name.
# -------------------------------------------------------------------------------------------------

test_compiler_detection_by_jenkins_job_name() {
  expect_num_args 2 "$@"
  local expected_compiler_type=$1
  local jenkins_job_name=$2
  (
    unset YB_COMPILER_TYPE
    JOB_NAME="$jenkins_job_name"
    set_compiler_type_based_on_jenkins_job_name
    if [[ ${YB_COMPILER_TYPE_WAS_ADJUSTED:-false} == "false" ]]; then
      assert_equals "$expected_compiler_type" "$YB_COMPILER_TYPE" "compiler type"
    fi
  )
}

test_compiler_detection_by_jenkins_job_name gcc my-job-gcc
test_compiler_detection_by_jenkins_job_name gcc gcc-my-job
test_compiler_detection_by_jenkins_job_name clang my-job-clang
test_compiler_detection_by_jenkins_job_name clang clang-my-job
test_compiler_detection_by_jenkins_job_name "" random-job-name

# -------------------------------------------------------------------------------------------------
# Test determining build type to pass to our CMakeLists.txt as CMAKE_BUILD_TYPE and compiler type.
# -------------------------------------------------------------------------------------------------

test_set_cmake_build_type_and_compiler_type() {
  expect_num_args 6 "$@"
  local _build_type=$1
  validate_build_type "$_build_type"
  local os_type=$2
  if [[ ! "$os_type" =~ ^(darwin|linux-gnu) ]]; then
    fatal "Unexpected value for the mock OSTYPE: '$os_type'"
  fi
  local compiler_type_preference=$3
  if [[ ! "$compiler_type_preference" =~ ^(gcc[0-9]*|clang[0-9]*|auto|N/A)$ ]]; then
    fatal "Invalid value for compiler_type_preference: '$compiler_type_preference'"
  fi

  local test_case_details="Build type: $_build_type, "
  local test_case_details+="OS type: $os_type, "
  local test_case_details+="compiler type preference: $compiler_type_preference."

  local expected_cmake_build_type=$4
  local expected_compiler_type=$5
  local expected_exit_code=$6

  set +e
  (
    set -e
    if [[ "$compiler_type_preference" == "auto" ]]; then
      unset YB_COMPILER_TYPE
    else
      YB_COMPILER_TYPE=$compiler_type_preference
    fi
    unset cmake_build_type
    build_type=$_build_type
    OSTYPE=$os_type
    set_cmake_build_type_and_compiler_type
    assert_equals "$expected_cmake_build_type" "$cmake_build_type" "$test_case_details" \
                  "Note: comparing CMake build type."
    if [[ ${YB_COMPILER_TYPE_WAS_ADJUSTED:-false} == "false" ]]; then
      assert_equals "$expected_compiler_type" "$YB_COMPILER_TYPE" "$test_case_details" \
                    "Note: comparing compiler type."
    fi
  )
  local exit_code=$?
  set -e
  assert_equals "$expected_exit_code" "$exit_code"
}

# Parameters:                               build_type OSTYPE    Compiler   Expected   Expected
#                                                                type       build_type YB_COMPILER_
#                                                                preference            TYPE

# The last parameter is expected exit code (0 or 1).

test_set_cmake_build_type_and_compiler_type   asan       darwin    auto       fastdebug  clang   0
test_set_cmake_build_type_and_compiler_type   asan       darwin    clang      fastdebug  clang   0
test_set_cmake_build_type_and_compiler_type   asan       darwin    gcc        N/A        N/A     1
test_set_cmake_build_type_and_compiler_type   asan       linux-gnu clang14    fastdebug  clang14 0
test_set_cmake_build_type_and_compiler_type   asan       linux-gnu gcc        N/A        N/A     1
test_set_cmake_build_type_and_compiler_type   asan       linux-gnu gcc11      N/A        gcc11   1
test_set_cmake_build_type_and_compiler_type   tsan       linux-gnu clang14    fastdebug  clang14 0
test_set_cmake_build_type_and_compiler_type   tsan       linux-gnu gcc        N/A        N/A     1
test_set_cmake_build_type_and_compiler_type   tsan       linux-gnu gcc11      N/A        gcc11   1
test_set_cmake_build_type_and_compiler_type   debug      darwin    auto       debug      clang   0
test_set_cmake_build_type_and_compiler_type   debug      darwin    clang      debug      clang   0
test_set_cmake_build_type_and_compiler_type   debug      linux-gnu clang      debug      clang   0
test_set_cmake_build_type_and_compiler_type   debug      linux-gnu gcc        debug      gcc     0
test_set_cmake_build_type_and_compiler_type   debug      linux-gnu gcc11      debug      gcc11   0
test_set_cmake_build_type_and_compiler_type   FaStDeBuG  darwin    auto       fastdebug  clang   0
test_set_cmake_build_type_and_compiler_type   FaStDeBuG  darwin    clang      fastdebug  clang   0
test_set_cmake_build_type_and_compiler_type   FaStDeBuG  linux-gnu clang      fastdebug  clang   0
test_set_cmake_build_type_and_compiler_type   FaStDeBuG  linux-gnu gcc        fastdebug  gcc     0
test_set_cmake_build_type_and_compiler_type   release    darwin    auto       release    clang   0
test_set_cmake_build_type_and_compiler_type   release    darwin    clang      release    clang   0
test_set_cmake_build_type_and_compiler_type   release    linux-gnu clang      release    clang   0
test_set_cmake_build_type_and_compiler_type   release    linux-gnu gcc        release    gcc     0
test_set_cmake_build_type_and_compiler_type   release    linux-gnu gcc11      release    gcc11   0
test_set_cmake_build_type_and_compiler_type   debug      linux-gnu auto       debug      clang17 0
test_set_cmake_build_type_and_compiler_type   FaStDeBuG  linux-gnu auto       fastdebug  clang17 0
test_set_cmake_build_type_and_compiler_type   release    linux-gnu auto       release    clang17 0
test_set_cmake_build_type_and_compiler_type   tsan       linux-gnu auto       fastdebug  clang17 0
test_set_cmake_build_type_and_compiler_type   asan       linux-gnu auto       fastdebug  clang17 0

# -------------------------------------------------------------------------------------------------
# Test existence of scripts pointed to by specical "script path" variables.
# -------------------------------------------------------------------------------------------------

list_yb_script_path_var_names() {
  env | grep -E '^YB_SCRIPT_PATH_' | sed 's/=.*//g'
}

# Unset all script path variables in case some of them are set from outside.
for script_path_var_name in $( list_yb_script_path_var_names ); do
  unset "${script_path_var_name}"
done

# Then set them again from scratch.
yb_script_paths_are_set=false
set_script_paths

# Verify that the script pointed to by each of these variables exists.
for script_path_var_name in $( list_yb_script_path_var_names ); do
  script_path_var_value=${!script_path_var_name}
  if [[ ! -f ${script_path_var_value} ]]; then
    fatal "Script path variable '$script_path_var_name' points to a non-existent file: " \
          "'$script_path_var_value'"
  fi
  if [[ ! -x ${script_path_var_value} ]]; then
    fatal "Script path variable '$script_path_var_name' points to a non-executable file: " \
          "'$script_path_var_value'"
  fi
done

# -------------------------------------------------------------------------------------------------

echo "${0##/*} succeeded"
