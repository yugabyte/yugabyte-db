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

YRPC_GENERATE(
  MASTER_YRPC_SRCS MASTER_YRPC_HDRS MASTER_YRPC_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES master.proto)
set(MASTER_YRPC_LIBS
  consensus_metadata_proto
  yrpc
  protobuf
  rpc_header_proto
  tablet_proto
  tserver_proto
  wire_protocol_proto)
ADD_EXPORTABLE_LIBRARY(master_proto
  SRCS ${MASTER_YRPC_SRCS}
  DEPS ${MASTER_YRPC_LIBS}
  NONLINK_DEPS ${MASTER_YRPC_TGTS})

set(MASTER_SRCS
  system_tablet.cc
  system_tables_handler.cc
  yql_virtual_table.cc
  yql_vtable_iterator.cc
  yql_aggregates_vtable.cc
  yql_columns_vtable.cc
  yql_empty_vtable.cc
  yql_functions_vtable.cc
  yql_indexes_vtable.cc
  yql_keyspaces_vtable.cc
  yql_peers_vtable.cc
  yql_tables_vtable.cc
  yql_triggers_vtable.cc
  yql_types_vtable.cc
  yql_views_vtable.cc
  catalog_manager.cc
  cluster_balance.cc
  master.cc
  master_options.cc
  master_service.cc
  master-path-handlers.cc
  mini_master.cc
  sys_catalog.cc
  ts_descriptor.cc
  ts_manager.cc
  master_tablet_service.cc
  master_tserver.cc
)

add_library(master ${MASTER_SRCS})
target_link_libraries(master
  yb_common
  tablet
  server_common
  server_process
  yrpc
  gutil
  yb_util
  tserver
  tserver_service_proto
  master_proto
  rpc_header_proto
  master_util)

set(MASTER_RPC_SRCS
  master_rpc.cc)
set(MASTER_RPC_LIBS
  yb_common
  yrpc
  gutil
  yb_util
  master_proto
  rpc_header_proto)
ADD_EXPORTABLE_LIBRARY(master_rpc
  SRCS ${MASTER_RPC_SRCS}
  DEPS ${MASTER_RPC_LIBS})

set(MASTER_UTIL_SRCS
  master_util.cc)
ADD_EXPORTABLE_LIBRARY(master_util
  SRCS ${MASTER_UTIL_SRCS}
  DEPS ${MASTER_RPC_LIBS})

# Tests
set(YB_TEST_LINK_LIBS master master_proto yb_client ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(catalog_manager-test)
ADD_YB_TEST(master-test RESOURCE_LOCK "master-web-port")
ADD_YB_TEST(sys_catalog-test RESOURCE_LOCK "master-web-port")

# Actual master executable
add_executable(yb-master master_main.cc)
target_link_libraries(yb-master
  master
  ${YB_BASE_LIBS})
