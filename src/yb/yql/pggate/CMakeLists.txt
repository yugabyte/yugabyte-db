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

set(YB_PCH_PREFIX pggate)
set(YB_PCH_DEP_LIBS yb_test_util)

# We need access to the generated pg_type_d.h header from this directory.
include_directories("${YB_BUILD_ROOT}/postgres_build/src/backend")

# Utility functions that are specific for PGGate.
add_subdirectory(util)

add_subdirectory(webserver)

add_subdirectory(ysql_bench_metrics_handler)

# PGGate API library.
set(PGGATE_SRCS
    ybc_pggate.cc
    pggate.cc
    pggate_thread_local_vars.cc
    pg_client.cc
    pg_op.cc
    pg_operation_buffer.cc
    pg_perform_future.cc
    pg_sample.cc
    pg_session.cc
    pg_ddl.cc
    pg_dml.cc
    pg_dml_write.cc
    pg_dml_read.cc
    pg_function.cc
    pg_function_helpers.cc
    pg_type.cc
    pg_select.cc
    pg_select_index.cc
    pg_expr.cc
    pg_column.cc
    pg_doc_op.cc
    pg_table.cc
    pg_tabledesc.cc
    pg_txn_manager.cc
    pg_value.cc
    pg_memctx.cc
    pg_callbacks.cc
    pg_tools.cc
    pg_sys_table_prefetcher.cc
    pg_doc_metrics.cc)

ADD_YB_LIBRARY(yb_pggate_flags
               SRCS pggate_flags.cc
               DEPS yb_ash yb_util)

set(PGGATE_LIBS
    consensus_error
    cdc_service_proto
    server_common
    yb_ash
    yb_util
    yb_common
    yb_client
    tserver_util
    yb_pggate_util
    yb_pggate_flags
    tablet_error
    pg_client_proto)

ADD_YB_LIBRARY(yb_pggate
               SRCS ${PGGATE_SRCS}
               DEPS ${PGGATE_LIBS})

add_dependencies(yb_pggate postgres_genbki)

yb_use_pch(yb_pggate pggate)

# Test
add_subdirectory(test)
