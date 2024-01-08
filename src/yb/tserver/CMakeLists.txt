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

set(YB_PCH_PREFIX tserver)
set(YB_PCH_DEP_LIBS opid_proto yb_test_util)

#########################################
# tserver_proto
#########################################

YRPC_GENERATE(
  TSERVER_PROTO_SRCS TSERVER_PROTO_HDRS TSERVER_PROTO_TGTS
  MESSAGES TRUE
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  NO_SERVICE_PROTO_FILES tserver.proto tserver_types.proto)
set(TSERVER_PROTO_LIBS
  yb_common_proto
  consensus_metadata_proto
  tablet_proto
  docdb_proto)
ADD_YB_LIBRARY(tserver_proto
  SRCS ${TSERVER_PROTO_SRCS}
  DEPS ${TSERVER_PROTO_LIBS}
  NONLINK_DEPS ${TSERVER_PROTO_TGTS})

#########################################
# backup_proto
#########################################

YRPC_GENERATE(
  BACKUP_YRPC_SRCS BACKUP_YRPC_HDRS BACKUP_YRPC_TGTS
  MESSAGES TRUE
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES backup.proto)
set(BACKUP_YRPC_LIBS
  any_yrpc
  yrpc
  tserver_proto)
ADD_YB_LIBRARY(backup_proto
  SRCS ${BACKUP_YRPC_SRCS}
  DEPS ${BACKUP_YRPC_LIBS}
  NONLINK_DEPS ${BACKUP_YRPC_TGTS})

#########################################
# remote_bootstrap_proto
#########################################

YRPC_GENERATE(
  REMOTE_BOOTSTRAP_YRPC_SRCS REMOTE_BOOTSTRAP_YRPC_HDRS REMOTE_BOOTSTRAP_YRPC_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES remote_bootstrap.proto)
set(REMOTE_BOOTSTRAP_YRPC_LIBS
  consensus_proto
  yrpc
  protobuf
  rpc_header_proto
  tablet_proto)
ADD_YB_LIBRARY(remote_bootstrap_proto
  SRCS ${REMOTE_BOOTSTRAP_YRPC_SRCS}
  DEPS ${REMOTE_BOOTSTRAP_YRPC_LIBS}
  NONLINK_DEPS ${REMOTE_BOOTSTRAP_YRPC_TGTS})

#########################################
# pg_client_proto
#########################################

YRPC_GENERATE(
  PG_CLIENT_YRPC_SRCS PG_CLIENT_YRPC_HDRS PG_CLIENT_YRPC_TGTS
  MESSAGES TRUE
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES pg_client.proto)
set(PG_CLIENT_YRPC_LIBS
  master_proto
  yrpc)
ADD_YB_LIBRARY(pg_client_proto
  SRCS ${PG_CLIENT_YRPC_SRCS}
  DEPS ${PG_CLIENT_YRPC_LIBS}
  NONLINK_DEPS ${PG_CLIENT_YRPC_TGTS})

#########################################
# tserver_admin_proto
#########################################

YRPC_GENERATE(
  TSERVER_ADMIN_YRPC_SRCS TSERVER_ADMIN_YRPC_HDRS TSERVER_ADMIN_YRPC_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES tserver_admin.proto)
set(TSERVER_ADMIN_YRPC_LIBS
  yrpc
  protobuf
  rpc_header_proto
  tserver_proto)
ADD_YB_LIBRARY(tserver_admin_proto
  SRCS ${TSERVER_ADMIN_YRPC_SRCS}
  DEPS ${TSERVER_ADMIN_YRPC_LIBS}
  NONLINK_DEPS ${TSERVER_ADMIN_YRPC_TGTS})

#########################################
# tserver_service_proto
#########################################

YRPC_GENERATE(
  TSERVER_YRPC_SRCS TSERVER_YRPC_HDRS TSERVER_YRPC_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES tserver_service.proto)
set(TSERVER_YRPC_LIBS
  yrpc
  yb_common_proto
  protobuf
  remote_bootstrap_proto
  rpc_header_proto
  tserver_proto)
ADD_YB_LIBRARY(tserver_service_proto
  SRCS ${TSERVER_YRPC_SRCS}
  DEPS ${TSERVER_YRPC_LIBS}
  NONLINK_DEPS ${TSERVER_YRPC_TGTS})

#########################################
# test_echo_service_proto
#########################################

YRPC_GENERATE(
  TSERVER_YRPC_SRCS TSERVER_YRPC_HDRS TSERVER_YRPC_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES stateful_services/test_echo_service.proto)
set(TSERVER_YRPC_LIBS
  yrpc
  yb_common_proto
  protobuf)
ADD_YB_LIBRARY(test_echo_service_proto
  SRCS ${TSERVER_YRPC_SRCS}
  DEPS ${TSERVER_YRPC_LIBS}
  NONLINK_DEPS ${TSERVER_YRPC_TGTS})

#########################################
# pg_auto_analyze_service_proto
#########################################

YRPC_GENERATE(
  TSERVER_YRPC_SRCS TSERVER_YRPC_HDRS TSERVER_YRPC_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES stateful_services/pg_auto_analyze_service.proto)
set(TSERVER_YRPC_LIBS
  yrpc
  yb_common_proto
  protobuf)
ADD_YB_LIBRARY(pg_auto_analyze_service_proto
  SRCS ${TSERVER_YRPC_SRCS}
  DEPS ${TSERVER_YRPC_LIBS}
  NONLINK_DEPS ${TSERVER_YRPC_TGTS})

#########################################
# tserver_util
#########################################

set(TSERVER_UTIL_SRCS
  tserver_flags.cc
  tserver_error.cc
  tserver_shared_mem.cc)
set(TSERVER_UTIL_LIBS
  yb_util)
ADD_YB_LIBRARY(tserver_util
  SRCS ${TSERVER_UTIL_SRCS}
  DEPS ${TSERVER_UTIL_LIBS})
add_dependencies(tserver_util gen_proto)

#########################################
# tserver
#########################################

include_directories(${YB_BUILD_ROOT}/postgres/include/server)

set(TSERVER_SRCS
  backup_service.cc
  db_server_base.cc
  full_compaction_manager.cc
  heartbeater.cc
  heartbeater_factory.cc
  metrics_snapshotter.cc
  pg_client_service.cc
  pg_client_session.cc
  pg_create_table.cc
  pg_mutation_counter.cc
  pg_response_cache.cc
  pg_sequence_cache.cc
  pg_table_cache.cc
  pg_table_mutation_count_sender.cc
  read_query.cc
  remote_bootstrap_anchor_client.cc
  remote_client_base.cc
  remote_bootstrap_client.cc
  remote_bootstrap_file_downloader.cc
  remote_bootstrap_service.cc
  remote_bootstrap_session.cc
  remote_bootstrap_snapshots.cc
  remote_snapshot_transfer_client.cc
  service_util.cc
  tablet_memory_manager.cc
  tablet_server.cc
  tablet_server_options.cc
  tablet_service.cc
  tablet_validator.cc
  ts_tablet_manager.cc
  tserver-path-handlers.cc
  tserver_auto_flags_manager.cc
  tserver_metrics_heartbeat_data_provider.cc
  server_main_util.cc
  stateful_services/pg_auto_analyze_service.cc
  stateful_services/stateful_service_base.cc
  stateful_services/test_echo_service.cc
  xcluster_async_executor.cc
  xcluster_safe_time_map.cc
  xcluster_consumer.cc
  xcluster_consumer_auto_flags_info.cc
  xcluster_consumer_replication_error.cc
  xcluster_poller.cc
  xcluster_poller_id.cc
  xcluster_poller_stats.cc
  xcluster_output_client.cc
  xcluster_write_implementations.cc)

set(TSERVER_DEPS
  protobuf
  backup_proto
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
  yrpc
  server_common
  server_process
  tablet
  yb_ash
  yb_client
  yb_pggate_flags
  ysql_upgrade
  cdc
  cdc_consumer_proto
  xcluster_producer_proto
  auto_flags_manager_common
)

ADD_YB_LIBRARY(tserver
  SRCS ${TSERVER_SRCS}
  DEPS ${TSERVER_DEPS}
  NONLINK_DEPS postgres)

#########################################
# yb-tserver_util
#########################################

set(YB_TSERVER_UTIL_SRCS
  tserver_call_home.cc)

ADD_YB_LIBRARY(yb-tserver_util
  SRCS ${YB_TSERVER_UTIL_SRCS}
  DEPS tserver)

#########################################
# yb-tserver
#########################################

set(TSERVER_MAIN_DEPS
    tserver
    yb-tserver_util
    yb-cql
    yb-redis
    yb_process_wrapper
    yb_pgwrapper
    ysql_conn_mgr_wrapper
    ${YB_BASE_LIBS})

ADD_YB_LIBRARY(tserver_main_impl
  SRCS tablet_server_main_impl.cc
  DEPS ${TSERVER_MAIN_DEPS})

add_executable("${YB_TSERVER_DYNAMIC_EXE_NAME}"
               tablet_server_main.cc)
target_link_libraries("${YB_TSERVER_DYNAMIC_EXE_NAME}" tserver_main_impl)

#########################################
# Generate tserver_flags.xml
#########################################
set(TSERVER_FLAGS_METADATA_FILE ${YB_BUILD_ROOT}/tserver_flags.xml)
add_custom_command(TARGET "${YB_TSERVER_DYNAMIC_EXE_NAME}" POST_BUILD
  COMMAND "${BUILD_SUPPORT_DIR}/gen_flags_metadata_wrapper"
    "--program_name" "${YB_TSERVER_DYNAMIC_EXE_NAME}"
    "--dynamically_linked_exe_suffix=${YB_DYNAMICALLY_LINKED_EXE_SUFFIX}"
    "--output_file_path" "${TSERVER_FLAGS_METADATA_FILE}"
  BYPRODUCTS "${TSERVER_FLAGS_METADATA_FILE}")

#########################################
# tserver_test_util
#########################################

add_library(tserver_test_util
  mini_tablet_server.cc
  remote_bootstrap_session-test.cc
  tablet_server-test-base.cc
  tablet_server_test_util.cc
  universe_key_test_util.cc)
target_link_libraries(tserver_test_util tablet_test_util tserver yb_test_util yb_util)

#########################################
# tserver tests
#########################################

set(YB_TEST_LINK_LIBS
  rpc_test_util
  tserver
  tserver_test_util
  yb_client # yb::client::YBTableName
  yb-tserver_util
  tablet_test_util
  ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(remote_bootstrap_rocksdb_client-test)
ADD_YB_TEST(remote_bootstrap_rocksdb_session-test)
ADD_YB_TEST(remote_bootstrap_service-test)
ADD_YB_TEST(tablet_server-test)
ADD_YB_TEST(tablet_server-stress-test RUN_SERIAL true)
ADD_YB_TEST(ts_tablet_manager-test)
ADD_YB_TEST(header_manager_impl-test)
ADD_YB_TEST(backup_service-test)

ADD_YB_TEST(encrypted_sstable-test)
YB_TEST_TARGET_LINK_LIBRARIES(encrypted_sstable-test encryption_test_util tserver_test_util tserver)


ADD_YB_TEST(remote_bootstrap_rocksdb_session-test_ent)
ADD_YB_TEST(remote_bootstrap_rocksdb_client-test_ent)

ADD_YB_TEST(xcluster_consumer_replication_error-test)
ADD_YB_TEST(xcluster_consumer_auto_flags_info-test)
