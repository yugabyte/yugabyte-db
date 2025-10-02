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

# Test-related functions.

# Compose ctest prefix and test binary name based on test filename and directory
# REL_TEST_NAME is the name of the test (see ADD_YB_TEST function).
# CTEST_PREFIX_RV - ctest prefix will be stored in a variable with name ${CTEST_PREFIX_RV}.
# TEST_BINARY_NAME_RV - test binary name will be stored in a variable with name
#                       ${TEST_BINARY_NAME_RV}.
# (Note: "RV" means "return value" here).
function(GET_TEST_PREFIX_AND_BINARY_NAME CTEST_PREFIX_RV TEST_BINARY_NAME_RV REL_TEST_NAME)
  set(DIR ${CMAKE_CURRENT_LIST_DIR})
  while (true)
    get_filename_component(DIR_NAME ${DIR} NAME_WE)
    # We want to use parent directory name as a prefix if test is located in "test" or "tests"
    # directory, so tests from 'src/ql/test' will be compiled into 'tests-ql' rather than
    # 'tests-test'.
    if (NOT "${DIR_NAME}" MATCHES "^(test|tests)$")
      set(${CTEST_PREFIX_RV} "${DIR_NAME}" PARENT_SCOPE)
      break()
    endif()
    get_filename_component(DIR ${DIR} DIRECTORY)
    if (${DIR} STREQUAL ${YB_SRC_ROOT})
      # If we have tests in 'src/test' or 'src/tests' - use following prefix:
      set(${CTEST_PREFIX_RV} "other" PARENT_SCOPE)
      break()
    endif()
  endwhile()
  get_filename_component(FILE_NAME ${REL_TEST_NAME} NAME_WE)
  set(${TEST_BINARY_NAME_RV} "${FILE_NAME}" PARENT_SCOPE)
endfunction()

# This macro determines if the given test should be enabled, based on the following:
# - create_initial_sys_catalog_snapshot is always enabled
# - YB_BUILD_TESTS=OFF disables all tests
# - If YB_BUILD_TESTS is ON, YB_TEST_FILTER_RE specifiees the regular expression of tests to build.

macro(yb_check_if_test_is_enabled REL_TEST_NAME)
  if("${CMAKE_CURRENT_FUNCTION}" STREQUAL "ADD_YB_TEST")
    set(_yb_verbose_test_filtering ON)
  else()
    set(_yb_verbose_test_filtering OFF)
  endif()
  if("${REL_TEST_NAME}" STREQUAL "create_initial_sys_catalog_snapshot")
    # We always build create_initial_sys_catalog_snapshot because we need it to run the database.
    if(_yb_verbose_test_filtering AND
       (NOT YB_BUILD_TESTS OR NOT "${YB_TEST_FILTER_RE}" STREQUAL ""))
      message("C++ test filtering: the test ${REL_TEST_NAME} is always enabled")
    endif()
    set(yb_test_enabled ON)
  elseif(NOT YB_BUILD_TESTS)
    set(yb_test_enabled OFF)
  elseif("${YB_TEST_FILTER_RE}" STREQUAL "")
    set(yb_test_enabled ON)
  else()
    string(REGEX MATCH "${YB_TEST_FILTER_RE}" _yb_test_filter_match_result "${REL_TEST_NAME}")
    if ("${_yb_test_filter_match_result}" STREQUAL "")
      set(yb_test_enabled OFF)
    else()
      set(yb_test_enabled ON)
      if(_yb_verbose_test_filtering)
        message("C++ test filtering: enabled ${REL_TEST_NAME} (matched ${YB_TEST_FILTER_RE})")
      endif()
    endif()
  endif()
endmacro()

# Add a new test case, with or without an executable that should be built.
#
# REL_TEST_NAME is the name of the test. It may be a single component
# (e.g. monotime-test) or contain additional components (e.g.
# net/net_util-test). Either way, the last component must be a globally
# unique name.
#
# Arguments after the test name will be passed to set_tests_properties().
function(ADD_YB_TEST REL_TEST_NAME)
  math(EXPR NEW_YB_NUM_TESTS "${YB_NUM_TESTS} + 1")
  set(YB_NUM_TESTS "${NEW_YB_NUM_TESTS}" CACHE INTERNAL "Number of tests" FORCE)
  yb_check_if_test_is_enabled(${REL_TEST_NAME})
  if(NOT yb_test_enabled)
    return()
  endif()

  math(EXPR NEW_YB_NUM_INCLUDED_TESTS "${YB_NUM_INCLUDED_TESTS} + 1")
  set(YB_NUM_INCLUDED_TESTS "${NEW_YB_NUM_INCLUDED_TESTS}" CACHE INTERNAL
      "Number of included tests" FORCE)

  GET_TEST_PREFIX_AND_BINARY_NAME(CTEST_PREFIX TEST_BINARY_NAME ${REL_TEST_NAME})
  SET(TEST_BINARY_DIR "${YB_BUILD_ROOT}/tests-${CTEST_PREFIX}")
  SET(CTEST_TEST_NAME "${CTEST_PREFIX}_${TEST_BINARY_NAME}")

  # We're using CMAKE_CURRENT_LIST_DIR instead of CMAKE_CURRENT_SOURCE_DIR here so that we can
  # include a CMakeLists.txt from a different directory and add tests whose sources reside in the
  # same directory as the included CMakeLists.txt.
  SET(TEST_SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}/${REL_TEST_NAME}.cc")

  if(YB_MATCHING_ENTERPRISE_DIR)
    SET(TEST_SOURCE_PATH_ENTERPRISE "${YB_MATCHING_ENTERPRISE_DIR}/${REL_TEST_NAME}.cc")
    if(EXISTS "${TEST_SOURCE_PATH}" AND
       EXISTS "${TEST_SOURCE_PATH_ENTERPRISE}" AND
       NOT "${TEST_SOURCE_PATH}" STREQUAL "${TEST_SOURCE_PATH_ENTERPRISE}")
      message(FATAL_ERROR "Both ${TEST_SOURCE_PATH} and ${TEST_SOURCE_PATH_ENTERPRISE} exist!")
    endif()
    if(NOT EXISTS "${TEST_SOURCE_PATH}")
      SET(TEST_SOURCE_PATH "${TEST_SOURCE_PATH_ENTERPRISE}")
    endif()
  endif()

  if(NOT EXISTS "${TEST_SOURCE_PATH}")
    message(FATAL_ERROR "Test source '${TEST_SOURCE_PATH}' does not exist.")
  endif()

  set(TEST_PATH "${TEST_BINARY_DIR}/${TEST_BINARY_NAME}")
  set(LAST_ADDED_TEST_BINARY_PATH "${TEST_PATH}" PARENT_SCOPE)
  set(LAST_ADDED_TEST_BINARY_NAME "${TEST_BINARY_NAME}" PARENT_SCOPE)

  # We need the ${CMAKE_CURRENT_LIST_DIR} prefix, because in case of including a CMakeLists.txt
  # file from a different directory, the test's source might not be in
  # ${CMAKE_CURRENT_SOURCE_DIR}.

  # This is used to let add_executable know whether we're adding a test.
  set(YB_ADDING_TEST_EXECUTABLE "TRUE" CACHE INTERNAL "" FORCE)

  add_executable("${TEST_BINARY_NAME}" "${TEST_SOURCE_PATH}")

  set(YB_ADDING_TEST_EXECUTABLE "FALSE" CACHE INTERNAL "" FORCE)

  set_target_properties(${TEST_BINARY_NAME}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${TEST_BINARY_DIR}")
  target_link_libraries(${TEST_BINARY_NAME} ${YB_TEST_LINK_LIBS})
  ADD_COMMON_YB_TEST_DEPENDENCIES(${REL_TEST_NAME})
  add_custom_target("${CTEST_TEST_NAME}" DEPENDS "${TEST_BINARY_NAME}")
  add_test("${CTEST_TEST_NAME}" "${BUILD_SUPPORT_DIR}/run-test.sh" "${TEST_PATH}")
  set_target_properties(
    ${CTEST_TEST_NAME}
    PROPERTIES
    TEST_BINARY_PATH "${TEST_PATH}"
  )

  if(ARGN)
    set_tests_properties(${TEST_NAME} PROPERTIES ${ARGN})
  endif()
endfunction()

function(ADD_YB_TESTS TESTS)
  foreach(TEST ${TESTS})
    ADD_YB_TEST(${TEST})
  endforeach(TEST)
endfunction()

# -------------------------------------------------------------------------------------------------
# Functions that configure tests while taking various test filtering options into account.
# -------------------------------------------------------------------------------------------------

# A wrapper for add_dependencies() that takes BUILD_TESTS into account.
function(ADD_YB_TEST_DEPENDENCIES REL_TEST_NAME)
  yb_check_if_test_is_enabled(${REL_TEST_NAME})
  if(NOT yb_test_enabled)
    return()
  endif()
  GET_TEST_PREFIX_AND_BINARY_NAME(CTEST_PREFIX TEST_BINARY_NAME ${REL_TEST_NAME})

  add_dependencies(${TEST_BINARY_NAME} ${ARGN})
endfunction()

function(YB_TEST_TARGET_LINK_LIBRARIES REL_TEST_NAME)
  yb_check_if_test_is_enabled(${REL_TEST_NAME})
  if(NOT yb_test_enabled)
    return()
  endif()
  GET_TEST_PREFIX_AND_BINARY_NAME(CTEST_PREFIX TEST_BINARY_NAME ${REL_TEST_NAME})

  target_link_libraries(${TEST_BINARY_NAME} ${ARGN})
endfunction()

function(YB_TEST_TARGET_INCLUDE_DIRECTORIES REL_TEST_NAME)
  yb_check_if_test_is_enabled(${REL_TEST_NAME})
  if(NOT yb_test_enabled)
    return()
  endif()
  GET_TEST_PREFIX_AND_BINARY_NAME(CTEST_PREFIX TEST_BINARY_NAME ${REL_TEST_NAME})

  target_include_directories(${TEST_BINARY_NAME} ${ARGN})
endfunction()

function(YB_TEST_TARGET_COMPILE_OPTIONS REL_TEST_NAME)
  yb_check_if_test_is_enabled(${REL_TEST_NAME})
  if(NOT yb_test_enabled)
    return()
  endif()
  GET_TEST_PREFIX_AND_BINARY_NAME(CTEST_PREFIX TEST_BINARY_NAME ${REL_TEST_NAME})

  target_compile_options(${TEST_BINARY_NAME} ${ARGN})
endfunction()

# -------------------------------------------------------------------------------------------------

# Adds dependencies common to all tests, both main YB tests added using ADD_YB_TESTS,
# and RocksDB tests, which are added in rocksdb/CMakeLists.txt using add_test directly.
function(ADD_COMMON_YB_TEST_DEPENDENCIES REL_TEST_NAME)
  # Our ctest-based wrapper, run-test.sh, uses this tool to put a time limit on tests.
  ADD_YB_TEST_DEPENDENCIES(${REL_TEST_NAME} run-with-timeout)
endfunction()

function(ADD_YB_TEST_LIBRARY LIB_NAME)
  # Parse the arguments.
  set(options "")
  set(one_value_args COMPILE_FLAGS)
  set(multi_value_args SRCS DEPS NONLINK_DEPS)
  cmake_parse_arguments(ARG "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})
  if(ARG_UNPARSED_ARGUMENTS)
    message(SEND_ERROR "Error: unrecognized arguments: ${ARG_UNPARSED_ARGUMENTS}")
  endif()

  # This is used to let add_library know whether we're adding a test.
  set(YB_ADDING_TEST_EXECUTABLE "TRUE" CACHE INTERNAL "" FORCE)
  add_library(${LIB_NAME} ${ARG_SRCS})
  set(YB_ADDING_TEST_EXECUTABLE "FALSE" CACHE INTERNAL "" FORCE)

  if(ARG_COMPILE_FLAGS)
    set_target_properties(${LIB_NAME}
      PROPERTIES COMPILE_FLAGS ${ARG_COMPILE_FLAGS})
  endif()
  if(ARG_DEPS)
    target_link_libraries(${LIB_NAME} ${ARG_DEPS})
  endif()
  if(ARG_NONLINK_DEPS)
    add_dependencies(${LIB_NAME} ${ARG_NONLINK_DEPS})
  endif()
endfunction()

function(ADD_YB_FUZZ_TARGET REL_TEST_NAME)
  if(NOT YB_BUILD_FUZZ_TARGETS)
    return()
  endif()
  set(TEST_SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}/${REL_TEST_NAME}.cc")
  if(NOT EXISTS "${TEST_SOURCE_PATH}")
    message(FATAL_ERROR "Test source '${TEST_SOURCE_PATH}' does not exist.")
  endif()
  GET_TEST_PREFIX_AND_BINARY_NAME(CTEST_PREFIX TEST_BINARY_NAME ${REL_TEST_NAME})
  set(TEST_BINARY_DIR "${YB_BUILD_ROOT}/fuzz-targets-${CTEST_PREFIX}")
  set(TEST_PATH "${TEST_BINARY_DIR}/${TEST_BINARY_NAME}")
  add_executable("${TEST_BINARY_NAME}" "${TEST_SOURCE_PATH}")
  set_target_properties(${TEST_BINARY_NAME}
    PROPERTIES
    LINK_FLAGS "-fsanitize=fuzzer"
    COMPILE_FLAGS "-fsanitize=fuzzer"
    RUNTIME_OUTPUT_DIRECTORY "${TEST_BINARY_DIR}")
  target_link_libraries(${TEST_BINARY_NAME} ${YB_FUZZ_TARGET_LINK_LIBS})
endfunction()
