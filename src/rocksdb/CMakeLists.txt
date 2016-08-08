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

SET(ROCKSDB_BUILD_VERBOSE_ARG "")
if ("${YB_VERBOSE}" STREQUAL "1")
  SET(ROCKSDB_BUILD_VERBOSE_ARG "--verbose")
endif()

# A way to pass arbitrary options to make when building RocksDB.
set(ROCKSDB_MAKE_OPTS " ")

if ("${YB_USE_ASAN}")
  set(ROCKSDB_MAKE_OPTS "${ROCKSDB_MAKE_OPTS} COMPILE_WITH_ASAN=1")
endif()

SET(ROCKSDB_BUILD_DISABLE_TCMALLOC_ARG)
if (NOT YB_TCMALLOC_AVAILABLE) 
  SET(ROCKSDB_BUILD_DISABLE_TCMALLOC_ARG "--disable-tcmalloc")
endif()

# TODO: auto-detect the gflags version installed in the thirdparty directory.
set(BUILD_ROCKSDB_ARGS
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/build-rocksdb.sh
  --make-parallelism "${PROCESSOR_COUNT}"
  --build-dir "${YB_BUILD_DIR}"
  --debug-level "${ROCKSDB_DEBUG_LEVEL}"
  --link-mode "${YB_LINK}"
  --cxx-compiler "${CMAKE_CXX_COMPILER}"
  --c-compiler "${CMAKE_C_COMPILER}"
  --extra-include-dir "${YB_SRC_DIR}/thirdparty/gflags-2.1.2/include"
  --extra-include-dir "${YB_SRC_DIR}/src"
  --extra-lib-dir "${YB_SRC_DIR}/thirdparty/gflags-2.1.2/lib"
  ${ROCKSDB_BUILD_USE_LD_GOLD_ARG}
  ${ROCKSDB_BUILD_VERBOSE_ARG}
  --make-opts "${ROCKSDB_MAKE_OPTS}"
  ${ROCKSDB_BUILD_DISABLE_TCMALLOC_ARG}
)


set(ROCKSDB_CXX_FLAGS "${CXX_GLOBAL_COMMON_FLAGS}")
# Use the same flags for the C compiler as for the C++ compiler, with some optional customizations
# later.
set(ROCKSDB_C_FLAGS "${ROCKSDB_CXX_FLAGS}")

if ("${COMPILER_FAMILY}" STREQUAL "clang")
  # -Wno-parentheses-equality is there because otherwise there is this error on Mac OS X:
  # util/instrumented_mutex.cc:19:114: error: equality comparison with extraneous parentheses [-Werror,-Wparentheses-equality]
  #   PerfStepTimer perf_step_timer_db_mutex_lock_nanos(&(perf_context.db_mutex_lock_nanos), true); if ((stats_code_ == DB_MUTEX_WAIT_MICROS)) { perf_step_timer_db_mutex_lock_nanos.Start(); };
  #                                                                                                    ~~~~~~~~~~~~^~~~~~~~~~~~~~~~~~~~~~~
  # -Wno-unevaluated-expression is there to prevent the following error on Mac OS X:
  # util/autovector_test.cc:55:120: error: expression with side effects has no effect in an unevaluated context [-Werror,-Wunevaluated-expression]
  #     switch (0) case 0: default: if (const ::testing::AssertionResult gtest_ar = (::testing::internal:: EqHelper<(sizeof(::testing::internal::IsNullLiteralHelper(--size)) == 1)>::Compare("--size", "vec.size()", --size, vec.size()))) ; else return ::testing::internal::AssertHelper(::testing::TestPartResult::kFatalFailure, "util/autovector_test.cc", 55, gtest_ar.failure_message()) = ::testing::Message();

  set(ROCKSDB_CXX_FLAGS "${ROCKSDB_CXX_FLAGS} -Wno-parentheses-equality")
  set(ROCKSDB_CXX_FLAGS "${ROCKSDB_CXX_FLAGS} -Wno-unevaluated-expression")

  if(EXISTS "/etc/centos-release")
    # Silence an error thrown by the clang C compiler on CentOS.
    # CC       db/c_test.o
    # clang-3.7: error: argument unused during compilation: '-I .'
    # clang-3.7: error: argument unused during compilation: '-I ./include'
    # clang-3.7: error: argument unused during compilation: '-isystem ./third-party/gtest-1.7.0/fused-src'
    # Compiler command failed with exit code 1: ccache compiler -g -W -Wextra -Wall -Wsign-compare -Wshadow -Wno-unused-parameter -Wno-unused-variable -Werror -I. -I./include -DROCKSDB_PLATFORM_POSIX -DROCKSDB_LIB_IO_POSIX -DOS_LINUX -fno-builtin-memcmp -DROCKSDB_FALLOCATE_PRESENT -DZLIB -DROCKSDB_MALLOC_USABLE_SIZE -march=native -isystem ./third-party/gtest-1.7.0/fused-src -c db/c_test.c -o db/c_test.o ; compiler executable: /var/lib/jenkins/workspace/yugabyte-centos-phabricator-clang-debug/thirdparty/installed/bin/clang ; current directory: /var/lib/jenkins/workspace/yugabyte-centos-phabricator-clang-debug/build/debug-clang-dynamic/rocksdb-build
    set(ROCKSDB_C_FLAGS "${ROCKSDB_C_FLAGS} -Qunused-arguments")
  endif()
endif()

if(NOT "${ROCKSDB_CXX_FLAGS}" STREQUAL "")
  list(APPEND BUILD_ROCKSDB_ARGS --cxx-flags "${ROCKSDB_CXX_FLAGS}")
endif()

if(NOT "${ROCKSDB_C_FLAGS}" STREQUAL "")
  list(APPEND BUILD_ROCKSDB_ARGS --c-flags "${ROCKSDB_C_FLAGS}")
endif()

add_custom_target(build_rocksdb_library ALL
  COMMAND ${BUILD_ROCKSDB_ARGS}
  VERBATIM
  COMMENT "Building RocksDB library")

add_custom_target(build_rocksdb_all_targets ALL
  COMMAND ${BUILD_ROCKSDB_ARGS} --targets all
  COMMENT "Building all RocksDB targets, including tests, benchmarks, and tools"
  VERBATIM)

add_dependencies(build_rocksdb_all_targets build_rocksdb_library)

set(ROCKSDB_BUILD_DIR "${YB_BUILD_DIR}/rocksdb-build")

set(ROCKSDB_DEBUG_ONLY_TESTS
  auto_roll_logger_test
  backupable_db_test
  checkpoint_test
  column_family_test
  compact_files_test
  compaction_job_stats_test
  corruption_test
  cuckoo_table_builder_test
  cuckoo_table_db_test
  cuckoo_table_reader_test
  db_block_cache_test
  db_compaction_filter_test
  db_compaction_test
  db_dynamic_level_test
  db_inplace_update_test
  db_iter_test
  db_log_iter_test
  db_properties_test
  db_table_properties_test
  db_tailing_iter_test
  db_test
  db_test2
  db_universal_compaction_test
  db_wal_test
  delete_scheduler_test
  deletefile_test
  fault_injection_test
  listener_test
  memory_test
  plain_table_db_test
  reduce_levels_test
  thread_local_test
  transaction_test
  version_edit_test
  write_callback_test
)

# Add separate targets for tests so we can run in them in CLion. A potential problem with this setup
# is that make may be invoked multiple times.
file(GLOB_RECURSE ROCKSDB_TEST_SOURCES "*_test.cc")
foreach(ROCKSDB_TEST_SRC IN LISTS ROCKSDB_TEST_SOURCES)
  get_filename_component(ROCKSDB_TEST_NAME "${ROCKSDB_TEST_SRC}" NAME_WE)
  get_filename_component(ROCKSDB_TEST_SRC_DIR "${ROCKSDB_TEST_SRC}" DIRECTORY)
  get_filename_component(ROCKSDB_TEST_SRC_DIR_BASENAME "${ROCKSDB_TEST_SRC_DIR}" NAME)
  # The rocksjni directory has some tests with the same name as other tests (write_batch_test.cc),
  # so we skip that directory. We also skip db_sanity_test, which is a command-line tool.
  if(NOT "${ROCKSDB_TEST_SRC_DIR_BASENAME}" STREQUAL "rocksjni" AND
     NOT "${ROCKSDB_TEST_NAME}" STREQUAL "db_sanity_test")
    set(ROCKSDB_TEST_TARGET_NAME "rocksdb_${ROCKSDB_TEST_NAME}")
    add_custom_target("${ROCKSDB_TEST_TARGET_NAME}"
      COMMAND ${BUILD_ROCKSDB_ARGS} --targets "${ROCKSDB_TEST_NAME}" --skip-link-dir-creation
      DEPENDS build_rocksdb_library ${ROCKSDB_TEST_SRC}
      COMMENT "RocksDB test ${ROCKSDB_TEST_NAME}"
      VERBATIM)
    if ("${ROCKSDB_TEST_NAME}" STREQUAL "redis_lists_test")
      set(ROCKSDB_TEST_BINARY_NAME "redis_test")
    else()
      set(ROCKSDB_TEST_BINARY_NAME "${ROCKSDB_TEST_NAME}")
    endif()

    set(ROCKSDB_SHOULD_INCLUDE_TEST true)
    if ("${CMAKE_BUILD_TYPE}" STREQUAL "RELEASE")
      list(FIND ROCKSDB_DEBUG_ONLY_TESTS "${ROCKSDB_TEST_NAME}"
        ROCKSDB_TEST_POSITION_IN_LIST_OF_DEBUG_ONLY_TESTS)
      if (NOT ROCKSDB_TEST_POSITION_IN_LIST_OF_DEBUG_ONLY_TESTS EQUAL -1)
        # This is a debug-only RocksDB test.
        set(ROCKSDB_SHOULD_INCLUDE_TEST false)
      endif()

    endif()

    if (ROCKSDB_SHOULD_INCLUDE_TEST)
      add_test(NAME "${ROCKSDB_TEST_TARGET_NAME}"
               COMMAND "${BUILD_SUPPORT_DIR}/run-test.sh"
               "${ROCKSDB_BUILD_DIR}/${ROCKSDB_TEST_BINARY_NAME}")
    else()
      message("Skipping test ${ROCKSDB_TEST_TARGET_NAME} in release mode")
    endif()
  endif()
endforeach(ROCKSDB_TEST_SRC)

# This library is only here to make CLion navigation work for RocksDB code. It is not being built
# at all due to EXCLUDE_FROM_ALL. The RocksDB build still happens via its Makefile.
add_library(
  rocksdb_dummy_library_for_clion
  EXCLUDE_FROM_ALL
  ${ROCKSDB_SOURCES})

# Allow navigating to RocksDB's internal include files in CLion.
target_include_directories(rocksdb_dummy_library_for_clion PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

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
