// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/common/common_flags.h"


// Note that this is used by the client or master only, not by tserver.
DEFINE_RUNTIME_int32(yb_num_shards_per_tserver, kAutoDetectNumShardsPerTServer,
    "The default number of shards per table per tablet server when a table is created. If the "
    "value is -1, the system automatically determines an appropriate value based on the number of "
    "CPU cores; it is determined to 1 if enable_automatic_tablet_splitting is set to true.");

DEFINE_RUNTIME_int32(ysql_num_shards_per_tserver, kAutoDetectNumShardsPerTServer,
    "The default number of shards per YSQL table per tablet server when a table is created. If the "
    "value is -1, the system automatically determines an appropriate value based on the number of "
    "CPU cores; it is determined to 1 if enable_automatic_tablet_splitting is set to true.");

DEFINE_UNKNOWN_bool(ysql_disable_index_backfill, false,
    "A kill switch to disable multi-stage backfill for YSQL indexes.");
TAG_FLAG(ysql_disable_index_backfill, hidden);
TAG_FLAG(ysql_disable_index_backfill, advanced);

DEFINE_NON_RUNTIME_bool(
    enable_pg_savepoints, true,
    "Set to false to disable savepoints in YugaByte PostgreSQL API.");
TAG_FLAG(enable_pg_savepoints, stable);
TAG_FLAG(enable_pg_savepoints, advanced);

DEFINE_RUNTIME_AUTO_bool(enable_automatic_tablet_splitting, kNewInstallsOnly, false, true,
    "If false, disables automatic tablet splitting driven from the yb-master side.");

DEFINE_UNKNOWN_bool(log_ysql_catalog_versions, false,
            "Log YSQL catalog events. For debugging purposes.");
TAG_FLAG(log_ysql_catalog_versions, hidden);

DEPRECATE_FLAG(bool, disable_hybrid_scan, "11_2022");
DEFINE_UNKNOWN_bool(enable_deadlock_detection, false,
    "If true, enables distributed deadlock detection.");
TAG_FLAG(enable_deadlock_detection, advanced);
TAG_FLAG(enable_deadlock_detection, evolving);

DEFINE_NON_RUNTIME_bool(enable_wait_queues, false,
    "If true, enable wait queues that help provide Wait-on-Conflict behavior during conflict "
    "resolution whenever required.");
TAG_FLAG(enable_wait_queues, evolving);

DEFINE_RUNTIME_bool(ysql_ddl_rollback_enabled, false,
            "If true, failed YSQL DDL transactions that affect both pg catalog and DocDB schema "
            "will be rolled back by YB-Master. Note that this is applicable only for few DDL "
            "operations such as dropping a table, adding a column, renaming a column/table. This "
            "flag should not be changed in the middle of a DDL operation.");
TAG_FLAG(ysql_ddl_rollback_enabled, hidden);
TAG_FLAG(ysql_ddl_rollback_enabled, advanced);

DEFINE_test_flag(bool, enable_db_catalog_version_mode, false,
                 "Enable the per database catalog version mode, a DDL statement is assumed to "
                 "only affect the current database and will only increment catalog version for "
                 "the current database. For an old cluster that is upgraded, this gflag should "
                 "only be turned on after pg_yb_catalog_version is upgraded to one row per "
                 "database.");

DEFINE_RUNTIME_uint32(external_transaction_retention_window_secs, 60 * 60 * 24,
                      "Retention window on both the coordinator and participant for uncommitted "
                      "transactions from a producer.");

DEFINE_NON_RUNTIME_bool(ysql_enable_pg_per_database_oid_allocator, true,
    "If true, enable per-database PG new object identifier allocator.");
TAG_FLAG(ysql_enable_pg_per_database_oid_allocator, advanced);
TAG_FLAG(ysql_enable_pg_per_database_oid_allocator, hidden);

namespace yb {

void InitCommonFlags() {
  // Note! Autoflags are in non-promoted state (are set to the initial value) during execution of
  // this function. Be very careful in manipulations with such flags.
}

} // namespace yb
