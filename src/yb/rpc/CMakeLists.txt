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
#
# The following only applies to changes made to this file as part of YugaByte development.
#
# Portions Copyright (c) YugaByte, Inc.
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

set(YB_PCH_PREFIX rpc)
set(YB_PCH_DEP_LIBS cds gmock gtest gutil libev protoc snappy yb_test_util yb_util)

ADD_YB_LIBRARY(
    yrpc_base
    SRCS lightweight_message.cc
    DEPS yb_util)

#### Global header protobufs
YRPC_GENERATE(
  RPC_HEADER_PROTO_SRCS RPC_HEADER_PROTO_HDRS RPC_HEADER_PROTO_TGTS
  MESSAGES TRUE
  SERVICE FALSE
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES rpc_header.proto)
ADD_YB_LIBRARY(rpc_header_proto
  SRCS ${RPC_HEADER_PROTO_SRCS}
  DEPS protobuf yrpc_base
  NONLINK_DEPS ${RPC_HEADER_PROTO_TGTS})

PROTOBUF_GENERATE_CPP(
  RPC_INTROSPECTION_PROTO_SRCS RPC_INTROSPECTION_PROTO_HDRS RPC_INTROSPECTION_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES rpc_introspection.proto)
set(RPC_INTROSPECTION_PROTO_LIBS
  yb_common_proto
  rpc_header_proto
  protobuf)
ADD_YB_LIBRARY(rpc_introspection_proto
  SRCS ${RPC_INTROSPECTION_PROTO_SRCS}
  DEPS ${RPC_INTROSPECTION_PROTO_LIBS}
  NONLINK_DEPS ${RPC_INTROSPECTION_PROTO_TGTS})

### RPC library
set(YRPC_SRCS
    acceptor.cc
    binary_call_parser.cc
    call_data.cc
    circular_read_buffer.cc
    compressed_stream.cc
    connection.cc
    connection_context.cc
    growable_buffer.cc
    inbound_call.cc
    io_thread_pool.cc
    messenger.cc
    network_error.cc
    outbound_call.cc
    local_call.cc
    rpc_call.cc
    periodic.cc
    poller.cc
    proxy.cc
    reactor.cc
    reactor_task.cc
    delayed_task.cc
    refined_stream.cc
    remote_method.cc
    rpc.cc
    rpc_context.cc
    rpc_controller.cc
    rpc_metrics.cc
    rpc_with_call_id.cc
    rpc_with_queue.cc
    rpc_util.cc
    scheduler.cc
    secure.cc
    secure_stream.cc
    serialization.cc
    service_if.cc
    service_pool.cc
    sidecars.cc
    stream.cc
    strand.cc
    tcp_stream.cc
    thread_pool.cc
    yb_rpc.cc)

set(YRPC_LIBS
  gutil
  libev
  lz4
  rpc_header_proto
  rpc_introspection_proto
  snappy
  yb_ash
  yb_common
  yb_util
  ${OPENSSL_CRYPTO_LIBRARY}
  ${OPENSSL_SSL_LIBRARY})

ADD_YB_LIBRARY(yrpc
  SRCS ${YRPC_SRCS}
  DEPS ${YRPC_LIBS})

ADD_YB_TEST_LIBRARY(rpc_test_util SRCS rpc_test_util.cc DEPS yrpc)

#### RPC test
PROTOBUF_GENERATE_CPP(
  RPC_BASE_SRCS RPC_BASE_HDRS RPC_BASE_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES lightweight_message.proto service.proto)
add_library(rpc_base_proto ${RPC_BASE_SRCS} ${RPC_BASE_HDRS})
target_link_libraries(rpc_base_proto protobuf)

YRPC_GENERATE(
  RPC_TEST_DIFF_PACKAGE_SRCS RPC_TEST_DIFF_PACKAGE_HDRS RPC_TEST_DIFF_PACKAGE_TGTS
  MESSAGES TRUE
  SERVICE FALSE
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES rtest_diff_package.proto)
ADD_YB_LIBRARY(
    rtest_diff_package_proto
    SRCS ${RPC_TEST_DIFF_PACKAGE_SRCS} ${RPC_TEST_DIFF_PACKAGE_HDRS})
target_link_libraries(rtest_diff_package_proto rpc_header_proto)

YRPC_GENERATE(
  RTEST_YRPC_SRCS RTEST_YRPC_HDRS RTEST_YRPC_TGTS
  MESSAGES TRUE
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES rtest.proto)
ADD_YB_LIBRARY(
  rtest_yrpc
  SRCS rpc-test-base.cc ${RTEST_YRPC_SRCS} ${RTEST_YRPC_HDRS}
  DEPS yb_test_util any_yrpc yrpc rpc_header_proto rpc_test_util rtest_diff_package_proto
       rpc_base_proto)

YRPC_GENERATE(
  ANY_YRPC_SRCS ANY_YRPC_HDRS ANY_YRPC_TGTS
  MESSAGES TRUE
  SERVICE FALSE
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES any.proto)
add_library(any_yrpc ${ANY_YRPC_SRCS} ${ANY_YRPC_HDRS})
target_link_libraries(any_yrpc yrpc rpc_base_proto)

# Tests
set(YB_TEST_LINK_LIBS rtest_yrpc yrpc rpc_test_util any_yrpc ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(growable_buffer-test)
ADD_YB_TEST(lwproto-test)
ADD_YB_TEST(mt-rpc-test RUN_SERIAL true)
ADD_YB_TEST(periodic-test)
ADD_YB_TEST(reactor-test)
ADD_YB_TEST(rpc-bench RUN_SERIAL true)
ADD_YB_TEST(rpc-test)
ADD_YB_TEST(rpc_stub-test RUN_SERIAL true)
ADD_YB_TEST(scheduler-test)
ADD_YB_TEST(thread_pool-test)
