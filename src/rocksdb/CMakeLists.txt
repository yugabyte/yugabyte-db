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

# Find all RocksDB sources to make CLion recognize them as such. This is not being used to invoke
# a compiler.
file(GLOB_RECURSE ROCKSDB_SOURCES
  "*.h"
  "*.cc"
  "Makefile")

add_custom_target(
  invoke_rocksdb_makefile ALL
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/build-rocksdb.sh
    --make-parallelism ${PROCESSOR_COUNT}
    --build-type ${CMAKE_BUILD_TYPE}
    --build-dir "${PROJECT_BINARY_DIR}"
    --debug-level ${ROCKSDB_DEBUG_LEVEL}
    --link-mode ${YB_LINK}
  COMMENT "Building RocksDB"
  SOURCES ${ROCKSDB_SOURCES}
  VERBATIM
)

set(ROCKSDB_BUILD_DIR "${PROJECT_BINARY_DIR}/rocksdb-build" CACHE STRING "RocksDB build directory")
