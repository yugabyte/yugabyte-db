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

set(YB_PCH_PREFIX master)
set(YB_PCH_DEP_LIBS opid_proto)

YRPC_GENERATE(
  MASTER_YRPC_SRCS MASTER_YRPC_HDRS MASTER_YRPC_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES
      master_admin.proto master_client.proto master_cluster.proto master_dcl.proto
      master_encryption.proto master_heartbeat.proto master_replication.proto
      master_backup.proto master_test.proto
  MESSAGES_PROTO_FILES
      master_ddl.proto
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
  async_flush_tablets_task.cc
  async_rpc_tasks.cc
  async_snapshot_tasks.cc
  async_snapshot_transfer_task.cc
  auto_flags_orchestrator.cc
  master_call_home.cc
  backfill_index.cc
  catalog_manager.cc
  catalog_manager_ext.cc
  catalog_manager_util.cc
  catalog_entity_info.cc
  catalog_manager_bg_tasks.cc
  catalog_loaders.cc
  xcluster_consumer_registry_service.cc
  xcluster_rpc_tasks.cc
  scoped_leader_shared_lock.cc
  cluster_balance.cc
  cluster_balance_util.cc
  encryption_manager.cc
  permissions_manager.cc
  flush_manager.cc
  master.cc
  master_admin_service.cc
  tablet_health_manager.cc
  master_backup_service.cc
  master_client_service.cc
  master_cluster_service.cc
  master_dcl_service.cc
  master_ddl_service.cc
  master_encryption_service.cc
  master_heartbeat_service.cc
  master_replication_service.cc
  master_options.cc
  master_service_base.cc
  master_tablet_service.cc
  master_test_service.cc
  master_tserver.cc
  master-path-handlers.cc
  mini_master.cc
  master_snapshot_coordinator.cc
  restoration_state.cc
  restore_sys_catalog_state.cc
  snapshot_coordinator_context.cc
  snapshot_schedule_state.cc
  snapshot_state.cc
  snapshot_transfer_manager.cc
  state_with_tablets.cc
  sys_catalog.cc
  sys_catalog_initialization.cc
  sys_catalog_writer.cc
  system_tablet.cc
  table_index.cc
  tablet_limits.cc
  tablet_split_manager.cc
  tasks_tracker.cc
  test_async_rpc_manager.cc
  ts_descriptor.cc
  ts_manager.cc
  universe_key_registry_service.cc
  util/yql_vtable_helpers.cc
  xcluster/add_table_to_xcluster_task.cc
  xcluster/xcluster_catalog_entity.cc
  xcluster/xcluster_config.cc
  xcluster/xcluster_consumer_metrics.cc
  xcluster/xcluster_manager.cc
  xcluster/xcluster_outbound_replication_group.cc
  xcluster/xcluster_replication_group.cc
  xcluster/xcluster_safe_time_service.cc
  xrepl_catalog_manager.cc
  yql_aggregates_vtable.cc
  yql_auth_resource_role_permissions_index.cc
  yql_auth_role_permissions_vtable.cc
  yql_auth_roles_vtable.cc
  yql_columns_vtable.cc
  yql_empty_vtable.cc
  yql_functions_vtable.cc
  yql_indexes_vtable.cc
  yql_keyspaces_vtable.cc
  yql_local_vtable.cc
  yql_partitions_vtable.cc
  yql_peers_vtable.cc
  yql_size_estimates_vtable.cc
  yql_tables_vtable.cc
  yql_triggers_vtable.cc
  yql_types_vtable.cc
  yql_views_vtable.cc
  yql_virtual_table.cc
  yql_vtable_iterator.cc
  ysql_backends_manager.cc
  ysql_tablegroup_manager.cc
  ysql_tablespace_manager.cc
  ysql_ddl_handler.cc
  ysql_transaction_ddl.cc)

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
  version_info_proto
  yb_pggate_flags
  yb_pggate
  cdc_util
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

set(MASTER_ERROR_SRCS
  master_error.cc)
ADD_YB_LIBRARY(master_error
  SRCS ${MASTER_ERROR_SRCS}
  DEPS ${MASTER_RPC_LIBS})

set(MASTER_UTIL_SRCS
  master_util.cc)
ADD_YB_LIBRARY(master_util
  SRCS ${MASTER_UTIL_SRCS}
  DEPS master_error ${MASTER_RPC_LIBS})

# A library with common code shared between master tests.
set(MASTER_TEST_COMMON_SRCS
  master-test_base.cc
  master-test-util.cc)
add_library(master_test_common ${MASTER_TEST_COMMON_SRCS})
target_link_libraries(master_test_common
  master ${MASTER_PROTO_LIBS} yb_client ${YB_MIN_TEST_LIBS})

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
ADD_YB_TEST(catalog_manager-test)
ADD_YB_TEST(catalog_manager_mini_cluster_test)
ADD_YB_TEST(cluster_balance_preferred_leader-test)
ADD_YB_TEST(flush_manager-test)
ADD_YB_TEST(load_balancer_mocked-test)
ADD_YB_TEST(master-test)
ADD_YB_TEST(master_xrepl-test)
ADD_YB_TEST(restore_sys_catalog_state_test)
ADD_YB_TEST(stateful_service-test)
ADD_YB_TEST(sys_catalog-test)
ADD_YB_TEST(sys_catalog_xrepl-test)
ADD_YB_TEST(table_index_test)
ADD_YB_TEST(tablet_limits_test)
ADD_YB_TEST(xcluster/xcluster_safe_time_service-test)

# Actual master executable. In LTO mode, can also act as the tablet server if executed through a
# symlink named as the tablet server executable.
add_executable("${YB_MASTER_DYNAMIC_EXE_NAME}" master_main.cc)
set(YB_MASTER_EXE_DEPS
    master
    ${YB_BASE_LIBS})
if(NOT DEFINED YB_LTO_ENABLED)
  message(FATAL_ERROR "YB_LTO_ENABLED is not defined")
endif()
if(YB_LTO_ENABLED)
  # In LTO mode, the master executable can also function as the tablet server.
  list(APPEND YB_MASTER_EXE_DEPS tserver_main_impl)
endif()
target_link_libraries(
  "${YB_MASTER_DYNAMIC_EXE_NAME}"
  ${YB_MASTER_EXE_DEPS})

# Generate master_flags.xml
set(MASTER_FLAGS_METADATA_FILE ${YB_BUILD_ROOT}/master_flags.xml)
add_custom_command(TARGET "${YB_MASTER_DYNAMIC_EXE_NAME}" POST_BUILD
  COMMAND "${BUILD_SUPPORT_DIR}/gen_flags_metadata_wrapper"
    "--program_name" "${YB_MASTER_DYNAMIC_EXE_NAME}"
    "--output_file_path" "${MASTER_FLAGS_METADATA_FILE}"
    "--dynamically_linked_exe_suffix=${YB_DYNAMICALLY_LINKED_EXE_SUFFIX}"
  BYPRODUCTS "${MASTER_FLAGS_METADATA_FILE}")

# Generate auto_flags.json
set(AUTO_FLAGS_FILE ${YB_BUILD_ROOT}/auto_flags.json)
list(APPEND GEN_AUTO_FLAGS_COMMAND "${BUILD_SUPPORT_DIR}/gen_auto_flags"
  "--program_list" "${YB_MASTER_DYNAMIC_EXE_NAME},${YB_TSERVER_DYNAMIC_EXE_NAME}"
  "--dynamically_linked_exe_suffix=${YB_DYNAMICALLY_LINKED_EXE_SUFFIX}"
  "--output_file_path" "${AUTO_FLAGS_FILE}"
  "--previous_auto_flags_file" "${YB_SRC_ROOT}/src/yb/previous_auto_flags.json")
add_custom_target(
  gen_auto_flags_json ALL
  COMMAND ${GEN_AUTO_FLAGS_COMMAND}
  BYPRODUCTS "${AUTO_FLAGS_FILE}"
  DEPENDS "${YB_MASTER_DYNAMIC_EXE_NAME}" "${YB_TSERVER_DYNAMIC_EXE_NAME}")
