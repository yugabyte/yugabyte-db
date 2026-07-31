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

#
# CMake module for Include What You Use (IWYU) integration.
#
# Two modes of operation:
#
# 1. Native CMake mode (YB_ENABLE_IWYU=ON):
#    IWYU runs automatically during compilation. No compile_commands.json needed.
#    Usage: ./yb_build.sh --iwyu
#
# 2. Separate analysis mode (ninja iwyu):
#    IWYU runs as a separate target using compile_commands.json.
#    Requires a prior build to generate the compilation database.
#

# Find the IWYU executable in the toolchain or PATH
# Sets YB_IWYU_PATH in the caller's scope
macro(yb_find_iwyu)
  # First, check if YB_IWYU_PATH environment variable is set
  if(NOT "$ENV{YB_IWYU_PATH}" STREQUAL "")
    if(EXISTS "$ENV{YB_IWYU_PATH}")
      set(YB_IWYU_PATH "$ENV{YB_IWYU_PATH}")
    else()
      message(WARNING "IWYU: YB_IWYU_PATH is set to '$ENV{YB_IWYU_PATH}' but file does not exist")
      unset(YB_IWYU_PATH)
    endif()
  else()
    # Try to find in the LLVM toolchain
    if(EXISTS "${YB_BUILD_ROOT}/toolchain/bin/include-what-you-use")
      set(YB_IWYU_PATH "${YB_BUILD_ROOT}/toolchain/bin/include-what-you-use")
    else()
      # Try common installation paths
      find_program(YB_IWYU_PATH
        NAMES include-what-you-use iwyu
        HINTS
          "${YB_THIRDPARTY_DIR}/clang-toolchain/bin"
          "/usr/bin"
          "/usr/local/bin"
      )
    endif()
  endif()
endmacro()

# Enable native CMake IWYU integration (runs during compilation)
# This is a macro so that CMAKE_CXX_INCLUDE_WHAT_YOU_USE is set in the caller's scope
macro(yb_enable_iwyu_for_compilation)
  yb_find_iwyu()

  if(NOT YB_IWYU_PATH)
    message(FATAL_ERROR "IWYU: include-what-you-use not found but YB_ENABLE_IWYU is ON")
  endif()

  message(STATUS "IWYU: Enabling native CMake IWYU integration (runs during compilation)")
  message(STATUS "IWYU: Using ${YB_IWYU_PATH}")

  # Set up output directory for IWYU results
  set(IWYU_OUTPUT_DIR "${YB_BUILD_ROOT}/iwyu_output")
  # Preserve results across CMake reconfiguration. The wrapper removes stale output for each
  # translation unit immediately before reanalyzing it, while untouched completion markers are
  # needed to track full-tree coverage across incremental builds.
  file(MAKE_DIRECTORY "${IWYU_OUTPUT_DIR}")

  # Use our wrapper script that captures output
  set(IWYU_WRAPPER "${YB_SRC_ROOT}/build-support/iwyu-wrapper")

  # Build the IWYU wrapper command with paths as arguments
  set(IWYU_COMMAND
    "${IWYU_WRAPPER};--iwyu-path;${YB_IWYU_PATH};--output-dir;${IWYU_OUTPUT_DIR};--source-root;${YB_SRC_ROOT};--build-root;${YB_BUILD_ROOT}")

  # Pass the LLVM library directory so the wrapper can set LD_LIBRARY_PATH for libc++ etc.
  if(DEFINED ENV{YB_LLVM_TOOLCHAIN_DIR})
    set(LLVM_LIB_DIR "$ENV{YB_LLVM_TOOLCHAIN_DIR}/lib/x86_64-unknown-linux-gnu")
    if(EXISTS "${LLVM_LIB_DIR}")
      set(IWYU_COMMAND "${IWYU_COMMAND};--llvm-lib-dir;${LLVM_LIB_DIR}")
    endif()
  endif()

  # Pass the filter path if specified (only run IWYU on files matching this path)
  if(NOT "$ENV{YB_IWYU_FILTER_PATH}" STREQUAL "")
    set(IWYU_FILTER_PATH "$ENV{YB_IWYU_FILTER_PATH}")
    if(NOT IS_ABSOLUTE "${IWYU_FILTER_PATH}")
      set(IWYU_FILTER_PATH "${YB_SRC_ROOT}/${IWYU_FILTER_PATH}")
    endif()
    set(IWYU_COMMAND "${IWYU_COMMAND};--filter-path;${IWYU_FILTER_PATH}")
    message(STATUS "IWYU: Filtering to files matching: ${IWYU_FILTER_PATH}")
  endif()

  # Add mapping file if it exists (passed to the actual IWYU by the wrapper)
  set(IWYU_MAPPING_FILE "${YB_SRC_ROOT}/yugabyte.imp")
  if(EXISTS "${IWYU_MAPPING_FILE}")
    set(IWYU_COMMAND "${IWYU_COMMAND};-Xiwyu;--mapping_file=${IWYU_MAPPING_FILE}")
  endif()

  # Add common IWYU options
  set(IWYU_COMMAND "${IWYU_COMMAND};-Xiwyu;--max_line_length=100")

  # Set for all C++ targets - this affects all subsequent add_library/add_executable calls
  set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE "${IWYU_COMMAND}")

  message(STATUS "IWYU: Output will be saved to ${IWYU_OUTPUT_DIR}")
  message(STATUS "IWYU: Combined output file: ${IWYU_OUTPUT_DIR}/iwyu_all.txt")
  message(STATUS "IWYU: Command: ${IWYU_COMMAND}")
endmacro()

# Create the IWYU target (for separate analysis mode using compile_commands.json)
function(yb_create_iwyu_target)
  yb_find_iwyu()

  if(NOT YB_IWYU_PATH)
    message(STATUS "IWYU: include-what-you-use not found, 'iwyu' target will not be available")
    return()
  endif()

  message(STATUS "IWYU: Found include-what-you-use at ${YB_IWYU_PATH}")

  # Path to the IWYU runner script
  set(IWYU_RUNNER_SCRIPT "${YB_SRC_ROOT}/build-support/run_iwyu")

  # Create the iwyu target (uses compile_commands.json)
  add_custom_target(iwyu
    COMMAND "${IWYU_RUNNER_SCRIPT}"
      --build_root "${YB_BUILD_ROOT}"
      --iwyu-path "${YB_IWYU_PATH}"
    WORKING_DIRECTORY "${YB_SRC_ROOT}"
    COMMENT "Running Include What You Use (IWYU) analysis on src/yb (requires compile_commands.json)..."
    VERBATIM
  )

  # Add a target for IWYU with fix application
  add_custom_target(iwyu-fix
    COMMAND "${IWYU_RUNNER_SCRIPT}"
      --build_root "${YB_BUILD_ROOT}"
      --iwyu-path "${YB_IWYU_PATH}"
      --apply-fixes
    WORKING_DIRECTORY "${YB_SRC_ROOT}"
    COMMENT "Running IWYU and applying fixes to src/yb..."
    VERBATIM
  )

  message(STATUS "IWYU: Created 'iwyu' and 'iwyu-fix' targets (require compile_commands.json)")
endfunction()
