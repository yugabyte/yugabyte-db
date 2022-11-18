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

#include <thread>

#include "yb/util/atomic.h"
#include "yb/util/flags.h"
#include "yb/util/tsan_util.h"
#include "yb/gutil/sysinfo.h"

// Note that this is used by the client or master only, not by tserver.
DEFINE_UNKNOWN_int32(yb_num_shards_per_tserver, kAutoDetectNumShardsPerTServer,
    "The default number of shards per table per tablet server when a table is created. If the "
    "value is -1, the system sets the number of shards per tserver to 1 if "
    "enable_automatic_tablet_splitting is true, and otherwise automatically determines an "
    "appropriate value based on number of CPU cores.");

DEFINE_UNKNOWN_int32(ysql_num_shards_per_tserver, kAutoDetectNumShardsPerTServer,
    "The default number of shards per YSQL table per tablet server when a table is created. If the "
    "value is -1, the system sets the number of shards per tserver to 1 if "
    "enable_automatic_tablet_splitting is true, and otherwise automatically determines an "
    "appropriate value based on number of CPU cores.");

DEFINE_UNKNOWN_bool(ysql_disable_index_backfill, false,
    "A kill switch to disable multi-stage backfill for YSQL indexes.");
TAG_FLAG(ysql_disable_index_backfill, hidden);
TAG_FLAG(ysql_disable_index_backfill, advanced);

DEPRECATE_FLAG(bool, enable_pg_savepoints, "10_2022");

DEFINE_UNKNOWN_bool(enable_automatic_tablet_splitting, true,
            "If false, disables automatic tablet splitting driven from the yb-master side.");

DEFINE_UNKNOWN_bool(log_ysql_catalog_versions, false,
            "Log YSQL catalog events. For debugging purposes.");
TAG_FLAG(log_ysql_catalog_versions, hidden);

DEPRECATE_FLAG(bool, disable_hybrid_scan, "11_2022");
DEFINE_UNKNOWN_bool(enable_deadlock_detection, false,
    "If true, enables distributed deadlock detection.");
TAG_FLAG(enable_deadlock_detection, advanced);
TAG_FLAG(enable_deadlock_detection, evolving);

DEFINE_UNKNOWN_bool(enable_wait_queues, false,
            "If true, use pessimistic locking behavior in conflict resolution.");
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

namespace yb {

static int GetYCQLNumShardsPerTServer() {
  if (GetAtomicFlag(&FLAGS_enable_automatic_tablet_splitting)) {
    return 1;
  }
  int value = 8;
  if (IsTsan()) {
    value = 2;
  } else if (base::NumCPUs() <= 2) {
    value = 4;
  }
  return value;
}

static int GetYSQLNumShardsPerTServer() {
  if (GetAtomicFlag(&FLAGS_enable_automatic_tablet_splitting)) {
    return 1;
  }
  int value = 8;
  if (IsTsan()) {
    value = 2;
  } else if (base::NumCPUs() <= 2) {
    value = 2;
  } else if (base::NumCPUs() <= 4) {
    value = 4;
  }
  return value;
}

void InitCommonFlags() {
  if (GetAtomicFlag(&FLAGS_yb_num_shards_per_tserver) == kAutoDetectNumShardsPerTServer) {
    int value = GetYCQLNumShardsPerTServer();
    VLOG(1) << "Auto setting FLAGS_yb_num_shards_per_tserver to " << value;
    CHECK_OK(SET_FLAG_DEFAULT_AND_CURRENT(yb_num_shards_per_tserver, value));
  }
  if (GetAtomicFlag(&FLAGS_ysql_num_shards_per_tserver) == kAutoDetectNumShardsPerTServer) {
    int value = GetYSQLNumShardsPerTServer();
    VLOG(1) << "Auto setting FLAGS_ysql_num_shards_per_tserver to " << value;
    CHECK_OK(SET_FLAG_DEFAULT_AND_CURRENT(ysql_num_shards_per_tserver, value));
  }
}

} // namespace yb
