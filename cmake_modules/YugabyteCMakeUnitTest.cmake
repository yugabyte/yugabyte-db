# Copyright (c) YugabyteDB, Inc.
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

# A simple unit test framework for CMake code.

# -------------------------------------------------------------------------------------------------
# Test framework
# -------------------------------------------------------------------------------------------------

function(assert_equals EXPECTED_VALUE ACTUAL_VALUE)
  if(("${ARGC}" GREATER 2) AND (NOT "${ARGV2}" STREQUAL ""))
    set(EXTRA_MESSAGE ". ${ARGV2}")
  else()
    set(EXTRA_MESSAGE "")
  endif()
  if(NOT "${EXPECTED_VALUE}" STREQUAL "${ACTUAL_VALUE}")
    message(FATAL_ERROR
            "Assertion failed: expected value '${EXPECTED_VALUE}', actual value '${ACTUAL_VALUE}'"
            "${EXTRA_MESSAGE}")
  endif()
endfunction()

# -------------------------------------------------------------------------------------------------
# Tests for specific functionality
# -------------------------------------------------------------------------------------------------

function(check_parse_build_root_basename
         BUILD_ROOT_BASENAME
         EXPECTED_BUILD_TYPE
         EXPECTED_COMPILER_TYPE
         EXPECTED_LINKING_TYPE
         EXPECTED_USING_LINUXBREW)
  set(CMAKE_CURRENT_BINARY_DIR "/somedir/${BUILD_ROOT_BASENAME}")
  unset(ENV{YB_COMPILER_TYPE})
  parse_build_root_basename()
  assert_equals("${EXPECTED_BUILD_TYPE}" "${YB_BUILD_TYPE}")
  assert_equals("${EXPECTED_COMPILER_TYPE}" "${YB_COMPILER_TYPE}" "(CMake var)")
  assert_equals("${EXPECTED_COMPILER_TYPE}" "$ENV{YB_COMPILER_TYPE}" "(env var)")
  assert_equals("${EXPECTED_LINKING_TYPE}" "${YB_LINKING_TYPE}")
  assert_equals("${EXPECTED_USING_LINUXBREW}" "${YB_USING_LINUXBREW_FROM_BUILD_ROOT}")
endfunction()

function(test_parse_build_root_basename)
  # BUILD_ROOT_BASENAME                  BUILD_TYPE      COMPILER_TYPE LINK_TYPE USING_LINUXBREW
  check_parse_build_root_basename(
    "debug-clang-dynamic-ninja"          "debug"         "clang"       "dynamic"  OFF)
  check_parse_build_root_basename(
    "debug-clang-dynamic"                "debug"         "clang"       "dynamic"  OFF)
  check_parse_build_root_basename(
    "asan-gcc12-dynamic-ninja"           "asan"          "gcc12"       "dynamic"  OFF)
  check_parse_build_root_basename(
    "asan_release-clang19-dynamic-ninja" "asan_release"  "clang19"     "dynamic"  OFF)
  check_parse_build_root_basename(
    "debug-clang-dynamic"                "debug"         "clang"       "dynamic"  OFF)
  check_parse_build_root_basename(
    "tsan-clang14-dynamic"               "tsan"          "clang14"     "dynamic"  OFF)
  check_parse_build_root_basename(
    "tsan_release-clang19-dynamic"       "tsan_release"  "clang19"     "dynamic"  OFF)
  check_parse_build_root_basename(
    "release-clang15-linuxbrew-dynamic"  "release"       "clang15"     "dynamic"  ON)
  check_parse_build_root_basename(
    "release-clang15-linuxbrew-thin-lto" "release"       "clang15"     "thin-lto" ON)
  check_parse_build_root_basename(
    "release-clang15-linuxbrew-full-lto" "release"       "clang15"     "full-lto" ON)
endfunction()

# -------------------------------------------------------------------------------------------------
# Main script
# -------------------------------------------------------------------------------------------------

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake_modules")

include(YugabyteFunctions)

yb_initialize_constants()

test_parse_build_root_basename()

message("All Yugabyte CMake tests passed successfully")
