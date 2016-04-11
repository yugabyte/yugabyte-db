# Copyright (c) YugaByte, Inc.

# RocksDB build.

# We are using the Makefile-based build that RocksDB provided (as of 03/15/2016, RocksDB only
# provides a CMakeLists.txt file for Windows).
#
# However, we do an out-of-source build, and we use a custom script that mirrors the entire RocksDB
# source directory inside of the build/debug (or build/release) directory.

# Parse the RocksDB version out of "version.h"
function(get_rocksdb_version_component COMPONENT)
  string(REGEX MATCH
    " *#define +ROCKSDB_${COMPONENT} +[0-9]+"
    ROCKSDB_VERSION_COMPONENT
    "${ROCKSDB_VERSION_H_CONTENTS}")
  if ("${ROCKSDB_VERSION_COMPONENT}" STREQUAL "")
    message(FATAL_ERROR "Failed to parse the ${COMPONENT} component of the RocksDB version")
  endif()

  string(REGEX REPLACE " *#define +ROCKSDB_${COMPONENT} +" ""
    ROCKSDB_VERSION_COMPONENT "${ROCKSDB_VERSION_COMPONENT}")
  if ("${ROCKSDB_VERSION_COMPONENT}" STREQUAL "")
    message(FATAL_ERROR "Failed to parse the ${COMPONENT} component of the RocksDB version")
  endif()
  set(ROCKSDB_${COMPONENT} ${ROCKSDB_VERSION_COMPONENT} PARENT_SCOPE)
endfunction()

file(READ "include/rocksdb/version.h" ROCKSDB_VERSION_H_CONTENTS)
get_rocksdb_version_component("MAJOR")
get_rocksdb_version_component("MINOR")
get_rocksdb_version_component("PATCH")
SET(ROCKSDB_VERSION "${ROCKSDB_MAJOR}.${ROCKSDB_MINOR}.${ROCKSDB_PATCH}"
  CACHE STRING "RocksDB version")
message("Detected RocksDB version: ${ROCKSDB_VERSION}")

# Find all RocksDB sources to make CLion recognize them as such. This is not being passed to a
# compiler.
file(GLOB_RECURSE ROCKSDB_SOURCES "*.h" "*.cc")

set(BUILD_ROCKSDB_ARGS
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/build-rocksdb.sh
  --make-parallelism "${PROCESSOR_COUNT}"
  --build-type "${CMAKE_BUILD_TYPE}"
  --build-dir "${YB_BUILD_DIR}"
  --debug-level "${ROCKSDB_DEBUG_LEVEL}"
  --link-mode "${YB_LINK}"
  --cxx-compiler "${CMAKE_CXX_COMPILER}"
  --c-compiler "${CMAKE_C_COMPILER}"
  ${ROCKSDB_BUILD_USE_LD_GOLD_ARG}
)

add_custom_target(build_rocksdb_library ALL
  ${BUILD_ROCKSDB_ARGS}
  VERBATIM
  COMMENT "Building RocksDB library")

add_custom_target(build_rocksdb_all_targets ALL
  ${BUILD_ROCKSDB_ARGS} --build-all-targets
  VERBATIM
  COMMENT "Building all RocksDB targets, including tests, benchmarks, and tools")

add_dependencies(build_rocksdb_all_targets build_rocksdb_library)

# This library is only here to make CLion navigation work for RocksDB code. It is not being built
# at all due to EXCLUDE_FROM_ALL. The RocksDB build still happens via its Makefile.
add_library(
  rocksdb_dummy_library_for_clion
  EXCLUDE_FROM_ALL
  ${ROCKSDB_SOURCES}
)
# Allow navigating to RocksDB's internal include files in CLion.
target_include_directories(rocksdb_dummy_library_for_clion PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

set(ROCKSDB_BUILD_DIR "${YB_BUILD_DIR}/rocksdb-build")

set(
  ROCKSDB_LIB_PATH_PREFIX "${ROCKSDB_BUILD_DIR}/librocksdb${ROCKSDB_LIB_SUFFIX}"
  CACHE STRING "RocksDB library path prefix")
set(ROCKSDB_VERSION_SUFFIX ".${ROCKSDB_VERSION}"
  CACHE STIRNG "RocksDB dynamic library version suffix")

if(APPLE)
  # On Mac OS X the dynamic library extension (".dylib") in the resulting RocksDB library is the
  # last component of the file name, following the version.
  set(ROCKSDB_SHARED_LIB
    "${ROCKSDB_LIB_PATH_PREFIX}${ROCKSDB_VERSION_SUFFIX}${YB_SHARED_LIBRARY_SUFFIX}"
    CACHE STRING "RocksDB shared library path")
  set(ROCKSDB_STATIC_LIB
    "${ROCKSDB_LIB_PATH_PREFIX}${ROCKSDB_VERSION_SUFFIX}${YB_STATIC_LIBRARY_SUFFIX}"
    CACHE STRING "RocksDB static library path")
else()
  # On Linux, the version is the last component of the library path.
  set(ROCKSDB_SHARED_LIB
    "${ROCKSDB_LIB_PATH_PREFIX}${YB_SHARED_LIBRARY_SUFFIX}${ROCKSDB_VERSION_SUFFIX}"
    CACHE STRING "RocksDB shared library path")
  set(ROCKSDB_STATIC_LIB
    "${ROCKSDB_LIB_PATH_PREFIX}${YB_STATIC_LIBRARY_SUFFIX}${ROCKSDB_VERSION_SUFFIX}"
    CACHE STRING "RocksDB static library path")
endif()
