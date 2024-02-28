# Copyright (c) Yugabyte, Inc.
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

# Various CMake macros and functions. This should eventually be organized and refactored.

# -------------------------------------------------------------------------------------------------
# Auxiliary macros for implementing other macros and functions.
# -------------------------------------------------------------------------------------------------

macro(set_in_current_and_parent_scope var_name var_value)
  set(${var_name} "${var_value}")
  if (NOT "${CMAKE_CURRENT_FUNCTION}" STREQUAL "")
    set(${var_name} "${var_value}" PARENT_SCOPE)
  endif()
endmacro()

# Attempts to set a variable to a value at global scope. However, this will only work if the macro
# is executed either from the global scope or a function called from the global scope.
macro(set_in_global_scope var_name var_value)
  if("${CMAKE_CURRENT_FUNCTION}" STREQUAL "")
    set(${var_name} "${var_value}")
  else()
    set(${var_name} "${var_value}" PARENT_SCOPE)
  endif()
endmacro()

# See this on how to iterate macro arguments:
# https://stackoverflow.com/questions/42682912/cmake-macro-how-to-iterate-over-arguments
macro(assert_vars_defined)
  foreach(_var_name_tmp IN ITEMS ${ARGN})
    if(NOT DEFINED ${_var_name_tmp})
      message(FATAL_ERROR "Variable ${_var_name_tmp} is not defined")
    endif()
  endforeach()
  unset(_var_name_tmp)
endmacro()

macro(yb_put_var_into_cache var_name data_type)
  if(NOT DEFINED ${var_name})
    message(FATAL_ERROR
            "Variable ${var_name} is not defined, cannot put it into CMake cache.")
  endif()
  if(NOT "${data_type}" MATCHES "^(STRING|BOOL)$")
    message(FATAL_ERROR
            "Data type ${data_type} not allowed when putting variable ${var_name} into cache.")
  endif()
  set("${var_name}" "${${var_name}}" CACHE ${data_type}
      "YugabyteDB configuration variable ${var_name} set using yb_put_var_into_cache" FORCE)
endmacro()

# Puts the given list of variables as INTERNAL CACHE variables.
# See for more details:
# - https://cmake.org/cmake/help/book/mastering-cmake/chapter/CMake%20Cache.html
# - https://cmake.org/cmake/help/latest/command/set.html
macro(yb_put_string_vars_into_cache)
  foreach(_var_name_tmp IN ITEMS ${ARGN})
    if(NOT DEFINED ${_var_name_tmp})
      message(FATAL_ERROR
              "Variable ${_var_name_tmp} is not defined, cannot put it into CMake cache.")
    endif()
    set("${_var_name_tmp}" "${${_var_name_tmp}}" CACHE INTERNAL
        "Internal variable ${_var_name_tmp} (from yb_put_string_vars_into_cache)")
  endforeach()
  unset(_var_name_tmp)
endmacro()

# Concatenate third-party prefix directories (YB_THIRDPARTY_COMMON_DIR and
# YB_THIRDPARTY_MAYBE_INSTRUMENTED_DIR) into the given target variable with the given separator
# and suffix.
function(concat_thirdparty_prefix_dirs_with_suffix target_var separator suffix)
  set(result "")
  foreach(thirdparty_prefix_var_name YB_THIRDPARTY_COMMON_DIR YB_THIRDPARTY_MAYBE_INSTRUMENTED_DIR)
    if(NOT "${result}" STREQUAL "")
      string(APPEND result "${separator}")
    endif()
    string(APPEND result "${${thirdparty_prefix_var_name}}${suffix}")
  endforeach()
  set("${target_var}" "${result}" PARENT_SCOPE)
endfunction()

# -------------------------------------------------------------------------------------------------
# Macros and functions for adding and manipulating compiler and linker flags.
# -------------------------------------------------------------------------------------------------

# This macro can be executed either from the top-level scope or from a function one level deep from
# the top level scope.
macro(ADD_CXX_FLAGS FLAGS)
  if ($ENV{YB_VERBOSE})
    message("Adding C++ flags: ${FLAGS}")
  endif()
  set_in_global_scope(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FLAGS}")
endmacro()

# Linker flags applied to both executables and shared libraries. We append this both to
# CMAKE_EXE_LINKER_FLAGS and CMAKE_SHARED_LINKER_FLAGS after we finish making changes to this.
# These flags apply to both YB and RocksDB parts of the codebase.
#
# This is an internal macro that modifies variables at the parent scope, which is really the parent
# scope of the functions calling it, i.e. function caller's scope.
macro(_ADD_LINKER_FLAGS_MACRO FLAGS)
  if ($ENV{YB_VERBOSE})
    message("Adding to linker flags: ${FLAGS}")
  endif()

  # We must set these variables in both current and parent scope, because this macro can be called
  # multiple times from the same function.
  set_in_current_and_parent_scope(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${FLAGS}")
  set_in_current_and_parent_scope(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${FLAGS}")
endmacro()

macro(ADD_EXE_LINKER_FLAGS FLAGS)
  if ($ENV{YB_VERBOSE})
    message("Adding executable linking flags: ${FLAGS}")
  endif()
  set_in_current_and_parent_scope(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${FLAGS}")
endmacro()

function(ADD_LINKER_FLAGS FLAGS)
  _ADD_LINKER_FLAGS_MACRO("${FLAGS}")
endfunction()

macro(_ADD_RPATH_ENTRY_MACRO RPATH_ENTRY)
  _ADD_LINKER_FLAGS_MACRO("-Wl,-rpath,${RPATH_ENTRY}")
endmacro()

function(ADD_GLOBAL_RPATH_ENTRY RPATH_ENTRY)
  _CHECK_LIB_DIR("${RPATH_ENTRY}" "rpath entry")
  message("Adding a global rpath entry: ${RPATH_ENTRY}")
  _ADD_RPATH_ENTRY_MACRO("${RPATH_ENTRY}")
endfunction()

function(YB_ADD_LIB_DIR DIR_PATH)
  _CHECK_LIB_DIR("${DIR_PATH}" "library directory")
  _ADD_LINKER_FLAGS_MACRO("-L${DIR_PATH}")
endfunction()

# This is similar to ADD_GLOBAL_RPATH_ENTRY but also adds an -L<dir> linker flag.
function(ADD_GLOBAL_RPATH_ENTRY_AND_LIB_DIR DIR_PATH)
  _CHECK_LIB_DIR("${DIR_PATH}" "library directory and rpath entry")
  message("Adding a library directory and global rpath entry: ${DIR_PATH}")
  _ADD_LINKER_FLAGS_MACRO("-L${DIR_PATH}")
  _ADD_RPATH_ENTRY_MACRO("${DIR_PATH}")
endfunction()

# Checks for redundant compiler or linker arguments in the given variable. Removes duplicate
# arguments and stores the result back in the same variable. If the
# YB_DEBUG_DUPLICATE_COMPILER_ARGS environment variable is set to 1, prints detailed debug output.
function(yb_deduplicate_arguments args_var_name)
  set(debug OFF)
  if("$ENV{YB_DEBUG_DUPLICATE_COMPILER_ARGS}" STREQUAL "1")
    set(debug ON)
  endif()
  separate_arguments(args_list UNIX_COMMAND "${${args_var_name}}")
  if(debug)
    message("Deduplicating ${args_var_name}:")
  endif()
  set(deduplicated_args "")
  foreach(arg IN LISTS args_list)
    if(arg IN_LIST deduplicated_args)
      if(debug)
        message("    DUPLICATE argument     : ${arg}")
      endif()
    else()
      if(debug)
        message("    Non-duplicate argument : ${arg}")
      endif()
      list(APPEND deduplicated_args "${arg}")
    endif()
  endforeach()
  list(JOIN "${deduplicated_args}" " " joined_deduplicated_args)
  set("${args_var_name}" PARENT_SCOPE "${joined_deduplicated_args}")
endfunction()

# -------------------------------------------------------------------------------------------------

macro(yb_initialize_constants)
  set(BUILD_SUPPORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build-support")
endmacro()

function(CHECK_YB_COMPILER_PATH COMPILER_PATH)
  if(NOT "${COMPILER_PATH}" MATCHES "/compiler-wrappers/(cc|c[+][+])$" AND
     NOT "${CMAKE_COMMAND}" MATCHES "/[.]?(CLion|clion)")
    message(
      SEND_ERROR
      "Invalid compiler path: '${COMPILER_PATH}'. Expected to end with one of: "
      "/compiler-wrappers/{cc,c++}. The only exception is for builds invoked from CLion, but "
      "CMAKE_COMMAND ('${CMAKE_COMMAND}') does not contain a substring '/[.]CLion' or '/[.]clion' "
      "(feel free to tweak the pattern in the top-level CMakeLists.txt if it has to be updated "
      "for the most recent version of CLion).")
  endif()
endfunction()

# Determine the number of CPUs to be used so we can call make on existing Makefiles (e.g. RocksDB)
# with the right level of parallelism.  Snippet taken from https://blog.kitware.com/how-many-ya-got/
function(DETECT_NUMBER_OF_PROCESSORS)
  if(NOT DEFINED PROCESSOR_COUNT)
    # Unknown:
    set(PROCESSOR_COUNT 0)

    # Linux:
    set(cpuinfo_file "/proc/cpuinfo")
    if(EXISTS "${cpuinfo_file}")
      file(STRINGS "${cpuinfo_file}" procs REGEX "^processor.: [0-9]+$")
      list(LENGTH procs PROCESSOR_COUNT)
    endif()

    # Mac:
    if(APPLE)
      execute_process(COMMAND /usr/sbin/sysctl -n hw.ncpu OUTPUT_VARIABLE PROCESSOR_COUNT)
      # Strip trailing newline (otherwise it may get into the generated Makefile).
      string(STRIP "${PROCESSOR_COUNT}" PROCESSOR_COUNT)
    endif()

    # Windows:
    if(WIN32)
      set(PROCESSOR_COUNT "$ENV{NUMBER_OF_PROCESSORS}")
    endif()
  endif()

  if (NOT DEFINED PROCESSOR_COUNT OR "${PROCESSOR_COUNT}" STREQUAL "")
    message(FATAL_ERROR "Could not determine the number of logical CPUs")
  endif()
  message("Detected the number of logical CPUs: ${PROCESSOR_COUNT}")
  set(PROCESSOR_COUNT "${PROCESSOR_COUNT}" PARENT_SCOPE)
endfunction()

# Prevent builds from the top-level source directory. This ensures that build output is well
# isolated from the source tree.
function(ENFORCE_OUT_OF_SOURCE_BUILD)
  if("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_BINARY_DIR}")
    message(FATAL_ERROR
      "YugaByte may not be built from the top-level source directory. Create a new "
      "directory and run cmake from there, passing the path to the top-level "
      "source directory as the last argument. "
      "To override this, rerun CMake with -DYB_ALLOW_IN_SOURCE_BUILD=0. "
      "Also, delete 'CMakeCache.txt' and 'CMakeFiles' from the top-level source "
      "directory, otherwise future builds will not work.")
  endif()
endfunction()

function(DETECT_BREW)
  EXPECT_COMPILER_TYPE_TO_BE_SET()
  if(NOT DEFINED IS_CLANG)
    message(FATAL_ERROR "IS_CLANG undefined")
  endif()
  if(NOT DEFINED IS_GCC)
    message(FATAL_ERROR "IS_GCC undefined")
  endif()
  if(NOT DEFINED COMPILER_VERSION)
    message(FATAL_ERROR "COMPILER_VERSION undefined")
  endif()
  if(NOT DEFINED COMPILER_FAMILY)
    message(FATAL_ERROR "COMPILER_FAMILY undefined")
  endif()

  # Detect Linuxbrew.
  #
  # TODO: consolidate Linuxbrew detection logic between here and detect_brew in common-build-env.sh.
  # As of 10/2020 we only check the compiler version here but not in detect_brew.
  set(USING_LINUXBREW FALSE)
  if(NOT APPLE)
    set(LINUXBREW_DIR "$ENV{YB_LINUXBREW_DIR}")
    message("Trying to detect whether we should use Linuxbrew. "
            "IS_CLANG=${IS_CLANG}, "
            "IS_GCC=${IS_GCC}, "
            "COMPILER_VERSION=${COMPILER_VERSION}, "
            "LINUXBREW_DIR=${LINUXBREW_DIR}")

    if("${LINUXBREW_DIR}" STREQUAL "" AND
       EXISTS "${CMAKE_CURRENT_BINARY_DIR}/linuxbrew_path.txt")
      file(STRINGS "${CMAKE_CURRENT_BINARY_DIR}/linuxbrew_path.txt" LINUXBREW_DIR)
    endif()
    if("${LINUXBREW_DIR}" STREQUAL "")
      set(USING_LINUXBREW FALSE)
      message("Not using Linuxbrew")
    else()
      if(EXISTS "${LINUXBREW_DIR}/bin" AND EXISTS "${LINUXBREW_DIR}/lib")
        message("Linuxbrew found at ${LINUXBREW_DIR}")
        set(ENV{YB_LINUXBREW_DIR} "${LINUXBREW_DIR}")
        set(USING_LINUXBREW TRUE)
      else()
        message(FATAL_ERROR
                "Linuxbrew is enabled but ${LINUXBREW_DIR} is not a valid Linuxbrew directory")
      endif()
    endif()
  endif()

  if(NOT USING_LINUXBREW)
    set(LINUXBREW_DIR "/tmp/not-using-linuxbrew")
  endif()

  set(USING_LINUXBREW "${USING_LINUXBREW}" PARENT_SCOPE)
  set(LINUXBREW_DIR "${LINUXBREW_DIR}" PARENT_SCOPE)
  set(LINUXBREW_LIB_DIR "${LINUXBREW_DIR}/lib" PARENT_SCOPE)
endfunction()

# Makes sure that we are using a supported compiler family.
function(EXPECT_COMPILER_TYPE_TO_BE_SET)
  if (NOT DEFINED YB_COMPILER_TYPE OR "${YB_COMPILER_TYPE}" STREQUAL "")
    message(FATAL_ERROR "The YB_COMPILER_TYPE CMake variable is not set or is empty")
  endif()
endfunction()

# Makes sure that we are using a supported compiler family.
function(VALIDATE_COMPILER_TYPE)
  if ("$ENV{YB_COMPILER_TYPE}" STREQUAL "")
    set(ENV{YB_COMPILER_TYPE} "${COMPILER_FAMILY}")
  endif()

  if(NOT "${YB_COMPILER_TYPE}" MATCHES "^${COMPILER_FAMILY}([0-9]*)$")
    message(FATAL_ERROR
            "Compiler type '${YB_COMPILER_TYPE}' does not match the compiler family "
            "'${COMPILER_FAMILY}'.")
  endif()

  # On macOS, we use the compiler type of simply "clang", without a major version suffix.
  # On Linux, we validate that the major version in the compiler type matches the actual one.
  if(NOT "${YB_COMPILER_TYPE}" STREQUAL "clang" AND
     NOT "${COMPILER_VERSION}" MATCHES "^${CMAKE_MATCH_1}[.].*$")
    message(FATAL_ERROR
            "Compiler version ${COMPILER_VERSION} does not match the major version "
            "${CMAKE_MATCH_1} from the compiler type ${YB_COMPILER_TYPE}.")
  endif()

  if (NOT IS_GCC AND
      NOT IS_CLANG)
    message(FATAL_ERROR "Unknown compiler family: ${COMPILER_FAMILY} (expected 'gcc' or 'clang').")
  endif()
endfunction()

# Check if the given directory is not an empty string and also warn if it does not exist.
function(_CHECK_LIB_DIR DIR_PATH DESCRIPTION)
  if (DIR_PATH STREQUAL "")
    message(FATAL_ERROR "Trying to add an empty ${DESCRIPTION}.")
  endif()
  if(NOT EXISTS "${DIR_PATH}" AND
     # The postgres/lib subdirectory of the build directory is a known case of a library directory
     # that does not exist in the beginning of the build. Skip the message in that case.
     NOT "${DIR_PATH}" STREQUAL "${YB_BUILD_ROOT}/postgres/lib")
    message(
      WARNING
      "Adding a non-existent ${DESCRIPTION} '${DIR_PATH}'. "
      "This might be OK in case the directory is created during the build.")
  endif()
endfunction()

# CXX_YB_COMMON_FLAGS are flags that are common across the 'src/yb' portion of the codebase (but do
# not apply to the 'src/rocksdb' part). "Common" in the name refers to the fact that they are common
# across different build types.
#
# Compiler flags that are common across debug/release builds:
#  -msse4.2: Enable sse4.2 compiler intrinsics.
#  -Wall: Enable all warnings.
#  -Wno-sign-compare: suppress warnings for comparison between signed and unsigned integers
#  -Wno-deprecated: some of the gutil code includes old things like ext/hash_set, ignore that
#  -pthread: enable multithreaded malloc
#  -fno-strict-aliasing
#     Assume programs do not follow strict aliasing rules.  GCC cannot always verify whether strict
#     aliasing rules are indeed followed due to fundamental limitations in escape analysis, which
#     can result in subtle bad code generation.  This has a small perf hit but worth it to avoid
#     hard to debug crashes.

function(yb_remember_dependency target)
  if("${ARGN}" STREQUAL "")
    message(FATAL_ERROR "yb_remember_dependency() called with no arguments")
  endif()
  # We use \\n instead of a real newline as this is stored in the CMake cache, and some versions
  # of CMake can't parse their own cache in case some values have newlines.
  set(YB_ALL_DEPS "${YB_ALL_DEPS}\\n${target}: ${ARGN}" CACHE INTERNAL "All dependencies" FORCE)
endfunction()

# Wrap add_dependencies so that we can capture dependencies for external processing. We use this
# when determining what tests to run for a particular set of changes.
function(add_dependencies target)
  if (TARGET "${target}" OR NOT ${YB_FILTERING_TARGETS})
    yb_remember_dependency(${target} ${ARGN})
    _add_dependencies(${target} ${ARGN})
  endif()
endfunction()

function(target_link_libraries target)
  if (TARGET "${target}" OR NOT ${YB_FILTERING_TARGETS})
    yb_remember_dependency(${target} ${ARGN})
    _target_link_libraries(${target} ${ARGN})
  endif()
endfunction()

# We override add_executable to ensure that whenever any executable is built, the latest symlink is
# re-created, and also to filter the set of executables if -DYB_EXECUTABLE_FILTER_RE is specified.
# This filtering is useful to keep CLion responsive when only working on a subset of code including
# e.g. yb-master / yb-tserver and some tests.
function(add_executable name)
  if (NOT ${YB_ADDING_TEST_EXECUTABLE})
    # Count non-test executables.
    math(EXPR NEW_NUM_EXECUTABLES "${YB_NUM_EXECUTABLES} + 1")
    set(YB_NUM_EXECUTABLES "${NEW_NUM_EXECUTABLES}" CACHE INTERNAL "Number of executables" FORCE)
  endif()

  if (NOT "${YB_EXECUTABLE_FILTER_RE}" STREQUAL "" AND
      NOT ${YB_ADDING_TEST_EXECUTABLE} AND
      NOT "${name}" STREQUAL "bfql_codegen" AND
      NOT "${name}" STREQUAL "bfpg_codegen" AND
      NOT "${name}" STREQUAL "run-with-timeout" AND
      NOT "${name}" STREQUAL "protoc-gen-insertions" AND
      NOT "${name}" STREQUAL "protoc-gen-yrpc")
    # Only do this filtering for non-test executables. Tests can be filtered separately using
    # YB_TEST_FILTER_RE.
    string(REGEX MATCH "${YB_EXECUTABLE_FILTER_RE}" EXECUTABLE_FILTER_MATCH_RESULT "${name}")
    if ("${EXECUTABLE_FILTER_MATCH_RESULT}" STREQUAL "")
      return()
    endif()
    message("Executable matched the filter: ${name}")
  endif()

  if (NOT ${YB_ADDING_TEST_EXECUTABLE})
    math(EXPR NEW_NUM_INCLUDED_EXECUTABLES "${YB_NUM_INCLUDED_EXECUTABLES} + 1")
    set(YB_NUM_INCLUDED_EXECUTABLES "${NEW_NUM_INCLUDED_EXECUTABLES}" CACHE INTERNAL
        "Number of included executables" FORCE)
  endif()

  # Call through to the original add_executable function.
  _add_executable("${name}" ${ARGN})
  if (NOT "$ENV{YB_DISABLE_LATEST_SYMLINK}" STREQUAL "1")
    add_dependencies(${name} latest_symlink)
  endif()

  if("${YB_TCMALLOC_ENABLED}" STREQUAL "1")
    # Link every executable with the tcmalloc static library.
    # If using Google TCMalloc, Abseil is also required, and will be linked by the libraries that
    # need it.
    # If using gperftools TCMalloc, libprofiler is also required, and will be linked by the
    # libraries that need it.
    #
    # We need to ensure that all symbols from the tcmalloc library are retained. This is done
    # differently depending on the OS.
    target_link_libraries(${name} "${TCMALLOC_STATIC_LIB_LD_FLAGS}")
    if("${YB_GOOGLE_TCMALLOC}" STREQUAL "1")
      target_link_libraries("${name}" absl)
    else()
      target_link_libraries("${name}" profiler)
    endif()
    if(IS_CLANG)
      # Static tcmalloc library depends on libc++ when building with Clang.
      target_link_libraries("${name}" c++)
    endif()
    # Link with libm because tcmalloc requires the log2 function.
    target_link_libraries("${name}" m)
  endif()

  yb_process_pch(${name})
endfunction()

function(add_library name)
  _add_library("${name}" ${ARGN})
  yb_process_pch(${name})
endfunction()

macro(YB_SETUP_CLANG)
  ADD_CXX_FLAGS("-stdlib=libc++")

  # Disables using the precompiled template specializations for std::string, shared_ptr, etc
  # so that the annotations in the header actually take effect.
  ADD_CXX_FLAGS("-D_GLIBCXX_EXTERN_TEMPLATE=0")

  if(NOT APPLE)
    set(LIBCXX_DIR "${YB_THIRDPARTY_INSTALLED_DIR}/${THIRDPARTY_INSTRUMENTATION_TYPE}/libcxx")
    if(NOT EXISTS "${LIBCXX_DIR}")
      message(FATAL_ERROR "libc++ directory does not exist: '${LIBCXX_DIR}'")
    endif()
    set(LIBCXX_INCLUDE_DIR "${LIBCXX_DIR}/include/c++/v1")
    if(NOT EXISTS "${LIBCXX_INCLUDE_DIR}")
      message(FATAL_ERROR "libc++ include directory does not exist: '${LIBCXX_INCLUDE_DIR}'")
    endif()
    if(NOT EXISTS "${LIBCXX_DIR}/lib")
      message(FATAL_ERROR "libc++ library directory does not exist: '${LIBCXX_DIR}/lib'")
    endif()
    ADD_GLOBAL_RPATH_ENTRY_AND_LIB_DIR("${LIBCXX_DIR}/lib")

    # This needs to appear before adding third-party dependencies that have their headers in the
    # Linuxbrew include directory, because otherwise we'll pick up the standard library headers from
    # the Linuxbrew include directory too.
    include_directories(SYSTEM "${LIBCXX_INCLUDE_DIR}")

    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-search-dirs
                    OUTPUT_VARIABLE CLANG_PRINT_SEARCH_DIRS_OUTPUT)

    if ("${CLANG_PRINT_SEARCH_DIRS_OUTPUT}" MATCHES ".*libraries: =([^:]+)(:.*|$)" )
      # We get a directory like this:
      # .../yb-llvm-v12.0.1-yb-1-1639783720-bdb147e6-almalinux8-x86_64/lib/clang/12.0.1
      set(CLANG_LIB_DIR "${CMAKE_MATCH_1}")
      set(CLANG_RUNTIME_LIB_DIR "${CLANG_LIB_DIR}/lib/linux")
      if(NOT EXISTS "${CLANG_RUNTIME_LIB_DIR}")
        set(CLANG_RUNTIME_LIB_DIR
            "${CLANG_LIB_DIR}/lib/${CMAKE_SYSTEM_PROCESSOR}-unknown-linux-gnu")
        if(NOT EXISTS "${CLANG_RUNTIME_LIB_DIR}")
          message(FATAL_ERROR
                  "Failed to determine Clang runtime library directory inside of "
                  "${CLANG_RUNTIME_LIB_DIR}/lib")
        endif()
      endif()
    else()
      message(FATAL_ERROR
              "Could not parse the output of 'clang -print-search-dirs': "
              "${CLANG_PRINT_SEARCH_DIRS_OUTPUT}")
    endif()
    if(USING_LINUXBREW)
      set(CLANG_INCLUDE_DIR "${CLANG_LIB_DIR}/include")
      if(NOT EXISTS "${CLANG_INCLUDE_DIR}")
        message(FATAL_ERROR "Clang include directory '${CLANG_INCLUDE_DIR}' does not exist")
      endif()
      ADD_CXX_FLAGS("-isystem ${CLANG_INCLUDE_DIR}")
    endif()

    if ("${COMPILER_VERSION}" VERSION_GREATER_EQUAL "12.0.0")
      ADD_LINKER_FLAGS("-fuse-ld=lld")
      ADD_LINKER_FLAGS("-lunwind")
    endif()

    ADD_CXX_FLAGS("-nostdinc++")
    if(USING_LINUXBREW)
      ADD_CXX_FLAGS("-nostdinc")
    endif()
  endif()
endmacro()

# This is a macro because we need to call functions that set flags on the parent scope.
macro(YB_SETUP_SANITIZER)
  if(APPLE)
    message(FATAL_ERROR "Sanitizers not supported on macOS")
  endif()
  if("${YB_BUILD_TYPE}" STREQUAL "asan")
    if(IS_CLANG)
      ADD_CXX_FLAGS("-mllvm -asan-use-private-alias=1")
      ADD_LINKER_FLAGS("-lunwind")

      # TODO: this is mostly needed because we depend on the ASAN runtime shared library and that
      # depends on libc++ but does not have the rpath set correctly, so we have to add our own
      # dependency on libc++ so it gets resolved using our rpath.
      ADD_LINKER_FLAGS("-lc++")

      if("${CLANG_RUNTIME_LIB_DIR}" STREQUAL "")
        message(FATAL_ERROR "CLANG_RUNTIME_LIB_DIR is not set")
      endif()
      if(NOT EXISTS "${CLANG_RUNTIME_LIB_DIR}")
        message(FATAL_ERROR "Clang runtime directory does not exist: ${CLANG_RUNTIME_LIB_DIR}")
      endif()
      ADD_GLOBAL_RPATH_ENTRY("${CLANG_RUNTIME_LIB_DIR}")
    endif()

    ADD_CXX_FLAGS("-fsanitize=address")
    ADD_CXX_FLAGS("-DADDRESS_SANITIZER")

    # Compile and link against the thirdparty ASAN instrumented libstdcxx.
    ADD_EXE_LINKER_FLAGS("-fsanitize=address")
    if(IS_GCC)
      ADD_EXE_LINKER_FLAGS("-lubsan -ldl")
      ADD_CXX_FLAGS("-Wno-error=maybe-uninitialized")
    endif()
  elseif("${YB_BUILD_TYPE}" STREQUAL "tsan")
    ADD_CXX_FLAGS("-fsanitize=thread")

    # Enables dynamic_annotations.h to actually generate code
    ADD_CXX_FLAGS("-DDYNAMIC_ANNOTATIONS_ENABLED")

    # changes atomicops to use the tsan implementations
    ADD_CXX_FLAGS("-DTHREAD_SANITIZER")

    # Compile and link against the thirdparty TSAN instrumented libstdcxx.
    ADD_EXE_LINKER_FLAGS("-fsanitize=thread")
    if(IS_CLANG AND
       "${COMPILER_VERSION}" VERSION_GREATER_EQUAL "10.0.0")
      # To avoid issues with missing libunwind symbols:
      # https://gist.githubusercontent.com/mbautin/5bc53ed2d342eab300aec7120eb42996/raw
      ADD_EXE_LINKER_FLAGS("-lunwind")
    endif()
  else()
    message(FATAL_ERROR "Invalid build type for YB_SETUP_SANITIZER: '${YB_BUILD_TYPE}'")
  endif()
endmacro()

function(SHOW_FOUND_BOOST_DETAILS BOOST_LIBRARY_TYPE)
  message("Results of finding Boost ${BOOST_LIBRARY_TYPE} libraries:")
  message("    Boost_FOUND: ${Boost_FOUND}")
  message("    Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")
  message("    Boost_LIBRARY_DIRS: ${Boost_LIBRARY_DIRS}")
  message("    Boost_LIBRARIES: ${Boost_LIBRARIES}")
  message("    Boost_SYSTEM_FOUND: ${Boost_SYSTEM_FOUND}")
  message("    Boost_SYSTEM_LIBRARY: ${Boost_SYSTEM_LIBRARY}")
  message("    Boost_THREAD_FOUND: ${Boost_THREAD_FOUND}")
  message("    Boost_THREAD_LIBRARY: ${Boost_THREAD_LIBRARY}")
  message("    Boost_VERSION: ${Boost_VERSION}")
  message("    Boost_LIB_VERSION: ${Boost_LIB_VERSION}")
  message("    Boost_MAJOR_VERSION: ${Boost_MAJOR_VERSION}")
  message("    Boost_MINOR_VERSION: ${Boost_MINOR_VERSION}")
  message("    Boost_SUBMINOR_VERSION: ${Boost_SUBMINOR_VERSION}")
  message("    Boost_LIB_DIAGNOSTIC_DEFINITIONS: ${Boost_LIB_DIAGNOSTIC_DEFINITIONS}")
endfunction()

# Setup target to use precompiled headers.
function(yb_process_pch target)
  if("${YB_PCH_PREFIX}" STREQUAL "" OR NOT ${YB_PCH_ON})
    return()
  endif()

  set(pch_file_property "PCH_FILE_FOR_${YB_PCH_PREFIX}")
  get_property(pch_file GLOBAL PROPERTY "${pch_file_property}")

  if ("${pch_file}" STREQUAL "")
    _add_library(${YB_PCH_PREFIX}_pch SHARED)
    get_target_property(build_dir ${YB_PCH_PREFIX}_pch BINARY_DIR)
    set(pch_dir "${build_dir}/CMakeFiles/${YB_PCH_PREFIX}_pch.dir")
    set(pch_file "${pch_dir}/${YB_PCH_PREFIX}_pch.h.pch")
    set(pch_cc_file "${build_dir}/${YB_PCH_PREFIX}_pch.cc")
    set(pch_h_file_copy "${pch_dir}/${YB_PCH_PREFIX}_pch.h")

    get_target_property(source_dir ${target} SOURCE_DIR)
    set(pch_h_file "${source_dir}/${YB_PCH_PATH}${YB_PCH_PREFIX}_pch.h")

    message("Generating PCH for ${YB_PCH_PREFIX}: ${pch_file}")
    set_property(GLOBAL PROPERTY "${pch_file_property}" "${pch_file}")

    if(NOT EXISTS "${pch_cc_file}")
      file(MAKE_DIRECTORY "${build_dir}")
      file(TOUCH "${pch_cc_file}")
    endif()
    # This file is only required by CLion, so could be out of date.
    if(NOT EXISTS "${pch_h_file_copy}")
      file(MAKE_DIRECTORY "${pch_dir}")
      file(COPY "${pch_h_file}" DESTINATION "${pch_dir}")
    endif()
    target_sources(${YB_PCH_PREFIX}_pch PRIVATE "${pch_cc_file}")

    set_source_files_properties(
        "${YB_PCH_PREFIX}_pch.cc" PROPERTIES
        COMPILE_FLAGS "-yb-pch ${source_dir}/${YB_PCH_PATH}${YB_PCH_PREFIX}_pch.h")

    if (NOT "${YB_PCH_DEP_LIBS}" STREQUAL "")
      target_link_libraries(${YB_PCH_PREFIX}_pch PUBLIC "${YB_PCH_DEP_LIBS}")
    endif()
    target_link_libraries(${YB_PCH_PREFIX}_pch PUBLIC "${YB_BASE_LIBS}")

    # Intermediate target is required to make sure that PCH file generated before any dependent
    # binary source file compilation.
    add_custom_target(${YB_PCH_PREFIX}_pch_proxy DEPENDS "${pch_h_file}")
    add_dependencies(${YB_PCH_PREFIX}_pch_proxy "${YB_PCH_PREFIX}_pch")
  endif()

  yb_use_pch(${target} ${YB_PCH_PREFIX})
endfunction()

function(yb_use_pch target prefix)
  if(NOT ${YB_PCH_ON})
    return()
  endif()

  set(pch_file_property "PCH_FILE_FOR_${prefix}")
  get_property(pch_file GLOBAL PROPERTY "${pch_file_property}")

  if ("${pch_file}" STREQUAL "")
    message(FATAL_ERROR "PCH file not set for ${prefix}")
  endif()

  set(use_pch_flags "-Xclang -include-pch -Xclang ${pch_file}")
  get_target_property(compile_flags ${target} COMPILE_FLAGS)
  if (NOT ${compile_flags} STREQUAL "compile_flags-NOTFOUND")
    set(compile_flags "${compile_flags} ${use_pch_flags}")
  else()
    set(compile_flags "${use_pch_flags}")
  endif ()
  set_target_properties(${target} PROPERTIES COMPILE_FLAGS "${compile_flags}")
  target_link_libraries(${target} ${prefix}_pch)
  add_dependencies(${target} ${prefix}_pch_proxy)
endfunction()

# A wrapper around add_library() for YugabyteDB libraries.
#
# Required arguments:
#
# LIB_NAME is the name of the library. It must come first.
# SRCS is the list of source files to compile into the library.
# DEPS is the list of targets that both library variants depend on.
#
# Optional arguments:
#
# NONLINK_DEPS is the list of (non-linked) targets that the library depends on.
# COMPILE_FLAGS is a string containing any additional compilation flags that should be added to the
# library.
function(ADD_YB_LIBRARY LIB_NAME)
  # Parse the arguments.
  set(options "")
  set(one_value_args COMPILE_FLAGS)
  set(multi_value_args SRCS DEPS NONLINK_DEPS)
  cmake_parse_arguments(ARG "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})
  if(ARG_UNPARSED_ARGUMENTS)
    message(SEND_ERROR "Error: unrecognized arguments: ${ARG_UNPARSED_ARGUMENTS}")
  endif()

  add_library(${LIB_NAME} ${ARG_SRCS})

  if(ARG_DEPS)
    target_link_libraries(${LIB_NAME} ${ARG_DEPS})
  endif()
  if(ARG_NONLINK_DEPS)
    add_dependencies(${LIB_NAME} ${ARG_NONLINK_DEPS})
  endif()
endfunction()

# Import a shared library (e.g. libpq) that is built as part of PostgreSQL as a CMake target.
# See https://github.com/yugabyte/yugabyte-db/issues/5853: we currently have to run this function
# separately for each library from each CMakeLists.txt file that uses that library.
function(ADD_POSTGRES_SHARED_LIBRARY LIB_NAME SHARED_LIB_PATH)
  if ("${SHARED_LIB_PATH}" STREQUAL "")
    message(FATAL_ERROR
            "Shared library path cannot be empty. "
            "LIB_NAME=${LIB_NAME} "
            "CMAKE_CURRENT_LIST_DIR=${CMAKE_CURRENT_LIST_DIR}")
  endif()
  add_library(${LIB_NAME} SHARED IMPORTED)
  set_target_properties(${LIB_NAME} PROPERTIES IMPORTED_LOCATION "${SHARED_LIB_PATH}")
  # "postgres" is the target that actually builds this shared library.
  add_dependencies(${LIB_NAME} postgres)
  message("Added target ${LIB_NAME} for a shared library built as part of PostgreSQL code: "
          "${SHARED_LIB_PATH} (invoked from ${CMAKE_CURRENT_LIST_FILE})")
endfunction()

# To be invoked from functions that are themselves invoked from the global scope.
macro(detect_lto_type_from_linking_type)
  if("${CMAKE_CURRENT_FUNCTION}" STREQUAL "")
    message(FATAL_ERROR "The detect_lto_type_from_linking_type must be invoked from a function")
  endif()
  if ("${YB_LINKING_TYPE}" STREQUAL "")
    message(FATAL_ERROR "YB_LINKING_TYPE is not set")
  endif()
  # Set the YB_LTO_ENABLED variable in the parent scope of the calling function (this is a macro).
  if("${YB_LINKING_TYPE}" MATCHES "^(.*)-lto$")
    set(YB_LTO_ENABLED ON PARENT_SCOPE)
    message("Enabling ${CMAKE_MATCH_1} LTO based on linking type: ${YB_LINKING_TYPE}")
    set(YB_LTO_TYPE "${CMAKE_MATCH_1}" PARENT_SCOPE)
  else()
    set(YB_LTO_ENABLED OFF PARENT_SCOPE)
    set(YB_LTO_TYPE "" PARENT_SCOPE)
  endif()
endmacro()

function(parse_build_root_basename)
  if ("${BUILD_SUPPORT_DIR}" STREQUAL "")
    message(FATAL_ERROR "BUILD_SUPPORT_DIR is not set in parse_build_root_basename")
  endif()
  get_filename_component(YB_BUILD_ROOT_BASENAME "${CMAKE_CURRENT_BINARY_DIR}" NAME)

  execute_process(COMMAND "${BUILD_SUPPORT_DIR}/show_build_root_name_regex.sh"
               OUTPUT_VARIABLE BUILD_ROOT_BASENAME_RE)
  # Remove trailing new line from BUILD_ROOT_BASENAME_RE.
  string(REGEX REPLACE "\n$" "" BUILD_ROOT_BASENAME_RE "${BUILD_ROOT_BASENAME_RE}")
  string(REGEX MATCH "${BUILD_ROOT_BASENAME_RE}" RE_MATCH_RESULT "${YB_BUILD_ROOT_BASENAME}")
  if("$ENV{YB_DEBUG_BUILD_ROOT_BASENAME_PARSING}" STREQUAL "1")
    message("Parsing build root basename: ${YB_BUILD_ROOT_BASENAME}")
    message("Regular expression: ${BUILD_ROOT_BASENAME_RE}")
    message("Capture groups (note that some components are repeated with and without a leading "
            "dash):")
    foreach(MATCH_INDEX RANGE 1 9)
      message("    CMAKE_MATCH_${MATCH_INDEX}=${CMAKE_MATCH_${MATCH_INDEX}}")
    endforeach()
  endif()

  set(YB_BUILD_TYPE "${CMAKE_MATCH_1}" PARENT_SCOPE)

  # -----------------------------------------------------------------------------------------------
  # YB_COMPILER_TYPE
  # -----------------------------------------------------------------------------------------------

  set(YB_COMPILER_TYPE_FROM_BUILD_ROOT_BASENAME "${CMAKE_MATCH_2}")
  if(NOT "${YB_COMPILER_TYPE}" STREQUAL "" AND
     NOT "${YB_COMPILER_TYPE}" STREQUAL "${YB_COMPILER_TYPE_FROM_BUILD_ROOT_BASENAME}")
    message(
        FATAL_ERROR
        "The YB_COMPILER_TYPE CMake variable is already set to '${YB_COMPILER_TYPE}', but the "
        "value auto-detected from the build root basename '${YB_BUILD_ROOT_BASENAME}' is "
        "different: '${YB_COMPILER_TYPE_FROM_BUILD_ROOT_BASENAME}'.")
  endif()

  if(NOT "$ENV{YB_COMPILER_TYPE}" STREQUAL "" AND
     NOT "$ENV{YB_COMPILER_TYPE}" STREQUAL "${YB_COMPILER_TYPE_FROM_BUILD_ROOT_BASENAME}")
    message(
        FATAL_ERROR
        "The YB_COMPILER_TYPE environment variable is already set to '${YB_COMPILER_TYPE}', but "
        "the value auto-detected from the build root basename '${YB_BUILD_ROOT_BASENAME}' is "
        "different: '${YB_COMPILER_TYPE_FROM_BUILD_ROOT_BASENAME}'.")
  endif()

  set(YB_COMPILER_TYPE "${YB_COMPILER_TYPE_FROM_BUILD_ROOT_BASENAME}" PARENT_SCOPE)
  set(ENV{YB_COMPILER_TYPE} "${YB_COMPILER_TYPE_FROM_BUILD_ROOT_BASENAME}")

  # -----------------------------------------------------------------------------------------------
  # YB_USING_LINUXBREW_FROM_BUILD_ROOT
  # -----------------------------------------------------------------------------------------------

  if (NOT "${CMAKE_MATCH_3}" STREQUAL "-linuxbrew" AND
      NOT "${CMAKE_MATCH_3}" STREQUAL "")
    message(FATAL_ERROR
            "Invalid value of the 3rd capture group for build root basename"
            "'${YB_BUILD_ROOT_BASENAME}': either '-linuxbrew' or an empty string.")
  endif()

  if ("${CMAKE_MATCH_3}" STREQUAL "-linuxbrew")
    set(YB_USING_LINUXBREW_FROM_BUILD_ROOT ON PARENT_SCOPE)
  else()
    set(YB_USING_LINUXBREW_FROM_BUILD_ROOT OFF PARENT_SCOPE)
  endif()

  # -----------------------------------------------------------------------------------------------
  # YB_LINKING_TYPE
  # -----------------------------------------------------------------------------------------------

  set(YB_LINKING_TYPE "${CMAKE_MATCH_5}")
  if(NOT "${YB_LINKING_TYPE}" MATCHES "^(dynamic|thin-lto|full-lto)$")
    message(
        FATAL_ERROR
        "Invalid linking type from the build root basename '${YB_BUILD_ROOT_BASENAME}': "
        "'${YB_LINKING_TYPE}'. Expected 'dynamic', 'thin-lto', or 'full-lto'.")
  endif()
  set(YB_LINKING_TYPE "${YB_LINKING_TYPE}" PARENT_SCOPE)
  detect_lto_type_from_linking_type()

  set(OPTIONAL_DASH_NINJA "${CMAKE_MATCH_8}")
  if(NOT "${OPTIONAL_DASH_NINJA}" STREQUAL "" AND
     NOT "${OPTIONAL_DASH_NINJA}" STREQUAL "-ninja")
    message(FATAL_ERROR
            "Invalid value of the 8th capture group for build root basename"
            "'${YB_BUILD_ROOT_BASENAME}': either '-ninja' or an empty string.")
  endif()

  set(YB_TARGET_ARCH_FROM_BUILD_ROOT "${CMAKE_MATCH_7}")
  if (NOT "${YB_TARGET_ARCH_FROM_BUILD_ROOT}" STREQUAL "" AND
      NOT "${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "${YB_TARGET_ARCH_FROM_BUILD_ROOT}")
    message(
        FATAL_ERROR
        "Target architecture inferred from build root is '${YB_TARGET_ARCH_FROM_BUILD_ROOT}', "
        "but CMAKE_SYSTEM_PROCESSOR is ${CMAKE_SYSTEM_PROCESSOR}")
  endif()
endfunction()

macro(configure_macos_sdk)
  # If the build type is e.g. "clang15", we consider this not to be Apple Clang but custom-built
  # LLVM on macOS.
  if(APPLE AND NOT IS_APPLE_CLANG)
    ADD_LINKER_FLAGS("-L${CMAKE_OSX_SYSROOT}/usr/lib")
  endif()
endmacro()

function(add_latest_symlink_target)
  # Provide a 'latest' symlink to this build directory if the "blessed" multi-build layout is
  # detected:
  #
  # build/
  # build/<first build directory>
  # build/<second build directory>
  # ...
  set(LATEST_BUILD_SYMLINK_PATH "${YB_BUILD_ROOT_PARENT}/latest")
  if (NOT "$ENV{YB_DISABLE_LATEST_SYMLINK}" STREQUAL "1")
    message("LATEST SYMLINK PATH: ${LATEST_BUILD_SYMLINK_PATH}")
    if ("${CMAKE_CURRENT_BINARY_DIR}" STREQUAL "${LATEST_BUILD_SYMLINK_PATH}")
      message(FATAL_ERROR
              "Should not run cmake inside the build/latest symlink. "
              "First change directories into the destination of the symlink.")
    endif()

    add_custom_target(latest_symlink ALL
      "${BUILD_SUPPORT_DIR}/create_latest_symlink.sh"
      "${CMAKE_CURRENT_BINARY_DIR}"
      "${LATEST_BUILD_SYMLINK_PATH}"
      COMMENT "Recreating the 'latest' symlink at '${LATEST_BUILD_SYMLINK_PATH}'")
  endif()
endfunction()

# -------------------------------------------------------------------------------------------------
# LTO support
# -------------------------------------------------------------------------------------------------

function(enable_lto_if_needed)
  if(NOT DEFINED COMPILER_FAMILY)
    message(FATAL_ERROR "COMPILER_FAMILY not defined")
  endif()
  if(NOT DEFINED USING_LINUXBREW)
    message(FATAL_ERROR "USING_LINUXBREW not defined")
  endif()
  if(NOT DEFINED YB_BUILD_TYPE)
    message(FATAL_ERROR "YB_BUILD_TYPE not defined")
  endif()

  detect_lto_type_from_linking_type()
  if(YB_LTO_ENABLED)
    if(YB_BUILD_TYPE STREQUAL "prof_gen")
      # We need a "mostly static" build (one big binary and few libs)
      # for tserver in order to dump all the counters. Our linking type "lto"
      # does it, but it also turns on link-time optimizations (LTO) for clang.
      # LTO and profile generation together produce incorrect counters for
      # inlined functions in clang15. That's why we need to remove
      # -lto=${YB_LTO_TYPE} from out "lto" linking type when building for prof_gen.
      # TODO: remove after we switch to clang16 (the problem is fixed in current llvm main).
      # https://github.com/yugabyte/yugabyte-db/issues/15093
      ADD_CXX_FLAGS("-fuse-ld=lld")
    else()
      ADD_CXX_FLAGS("-flto=${YB_LTO_TYPE} -fuse-ld=lld")
    endif()
    # In LTO mode, yb-master / yb-tserver executables are generated with LTO, but we first generate
    # yb-master-dynamic and yb-tserver-dynamic binaries that are dynamically linked.
    set_in_current_and_parent_scope(YB_DYNAMICALLY_LINKED_EXE_SUFFIX "-dynamic" PARENT_SCOPE)
  else()
    message("Not enabling LTO: "
            "YB_BUILD_TYPE=${YB_BUILD_TYPE}, "
            "YB_LINKING_TYPE=${YB_LINKING_TYPE}, "
            "COMPILER_FAMILY=${COMPILER_FAMILY}, "
            "USING_LINUXBREW=${USING_LINUXBREW}, "
            "APPLE=${APPLE}")
    # In non-LTO builds, yb-master / yb-tserver executables themselves are dynamically linked to
    # other YB libraries.
    set_in_current_and_parent_scope(YB_DYNAMICALLY_LINKED_EXE_SUFFIX "")
  endif()

  # Only set these in parent scope.
  set(YB_MASTER_DYNAMIC_EXE_NAME "yb-master${YB_DYNAMICALLY_LINKED_EXE_SUFFIX}" PARENT_SCOPE)
  set(YB_TSERVER_DYNAMIC_EXE_NAME "yb-tserver${YB_DYNAMICALLY_LINKED_EXE_SUFFIX}" PARENT_SCOPE)
endfunction()

function(yb_add_lto_target original_exe_name output_exe_name symlink_as_names)
  assert_vars_defined(YB_LTO_ENABLED YB_DYNAMICALLY_LINKED_EXE_SUFFIX)
  if(NOT YB_LTO_ENABLED)
    return()
  endif()

  if("$ENV{YB_SKIP_FINAL_LTO_LINK}" STREQUAL "1")
    message("Skipping adding LTO target ${output_exe_name} because the YB_SKIP_FINAL_LTO_LINK "
            "environment variable is set to 1")
    return()
  endif()

  set(dynamic_exe_name "${original_exe_name}${YB_DYNAMICALLY_LINKED_EXE_SUFFIX}")
  message("Adding LTO target: ${output_exe_name} "
          "(LTO equivalent of ${dynamic_exe_name})")
  set(output_executable_path "${EXECUTABLE_OUTPUT_PATH}/${output_exe_name}")

  set(cmd_args
      "${BUILD_SUPPORT_DIR}/dependency_graph"
      "--build-root=${YB_BUILD_ROOT}"
      # Use $$ to escape $.
      "--file-regex=^.*/${dynamic_exe_name}$$"
      # Allow LTO linking in parallel with the rest of the build.
      --incomplete-build
      "--lto-output-path=${output_executable_path}"
      "--never-run-build")

  foreach(symlink_name IN LISTS symlink_as_names)
    list(APPEND cmd_args "--symlink-as=${symlink_name}")
  endforeach()
  list(APPEND cmd_args "link-whole-program")

  message("Command for generating LTO target ${output_exe_name}: ${cmd_args}")
  add_custom_command(
    OUTPUT "${output_executable_path}"
    COMMAND ${cmd_args}
    DEPENDS "${dynamic_exe_name}"
  )

  add_custom_target("${output_exe_name}" ALL DEPENDS "${output_executable_path}")
  foreach(symlink_name IN LISTS symlink_as_names)
    # For each symlinked executable name (yb-master, yb-tserver) create an alias target that will
    # cause the LTO executable to be built.
    add_custom_target("${symlink_name}" DEPENDS "${output_executable_path}")
  endforeach()

  # We need to build the corresponding non-LTO executable first, such as yb-master or yb-tserver.
  add_dependencies("${output_exe_name}" "${dynamic_exe_name}")
endfunction()

# -------------------------------------------------------------------------------------------------

macro(yb_setup_odyssey)
  # Flags common to C and C++.
  set(OD_EXTRA_COMPILER_FLAGS
      -Wno-implicit-fallthrough
      -Wno-missing-field-initializers
      -Wno-unused-but-set-variable
      -Wno-unused-function
      -Wno-unused-parameter
      -Wno-unused-variable
      # This is needed to e.g. have access to pthread_setname_np when including pthread.h.
      -D_GNU_SOURCE
      # This is needed so that compiler can throw warnings instead of errors
      # for the uninitialized variables throughout the odyssey code base.
      -Wno-uninitialized
     )
  set(OD_EXTRA_C_FLAGS
      -Wno-strict-prototypes
      # https://gist.githubusercontent.com/mbautin/323dd6fe9c6685377288397d4adf826c/raw
      -Wno-incompatible-pointer-types
     )

  if(IS_CLANG)
    list(APPEND OD_EXTRA_COMPILER_FLAGS
         -Wno-language-extension-token
         -Wno-shorten-64-to-32
         -Wno-static-in-inline
         -Wno-pointer-bool-conversion
         -Wno-newline-eof
        )
  endif()
  if(IS_GCC)
    list(APPEND OD_EXTRA_COMPILER_FLAGS -Wno-pedantic)
    if("${COMPILER_VERSION}" MATCHES "^1[2-9][.].*$")
      # To work around:
      # https://gist.github.com/mbautin/c4d4193ff1c2c310ff6ed4d8a01bd385
      list(APPEND OD_EXTRA_COMPILER_FLAGS -Wno-address)
    endif()
  endif()

  set(MACHINARIUM_INCLUDE_DIRS "${YB_SRC_ROOT}/src/odyssey/third_party/machinarium/sources")
  set(MACHINARIUM_LIBRARIES "machine_library_static")

  set(KIWI_INCLUDE_DIRS "${YB_SRC_ROOT}/src/odyssey/third_party/kiwi")
  set(KIWI_LIBRARIES "kw_library_static")

  set(POSTGRESQL_INCLUDE_DIR "${YB_BUILD_ROOT}/postgres_build/src/include")
  set(POSTGRESQL_LIBPGPORT "${PG_PORT_STATIC_LIB}")
  set(POSTGRESQL_LIBRARY "${PG_COMMON_STATIC_LIB}")
  set(PQ_LIBRARY "${LIBPQ_SHARED_LIB}")
  set(od_binary "odyssey")
  set(OD_EXTRA_LIBRARIES ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY})
  set(od_extra_dependencies "postgres")

  set(OD_EXTRA_EXE_LINKER_FLAGS "-L${YB_BUILD_ROOT}/lib")

  # We only use YB_ADD_LIB_DIR("${YB_THIRDPARTY_MAYBE_INSTRUMENTED_DIR}/lib") in the top-level
  # CMakeLists because the corresponding RPATH ends up on the linker command line in some other way,
  # presumably, due to library dependencies. However, for Odyssey, we have to add this RPATH
  # explicitly.
  string(APPEND OD_EXTRA_EXE_LINKER_FLAGS " ${YB_THIRDPARTY_MAYBE_INSTRUMENTED_RPATH_ARG}")

  add_subdirectory(src/odyssey/third_party/machinarium)
  add_subdirectory(src/odyssey/third_party/kiwi)
  add_subdirectory(src/odyssey)
endmacro()

# This macro is invoked in each Odyssey CMake file.
macro(add_extra_yb_flags_in_odyssey)
  add_compile_options(${OD_EXTRA_COMPILER_FLAGS})
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OD_EXTRA_EXE_LINKER_FLAGS}")
  foreach(od_extra_c_flag IN LISTS OD_EXTRA_C_FLAGS)
    string(APPEND CMAKE_C_FLAGS " ${od_extra_c_flag}")
  endforeach()
endmacro()

macro(add_cxx_no_sanitize_flag flag)
  if(NOT "${CXX_NO_SANITIZE_FLAG}" STREQUAL "")
    string(APPEND CXX_NO_SANITIZE_FLAG ",")
  endif()
  string(APPEND CXX_NO_SANITIZE_FLAG "${flag}")
endmacro()
