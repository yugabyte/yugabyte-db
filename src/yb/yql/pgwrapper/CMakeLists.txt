# Copyright (c) YugaByte, Inc.
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

set(YB_PCH_PREFIX pgwrapper)
set(YB_PCH_DEP_LIBS opid_proto)

set(PGWRAPPER_SRCS
    pg_wrapper.cc)
set(PGWRAPPER_LIBS
    yb_common
    yb_pggate_flags
    yb_util)
ADD_YB_LIBRARY(yb_pgwrapper
               SRCS ${PGWRAPPER_SRCS}
               DEPS ${PGWRAPPER_LIBS})

# We indirectly use some headers that are generated while processing the protobufs below.
add_dependencies(
  yb_pgwrapper
  gen_src_yb_tserver_backup_proto
  gen_src_yb_docdb_docdb_proto
  gen_src_yb_util_opid_proto
  gen_src_yb_tablet_metadata_proto
  gen_src_yb_tablet_operations_proto
  gen_src_yb_tablet_tablet_proto
  gen_src_yb_tablet_tablet_types_proto
  gen_src_yb_tserver_tserver_proto
  gen_src_yb_tserver_tserver_types_proto
  gen_src_yb_cdc_cdc_consumer_proto
  gen_src_yb_encryption_encryption_proto
  gen_src_yb_master_master_heartbeat_proto
  gen_src_yb_master_master_types_proto
  gen_src_yb_tserver_tserver_service_proto
  gen_src_yb_rpc_any_proto
  gen_src_yb_consensus_consensus_types_proto
  )

set(PQ_UTILS_SRCS
    libpq_utils.cc)
set(PQ_UTILS_DEPS
    pq
    yb_common_proto
    yb_pggate_flags)
ADD_YB_LIBRARY(pq_utils
               SRCS ${PQ_UTILS_SRCS}
               DEPS ${PQ_UTILS_DEPS})

set(PG_WRAPPER_TEST_BASE_SRCS
    libpq_test_base.cc
    pg_mini_test_base.cc
    pg_tablet_split_test_base.cc
    pg_wrapper_test_base.cc)
set(PG_WRAPPER_TEST_BASE_DEPS
    pq_utils)
ADD_YB_LIBRARY(pg_wrapper_test_base
               SRCS ${PG_WRAPPER_TEST_BASE_SRCS}
               DEPS ${PG_WRAPPER_TEST_BASE_DEPS})

target_link_libraries(pg_wrapper_test_base integration-tests)

set(GEO_TRANSACTIONS_TEST_BASE_SRCS
    geo_transactions_test_base.cc)
set(GEO_TRANSACTIONS_TEST_BASE_DEPS
    pg_wrapper_test_base)
ADD_YB_LIBRARY(geo_transactions_test_base
               SRCS ${GEO_TRANSACTIONS_TEST_BASE_SRCS}
               DEPS ${GEO_TRANSACTIONS_TEST_BASE_DEPS})

target_link_libraries(geo_transactions_test_base pg_wrapper_test_base integration-tests)

set(YSQL_UPGRADE_SRCS
    ysql_upgrade.cc)
set(YSQL_UPGRADE_DEPS
    yb_pgwrapper
    pq_utils)
ADD_YB_LIBRARY(ysql_upgrade
               SRCS ${YSQL_UPGRADE_SRCS}
               DEPS ${YSQL_UPGRADE_DEPS})

set(YB_TEST_LINK_LIBS yb_pgwrapper yb_client ql-dml-test-base pg_wrapper_test_base tools_test_utils
    geo_transactions_test_base ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(pg_catalog_perf-test)
ADD_YB_TEST(pg_clock_skew-test)
ADD_YB_TEST(pg_ddl_concurrency-test)
ADD_YB_TEST(pg_fkey-test)
ADD_YB_TEST(pg_gin_index-test)
ADD_YB_TEST(pg_index_backfill-test)
ADD_YB_TEST(pg_libpq-test)
ADD_YB_TEST(pg_libpq_err-test)
ADD_YB_TEST(pg_load_balancer-test)
ADD_YB_TEST(pg_mini-test)
ADD_YB_TEST(pg_on_conflict-test)
ADD_YB_TEST(pg_op_buffering-test)
ADD_YB_TEST(pg_packed_row-test)
ADD_YB_TEST(pg_tablet_split-test)
ADD_YB_TEST(pg_txn-test)
ADD_YB_TEST(pg_wrapper-test)
ADD_YB_TEST(alter_table_with_concurrent_txn-test)
ADD_YB_TEST(geo_transactions-test)
ADD_YB_TEST(geo_transactions_promotion-test)
ADD_YB_TEST(pg_explicit_lock-test)
ADD_YB_TEST(alter_schema_abort_txn-test)
ADD_YB_TEST(pg_cache_refresh-test)
ADD_YB_TEST(pg_ddl_atomicity-test)
ADD_YB_TEST(pg_server_restart-test)
ADD_YB_TEST(pg_retryable_request-test)

# This is really a tool, not a test, but uses a lot of existing test infrastructure.
ADD_YB_TEST(create_initial_sys_catalog_snapshot)
set(CREATE_INITIAL_SYS_CATALOG_SNAPSHOT_TOOL_PATH ${LAST_ADDED_TEST_BINARY_PATH})
set(CREATE_INITIAL_SYS_CATALOG_SNAPSHOT_TARGET_NAME ${LAST_ADDED_TEST_BINARY_NAME})

set(gen_initial_sys_catalog_snapshot_cmd
    "${CMAKE_COMMAND}" -E env "YB_BUILD_ROOT=$ENV{YB_BUILD_ROOT}"
    "${BUILD_SUPPORT_DIR}/gen_initial_sys_catalog_snapshot_wrapper")

if ("${YB_BUILD_TYPE}" STREQUAL "tsan")
  # Don't build the initial sys catalog snapshot target automatically in TSAN. But still create the
  # target so it could be built explicitly.
  add_custom_target(
    initial_sys_catalog_snapshot  # Don't add "ALL" here.
    COMMAND ${gen_initial_sys_catalog_snapshot_cmd})
else()
  add_custom_target(
    initial_sys_catalog_snapshot ALL
    COMMAND ${gen_initial_sys_catalog_snapshot_cmd})
endif()

add_dependencies(
  initial_sys_catalog_snapshot
  ${CREATE_INITIAL_SYS_CATALOG_SNAPSHOT_TARGET_NAME} yb-master yb-tserver postgres)

add_custom_target(
  update_ysql_migrations ALL
  COMMAND ${CMAKE_COMMAND} -E remove_directory
    ${YB_BUILD_ROOT}/share/ysql_migrations
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/ysql_migrations
    ${YB_BUILD_ROOT}/share/ysql_migrations
  COMMENT "Updating YSQL migrations")
