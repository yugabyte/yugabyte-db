# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

#######################################
# histogram_proto
#######################################

PROTOBUF_GENERATE_CPP(
  HISTOGRAM_PROTO_SRCS HISTOGRAM_PROTO_HDRS HISTOGRAM_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES histogram.proto)
ADD_EXPORTABLE_LIBRARY(histogram_proto
  SRCS ${HISTOGRAM_PROTO_SRCS}
  DEPS protobuf
  NONLINK_DEPS ${HISTOGRAM_PROTO_TGTS})

#######################################
# pb_util_proto
#######################################

PROTOBUF_GENERATE_CPP(
  PB_UTIL_PROTO_SRCS PB_UTIL_PROTO_HDRS PB_UTIL_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES pb_util.proto)
ADD_EXPORTABLE_LIBRARY(pb_util_proto
  SRCS ${PB_UTIL_PROTO_SRCS}
  DEPS protobuf
  NONLINK_DEPS ${PB_UTIL_PROTO_TGTS})

#######################################
# version_info_proto
#######################################

PROTOBUF_GENERATE_CPP(
  VERSION_INFO_PROTO_SRCS VERSION_INFO_PROTO_HDRS VERSION_INFO_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES version_info.proto)
ADD_EXPORTABLE_LIBRARY(version_info_proto
  SRCS ${VERSION_INFO_PROTO_SRCS}
  DEPS protobuf
  NONLINK_DEPS ${VERSION_INFO_PROTO_TGTS})

############################################################
# Version stamp
############################################################

# Unlike CMAKE_CURRENT_BINARY_DIR, CMAKE_BINARY_DIR is always the root of
# the build directory.
set(VERSION_STAMP_FILE ${CMAKE_BINARY_DIR}/src/kudu/generated/version_defines.h)

list(APPEND GEN_VERSION_INFO_COMMAND "${BUILD_SUPPORT_DIR}/gen_version_info.py")
list(APPEND GEN_VERSION_INFO_COMMAND "--version=${KUDU_VERSION_NUMBER}")
list(APPEND GEN_VERSION_INFO_COMMAND "--build-type=${CMAKE_BUILD_TYPE}")
if(KUDU_GIT_HASH)
  message(STATUS "Provided git hash: ${KUDU_GIT_HASH}")
  list(APPEND GEN_VERSION_INFO_COMMAND "--git-hash=${KUDU_GIT_HASH}")
endif()
list(APPEND GEN_VERSION_INFO_COMMAND "${VERSION_STAMP_FILE}")
add_custom_target(gen_version_info
    COMMAND ${GEN_VERSION_INFO_COMMAND}
    BYPRODUCTS "${VERSION_STAMP_FILE}")

#######################################
# kudu_util
#######################################

if (APPLE)
  set(SEMAPHORE_CC "semaphore_macosx.cc")
else ()
  set(SEMAPHORE_CC "semaphore.cc")
endif()

set(UTIL_SRCS
  atomic.cc
  bitmap.cc
  bloom_filter.cc
  bitmap.cc
  cache.cc
  cache_metrics.cc
  coding.cc
  condition_variable.cc
  crc.cc
  debug-util.cc
  debug/trace_event_impl.cc
  debug/trace_event_impl_constants.cc
  debug/trace_event_synthetic_delay.cc
  env.cc env_posix.cc env_util.cc
  errno.cc
  faststring.cc
  failure_detector.cc
  fault_injection.cc
  flags.cc
  flag_tags.cc
  group_varint.cc
  pstack_watcher.cc
  hdr_histogram.cc
  hexdump.cc
  init.cc
  jsonreader.cc
  jsonwriter.cc
  kernel_stack_watchdog.cc
  locks.cc
  logging.cc
  malloc.cc
  memcmpable_varint.cc
  memory/arena.cc memory/memory.cc
  memenv/memenv.cc
  mem_tracker.cc
  metrics.cc
  monotime.cc
  mutex.cc
  net/dns_resolver.cc
  net/net_util.cc
  net/sockaddr.cc
  net/socket.cc
  oid_generator.cc
  once.cc
  os-util.cc
  path_util.cc
  pb_util.cc
  pb_util-internal.cc
  random_util.cc
  resettable_heartbeater.cc
  rolling_log.cc
  rwc_lock.cc
  ${SEMAPHORE_CC}
  slice.cc
  spinlock_profiling.cc
  status.cc
  status_callback.cc
  string_case.cc
  striped64.cc
  subprocess.cc
  sync_point.cc
  test_graph.cc
  thread.cc
  threadlocal.cc
  threadpool.cc
  thread_restrictions.cc
  trace.cc
  user.cc
  url-coding.cc
  version_info.cc
)

if(NOT APPLE)
  set(UTIL_SRCS
    ${UTIL_SRCS}
    nvm_cache.cc)
endif()

set(UTIL_LIBS
  boost_system
  boost_thread
  crcutil
  gflags
  glog
  gutil
  histogram_proto
  pb_util_proto
  protobuf
  version_info_proto
  zlib)

if(NOT APPLE)
  set(UTIL_LIBS
    ${UTIL_LIBS}
    rt
    vmem)
endif()

# We use MallocExtension, but not in the exported version of the library.
set(EXPORTED_UTIL_LIBS ${UTIL_LIBS})
if(${KUDU_TCMALLOC_AVAILABLE})
  list(APPEND UTIL_LIBS tcmalloc)
endif()

ADD_EXPORTABLE_LIBRARY(kudu_util
  SRCS ${UTIL_SRCS}
  DEPS ${UTIL_LIBS}
  EXPORTED_DEPS ${EXPORTED_UTIL_LIBS})

add_dependencies(kudu_util gen_version_info)

#######################################
# kudu_test_util
#######################################

add_library(kudu_test_util
  test_util.cc
  curl_util.cc)
target_link_libraries(kudu_test_util
  ${CURL_LIBRARIES}
  gflags
  glog
  gmock
  kudu_util)

if(NOT APPLE)
target_link_libraries(kudu_test_util
  vmem)
endif()

#######################################
# kudu_test_main
#######################################

add_library(kudu_test_main
  test_main.cc)
target_link_libraries(kudu_test_main
  gflags
  glog
  gmock
  kudu_util
  kudu_test_util
  dl)

if(NOT APPLE)
  target_link_libraries(kudu_test_main
    rt)
endif()

#######################################
# protoc-gen-insertions
#######################################

add_executable(protoc-gen-insertions protoc-gen-insertions.cc)
target_link_libraries(protoc-gen-insertions gutil protobuf protoc ${KUDU_BASE_LIBS})

#######################################
# Unit tests
#######################################

set(KUDU_TEST_LINK_LIBS kudu_util gutil ${KUDU_MIN_TEST_LIBS})
ADD_KUDU_TEST(atomic-test)
ADD_KUDU_TEST(bit-util-test)
ADD_KUDU_TEST(bitmap-test)
ADD_KUDU_TEST(blocking_queue-test)
ADD_KUDU_TEST(bloom_filter-test)
ADD_KUDU_TEST(cache-test)
ADD_KUDU_TEST(callback_bind-test)
ADD_KUDU_TEST(countdown_latch-test)
ADD_KUDU_TEST(crc-test RUN_SERIAL true) # has a benchmark
ADD_KUDU_TEST(debug-util-test)
ADD_KUDU_TEST(env-test LABELS no_tsan)
ADD_KUDU_TEST(errno-test)
ADD_KUDU_TEST(failure_detector-test)
ADD_KUDU_TEST(flag_tags-test)
ADD_KUDU_TEST(group_varint-test)
ADD_KUDU_TEST(hash_util-test)
ADD_KUDU_TEST(hdr_histogram-test)
ADD_KUDU_TEST(inline_slice-test)
ADD_KUDU_TEST(interval_tree-test)
ADD_KUDU_TEST(jsonreader-test)
ADD_KUDU_TEST(knapsack_solver-test)
ADD_KUDU_TEST(logging-test)
ADD_KUDU_TEST(map-util-test)
ADD_KUDU_TEST(memcmpable_varint-test LABELS no_tsan)
ADD_KUDU_TEST(memenv/memenv-test)
ADD_KUDU_TEST(memory/arena-test)
ADD_KUDU_TEST(mem_tracker-test)
ADD_KUDU_TEST(metrics-test)
ADD_KUDU_TEST(monotime-test)
ADD_KUDU_TEST(mt-hdr_histogram-test RUN_SERIAL true)
ADD_KUDU_TEST(mt-metrics-test RUN_SERIAL true)
ADD_KUDU_TEST(mt-threadlocal-test RUN_SERIAL true)
ADD_KUDU_TEST(net/dns_resolver-test)
ADD_KUDU_TEST(net/net_util-test)
ADD_KUDU_TEST(object_pool-test)
ADD_KUDU_TEST(once-test)
ADD_KUDU_TEST(os-util-test)
ADD_KUDU_TEST(path_util-test)
ADD_KUDU_TEST(pstack_watcher-test)
ADD_KUDU_TEST(random-test)
ADD_KUDU_TEST(random_util-test)
ADD_KUDU_TEST(resettable_heartbeater-test)
ADD_KUDU_TEST(rle-test)
ADD_KUDU_TEST(rolling_log-test)
ADD_KUDU_TEST(rw_semaphore-test)
ADD_KUDU_TEST(rwc_lock-test)
ADD_KUDU_TEST(safe_math-test)
ADD_KUDU_TEST(slice-test)
ADD_KUDU_TEST(spinlock_profiling-test)
ADD_KUDU_TEST(stack_watchdog-test)
ADD_KUDU_TEST(status-test)
ADD_KUDU_TEST(string_case-test)
ADD_KUDU_TEST(striped64-test)
ADD_KUDU_TEST(subprocess-test)
ADD_KUDU_TEST(sync_point-test)
ADD_KUDU_TEST(thread-test)
ADD_KUDU_TEST(threadpool-test)
ADD_KUDU_TEST(trace-test)
ADD_KUDU_TEST(url-coding-test)
ADD_KUDU_TEST(user-test)

#######################################
# jsonwriter_test_proto
#######################################

PROTOBUF_GENERATE_CPP(
  JSONWRITER_TEST_PROTO_SRCS JSONWRITER_TEST_PROTO_HDRS JSONWRITER_TEST_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES jsonwriter_test.proto)
add_library(jsonwriter_test_proto ${JSONWRITER_TEST_PROTO_SRCS} ${JSONWRITER_TEST_PROTO_HDRS})
target_link_libraries(jsonwriter_test_proto
  protobuf)

#######################################
# jsonwriter-test
#######################################

ADD_KUDU_TEST(jsonwriter-test)
if(NOT "${NO_TESTS}")
  target_link_libraries(jsonwriter-test
    jsonwriter_test_proto)
endif()

#######################################
# proto_container_test_proto
#######################################

PROTOBUF_GENERATE_CPP(
  PROTO_CONTAINER_TEST_PROTO_SRCS PROTO_CONTAINER_TEST_PROTO_HDRS PROTO_CONTAINER_TEST_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES
    proto_container_test.proto
    proto_container_test2.proto
    proto_container_test3.proto)
add_library(proto_container_test_proto
  ${PROTO_CONTAINER_TEST_PROTO_SRCS}
  ${PROTO_CONTAINER_TEST_PROTO_HDRS})
target_link_libraries(proto_container_test_proto
  protobuf)

#######################################
# pb_util-test
#######################################

ADD_KUDU_TEST(pb_util-test)
if(NOT "${NO_TESTS}")
  target_link_libraries(pb_util-test
    proto_container_test_proto)
endif()
