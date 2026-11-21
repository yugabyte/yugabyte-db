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
# The following only applies to changes made to this file as part of YugabyteDB development.
#
# Portions Copyright (c) YugabyteDB, Inc.
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

set(YB_PCH_PREFIX master)

YRPC_GENERATE(
  MASTER_YRPC_SRCS MASTER_YRPC_HDRS MASTER_YRPC_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES
      master_cluster.proto master_dcl.proto
      master_encryption.proto master_heartbeat.proto master_replication.proto
      master_backup.proto master_test.proto master_ysql_lease.proto master_rust_peer.proto
  MESSAGES_PROTO_FILES
      master_ddl.proto master_client.proto master_admin.proto
  NO_SERVICE_MESSAGES_PROTO_FILES
      catalog_entity_info.proto master_types.proto)
set(MASTER_YRPC_LIBS
  consensus_metadata_proto
  yrpc
  protobuf
  encryption_proto
  rpc_base_proto
  rpc_header_proto
  tablet_proto
  tserver_proto
  cdc_consumer_proto
  xcluster_producer_proto
  consensus_proto)
ADD_YB_LIBRARY(master_proto
  SRCS ${MASTER_YRPC_SRCS}
  DEPS ${MASTER_YRPC_LIBS}
  NONLINK_DEPS ${MASTER_YRPC_TGTS})

set(MASTER_PROTO_LIBS
  master_proto)

include_directories(${YB_BUILD_ROOT}/postgres/include/server)

set(MASTER_SRCS
  catalog_entity_base.cc
  catalog_entity_info.cc
  catalog_loaders.cc
  catalog_manager_util.cc
  embedded_master_if.cc
  encryption_manager.cc
  master-path-handlers.cc
  master_auto_flags_manager.cc
  master_call_home.cc
  master_flag_defaults.cc
  master_flags_balancer.cc
  master_flags_catalog.cc
  master_flags_cdc.cc
  master_flags_server.cc
  master_flags_snapshots.cc
  master_flags_xcluster.cc
  master_flags_ysql.cc
  master_metrics.cc
  master_options.cc
  master_snapshot_coordinator.cc
  master_tablet_service.cc
  master_tserver.cc
  master_types.cc
  mini_master.cc
  restoration_state.cc
  restore_sys_catalog_state.cc
  snapshot_coordinator_context.cc
  snapshot_schedule_state.cc
  snapshot_state.cc
  state_with_tablets.cc
  sys_catalog.cc
  sys_catalog_restore.cc
  sys_catalog_initialization.cc
  sys_catalog_writer.cc
  system_tablet.cc
  system_tables.cc
  table_ddl_util.cc
  tasks_tracker.cc
  universe_key_registry_service.cc
  util/yql_vtable_helpers.cc
  xcluster_consumer_registry_service.cc
  yql_virtual_table.cc
  yql_vtable_iterator.cc
  ysql/ysql_initdb_major_upgrade_handler.cc
  ysql_ddl_verification_task.cc
  ysql_sequence_util.cc
)

set(MASTER_DEPS
  yb_common
  tablet
  server_common
  server_process
  yrpc
  gutil
  yb_ash
  yb_util
  tserver
  tserver_service_proto
  ${MASTER_PROTO_LIBS}
  rpc_header_proto
  master_util
  yb_pggate_flags
  yb_pggate
  cdc_util
  auto_flags_manager_common
)

ADD_YB_LIBRARY(master
  SRCS ${MASTER_SRCS}
  DEPS ${MASTER_DEPS}
  NONLINK_DEPS postgres)

set(MASTER_RPC_SRCS
  master_rpc.cc)
set(MASTER_RPC_LIBS
  yb_common
  yrpc
  gutil
  yb_util
  ${MASTER_PROTO_LIBS}
  rpc_header_proto)
ADD_YB_LIBRARY(master_rpc
  SRCS ${MASTER_RPC_SRCS}
  DEPS ${MASTER_RPC_LIBS})

set(MASTER_UTIL_SRCS
  master_util.cc
  catalog_entity_parser.cc)
ADD_YB_LIBRARY(master_util
  SRCS ${MASTER_UTIL_SRCS}
  DEPS ${MASTER_RPC_LIBS})

# A library with common code shared between master tests. MiniMaster starts the embedded Rust
# master, whose factory yb_master_rs_embedded registers.
set(MASTER_TEST_COMMON_SRCS
  master-test-util.cc
  master-test_base.cc
  master_cluster_client.cc
  master_ddl_client.cc
  master_ysql_lease_client.cc)
add_library(master_test_common ${MASTER_TEST_COMMON_SRCS})
target_link_libraries(master_test_common
  master ${MASTER_PROTO_LIBS} yb_client yb_master_rs_embedded ${YB_MIN_TEST_LIBS})

# Tests
set(YB_TEST_LINK_LIBS master
  ${MASTER_PROTO_LIBS}
  integration-tests
  master_test_common
  cql_test_util
  yb_client
  yb_tools_util
  ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(catalog_entity_info-test)
ADD_YB_TEST(flush_manager-test)
ADD_YB_TEST(master-test)
ADD_YB_TEST(master_cluster_test)
ADD_YB_TEST(master_tablet_service_test)
ADD_YB_TEST(master_xrepl-test)
ADD_YB_TEST(restore_sys_catalog_state_test)
ADD_YB_TEST(stateful_service-test)
ADD_YB_TEST(sys_catalog-test)
ADD_YB_TEST(sys_catalog_xrepl-test)

