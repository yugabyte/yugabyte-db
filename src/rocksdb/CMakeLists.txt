# Copyright (c) YugaByte, Inc.

add_custom_target(
  rocksdb ALL
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/build-rocksdb.sh
    --make-parallelism ${PROCESSOR_COUNT}
    --build-type ${CMAKE_BUILD_TYPE}
    --build-dir "${CMAKE_CURRENT_BINARY_DIR}/../.."
    --debug-level ${ROCKSDB_DEBUG_LEVEL}
  COMMENT "Building RocksDB"
  VERBATIM
)
