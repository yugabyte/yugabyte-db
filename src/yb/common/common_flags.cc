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

#include "yb/util/flags.h"
#include "yb/util/flag_tags.h"

// Note that this is used by the client or master only, not by tserver.
DEFINE_RUNTIME_int32(yb_num_shards_per_tserver, kAutoDetectNumShardsPerTServer,
    "The default number of shards per table per tablet server when a table is created. If the "
    "value is -1, the system automatically determines an appropriate value based on the number of "
    "CPU cores; it is determined to 1 if enable_automatic_tablet_splitting is set to true.");

DEFINE_RUNTIME_int32(ysql_num_shards_per_tserver, kAutoDetectNumShardsPerTServer,
    "The default number of shards per YSQL table per tablet server when a table is created. If the "
    "value is -1, the system automatically determines an appropriate value based on the number of "
    "CPU cores; it is determined to 1 if enable_automatic_tablet_splitting is set to true.");

DEFINE_bool(ysql_disable_index_backfill, false,
    "A kill switch to disable multi-stage backfill for YSQL indexes.");
TAG_FLAG(ysql_disable_index_backfill, hidden);
TAG_FLAG(ysql_disable_index_backfill, advanced);

DEFINE_bool(enable_pg_savepoints, true,
            "DEPRECATED -- Set to false to disable savepoints in YugaByte PostgreSQL API.");
TAG_FLAG(enable_pg_savepoints, hidden);

DEFINE_bool(enable_automatic_tablet_splitting, false,
            "If false, disables automatic tablet splitting driven from the yb-master side.");

DEFINE_bool(log_ysql_catalog_versions, false,
            "Log YSQL catalog events. For debugging purposes.");
TAG_FLAG(log_ysql_catalog_versions, hidden);

DEFINE_bool(disable_hybrid_scan, false,
            "If true, hybrid scan will be disabled");
TAG_FLAG(disable_hybrid_scan, runtime);

DEFINE_bool(enable_deadlock_detection, false, "If true, enables distributed deadlock detection.");
TAG_FLAG(enable_deadlock_detection, advanced);
TAG_FLAG(enable_deadlock_detection, evolving);

DEFINE_NON_RUNTIME_bool(enable_wait_queues, false,
    "If true, enable wait queues that help provide Wait-on-Conflict behavior during conflict "
    "resolution whenever required.");
TAG_FLAG(enable_wait_queues, evolving);
TAG_FLAG(enable_wait_queues, hidden);

DEFINE_test_flag(bool, enable_db_catalog_version_mode, false,
                 "Enable the per database catalog version mode, a DDL statement is assumed to "
                 "only affect the current database and will only increment catalog version for "
                 "the current database. For an old cluster that is upgraded, this gflag should "
                 "only be turned on after pg_yb_catalog_version is upgraded to one row per "
                 "database.");

namespace yb {

void InitCommonFlags() {
  // Note! Autoflags are in non-promoted state (are set to the initial value) during execution of
  // this function. Be very careful in manipulations with such flags.
}

} // namespace yb
