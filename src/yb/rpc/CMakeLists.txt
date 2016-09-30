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

#### Global header protobufs
PROTOBUF_GENERATE_CPP(
  RPC_HEADER_PROTO_SRCS RPC_HEADER_PROTO_HDRS RPC_HEADER_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES rpc_header.proto)
ADD_EXPORTABLE_LIBRARY(rpc_header_proto
  SRCS ${RPC_HEADER_PROTO_SRCS}
  DEPS protobuf
  NONLINK_DEPS ${RPC_HEADER_PROTO_TGTS})

PROTOBUF_GENERATE_CPP(
  RPC_INTROSPECTION_PROTO_SRCS RPC_INTROSPECTION_PROTO_HDRS RPC_INTROSPECTION_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES rpc_introspection.proto)
set(RPC_INTROSPECTION_PROTO_LIBS
  rpc_header_proto
  protobuf)
ADD_EXPORTABLE_LIBRARY(rpc_introspection_proto
  SRCS ${RPC_INTROSPECTION_PROTO_SRCS}
  DEPS ${RPC_INTROSPECTION_PROTO_LIBS}
  NONLINK_DEPS ${RPC_INTROSPECTION_PROTO_TGTS})

### RPC library
set(YRPC_SRCS
    acceptor_pool.cc
    auth_store.cc
    blocking_ops.cc
    outbound_call.cc
    connection.cc
    constants.cc
    inbound_call.cc
    messenger.cc
    negotiation.cc
    proxy.cc
    reactor.cc
    redis_encoding.cc
    remote_method.cc
    rpc.cc
    rpc_context.cc
    rpc_controller.cc
    sasl_common.cc
    sasl_client.cc
    sasl_helper.cc
    sasl_server.cc
    serialization.cc
    service_if.cc
    service_pool.cc
    transfer.cc
)

set(YRPC_LIBS
  redis_protocol_proto
  rpc_header_proto
  rpc_introspection_proto
  yb_util
  gutil
  libev
  cyrus_sasl)

ADD_EXPORTABLE_LIBRARY(yrpc
  SRCS ${YRPC_SRCS}
  DEPS ${YRPC_LIBS})

### RPC generator tool
add_executable(protoc-gen-yrpc protoc-gen-yrpc.cc)
target_link_libraries(protoc-gen-yrpc
    ${YB_BASE_LIBS}
    protoc
    protobuf
    gutil
    yb_util)

#### RPC test
PROTOBUF_GENERATE_CPP(
  RPC_TEST_DIFF_PACKAGE_SRCS RPC_TEST_DIFF_PACKAGE_HDRS RPC_TEST_DIFF_PACKAGE_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES rtest_diff_package.proto)
add_library(rtest_diff_package_proto ${RPC_TEST_DIFF_PACKAGE_SRCS} ${RPC_TEST_DIFF_PACKAGE_HDRS})
target_link_libraries(rtest_diff_package_proto rpc_header_proto)

YRPC_GENERATE(
  RTEST_YRPC_SRCS RTEST_YRPC_HDRS RTEST_YRPC_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES rtest.proto)
add_library(rtest_yrpc ${RTEST_YRPC_SRCS} ${RTEST_YRPC_HDRS})
target_link_libraries(rtest_yrpc
  yrpc
  rpc_header_proto
  rtest_diff_package_proto)

# Tests
set(YB_TEST_LINK_LIBS rtest_yrpc yrpc ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(mt-rpc-test RUN_SERIAL true)
ADD_YB_TEST(reactor-test)
ADD_YB_TEST(rpc-bench RUN_SERIAL true)
ADD_YB_TEST(rpc-test)
ADD_YB_TEST(rpc_stub-test)
ADD_YB_TEST(sasl_rpc-test)
