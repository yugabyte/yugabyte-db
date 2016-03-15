# Copyright (c) YugaByte, Inc.

# RocksDB build.

# We are using the Makefile-based build that RocksDB provided (as of 03/15/2016, RocksDB only
# provides a CMakeLists.txt file for Windows).
#
# However, we do an out-of-source build, and we use a custom script that mirrors the entire RocksDB
# source directory inside of the build/debug (or build/release) directory.

# Find all RocksDB sources to make CLion recognize them as such. This is not being used to invoke
# a compiler.
file(GLOB_RECURSE ROCKSDB_SOURCES
  "*.h"
  "*.cc"
  "Makefile")

add_custom_target(
  rocksdb ALL
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/build-rocksdb.sh
    --make-parallelism ${PROCESSOR_COUNT}
    --build-type ${CMAKE_BUILD_TYPE}
    --build-dir "${CMAKE_CURRENT_BINARY_DIR}/../.."
    --debug-level ${ROCKSDB_DEBUG_LEVEL}
    --link-mode ${YB_LINK}
  COMMENT "Building RocksDB"
  SOURCES ${ROCKSDB_SOURCES}
  VERBATIM
)
