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

KRPC_GENERATE(
  MASTER_KRPC_SRCS MASTER_KRPC_HDRS MASTER_KRPC_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES master.proto)
set(MASTER_KRPC_LIBS
  consensus_metadata_proto
  krpc
  protobuf
  rpc_header_proto
  tablet_proto
  wire_protocol_proto)
ADD_EXPORTABLE_LIBRARY(master_proto
  SRCS ${MASTER_KRPC_SRCS}
  DEPS ${MASTER_KRPC_LIBS}
  NONLINK_DEPS ${MASTER_KRPC_TGTS})

set(MASTER_SRCS
  catalog_manager.cc
  master.cc
  master_options.cc
  master_service.cc
  master-path-handlers.cc
  mini_master.cc
  sys_catalog.cc
  ts_descriptor.cc
  ts_manager.cc
)

add_library(master ${MASTER_SRCS})
target_link_libraries(master
  kudu_common
  tablet
  server_common
  server_process
  krpc
  gutil
  kudu_util
  tserver
  tserver_service_proto
  master_proto
  rpc_header_proto)

set(MASTER_RPC_SRCS
  master_rpc.cc)
set(MASTER_RPC_LIBS
  kudu_common
  krpc
  gutil
  kudu_util
  master_proto
  rpc_header_proto)
ADD_EXPORTABLE_LIBRARY(master_rpc
  SRCS ${MASTER_RPC_SRCS}
  DEPS ${MASTER_RPC_LIBS})

# Tests
set(KUDU_TEST_LINK_LIBS master master_proto kudu_client ${KUDU_MIN_TEST_LIBS})
ADD_KUDU_TEST(catalog_manager-test)
ADD_KUDU_TEST(master-test RESOURCE_LOCK "master-web-port")
ADD_KUDU_TEST(sys_catalog-test RESOURCE_LOCK "master-web-port")

# Actual master executable
add_executable(kudu-master master_main.cc)
target_link_libraries(kudu-master
  master
  ${KUDU_BASE_LIBS})
