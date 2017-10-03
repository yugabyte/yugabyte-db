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

#########################################
# remote_bootstrap_proto
#########################################

KRPC_GENERATE(
  REMOTE_BOOTSTRAP_KRPC_SRCS REMOTE_BOOTSTRAP_KRPC_HDRS REMOTE_BOOTSTRAP_KRPC_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES remote_bootstrap.proto)
set(REMOTE_BOOTSTRAP_KRPC_LIBS
  consensus_proto
  krpc
  protobuf
  rpc_header_proto
  tablet_proto
  wire_protocol_proto)
ADD_EXPORTABLE_LIBRARY(remote_bootstrap_proto
  SRCS ${REMOTE_BOOTSTRAP_KRPC_SRCS}
  DEPS ${REMOTE_BOOTSTRAP_KRPC_LIBS}
  NONLINK_DEPS ${REMOTE_BOOTSTRAP_KRPC_TGTS})

#########################################
# tserver_proto
#########################################

PROTOBUF_GENERATE_CPP(
  TSERVER_PROTO_SRCS TSERVER_PROTO_HDRS TSERVER_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES tserver.proto)
set(TSERVER_PROTO_LIBS
  kudu_common_proto
  consensus_metadata_proto
  tablet_proto
  wire_protocol_proto)
ADD_EXPORTABLE_LIBRARY(tserver_proto
  SRCS ${TSERVER_PROTO_SRCS}
  DEPS ${TSERVER_PROTO_LIBS}
  NONLINK_DEPS ${TSERVER_PROTO_TGTS})

#########################################
# tserver_admin_proto
#########################################

KRPC_GENERATE(
  TSERVER_ADMIN_KRPC_SRCS TSERVER_ADMIN_KRPC_HDRS TSERVER_ADMIN_KRPC_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES tserver_admin.proto)
set(TSERVER_ADMIN_KRPC_LIBS
  krpc
  protobuf
  rpc_header_proto
  tserver_proto
  wire_protocol_proto)
ADD_EXPORTABLE_LIBRARY(tserver_admin_proto
  SRCS ${TSERVER_ADMIN_KRPC_SRCS}
  DEPS ${TSERVER_ADMIN_KRPC_LIBS}
  NONLINK_DEPS ${TSERVER_ADMIN_KRPC_TGTS})

#########################################
# tserver_service_proto
#########################################

KRPC_GENERATE(
  TSERVER_KRPC_SRCS TSERVER_KRPC_HDRS TSERVER_KRPC_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES tserver_service.proto)
set(TSERVER_KRPC_LIBS
  krpc
  kudu_common_proto
  protobuf
  remote_bootstrap_proto
  rpc_header_proto
  tserver_proto
  wire_protocol_proto)
ADD_EXPORTABLE_LIBRARY(tserver_service_proto
  SRCS ${TSERVER_KRPC_SRCS}
  DEPS ${TSERVER_KRPC_LIBS}
  NONLINK_DEPS ${TSERVER_KRPC_TGTS})

#########################################
# tserver
#########################################

set(TSERVER_SRCS
  heartbeater.cc
  mini_tablet_server.cc
  remote_bootstrap_client.cc
  remote_bootstrap_service.cc
  remote_bootstrap_session.cc
  scanner_metrics.cc
  scanners.cc
  tablet_server.cc
  tablet_server_options.cc
  tablet_service.cc
  ts_tablet_manager.cc
  tserver-path-handlers.cc
)

add_library(tserver ${TSERVER_SRCS})
target_link_libraries(tserver
  protobuf
  tserver_proto
  tserver_admin_proto
  tserver_service_proto
  remote_bootstrap_proto
  master_rpc
  master_proto
  consensus_proto
  log_proto
  log
  consensus
  krpc
  server_common
  server_process
  tablet)

#########################################
# kudu-tserver
#########################################

add_executable(kudu-tserver tablet_server_main.cc)
target_link_libraries(kudu-tserver
  tserver
  ${KUDU_BASE_LIBS})

#########################################
# tserver_test_util
#########################################

set(TSERVER_TEST_UTIL_SRCS
  tablet_server_test_util.cc
)

add_library(tserver_test_util ${TSERVER_TEST_UTIL_SRCS})
target_link_libraries(tserver_test_util
  tserver)

#########################################
# tserver tests
#########################################

set(KUDU_TEST_LINK_LIBS
  tserver
  tserver_test_util
  ${KUDU_MIN_TEST_LIBS})
ADD_KUDU_TEST(remote_bootstrap_client-test)
ADD_KUDU_TEST(remote_bootstrap_session-test)
ADD_KUDU_TEST(remote_bootstrap_service-test)
ADD_KUDU_TEST(tablet_server-test)
ADD_KUDU_TEST(tablet_server-stress-test RUN_SERIAL true)
ADD_KUDU_TEST(scanners-test)
ADD_KUDU_TEST(ts_tablet_manager-test)
