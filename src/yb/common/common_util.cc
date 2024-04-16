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

#include "yb/common/common_util.h"

#include "yb/common/common_flags.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/sysinfo.h"

#include "yb/util/flags.h"
#include "yb/util/tsan_util.h"

DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_bool(ysql_yb_ddl_rollback_enabled);
DECLARE_bool(ysql_yb_enable_ddl_atomicity_infra);

namespace yb {

namespace {

size_t GetDefaultYbNumShardsPerTServer(const bool is_automatic_tablet_splitting_enabled) {
  if (is_automatic_tablet_splitting_enabled) {
    return 1;
  }
  if (IsTsan()) {
    return 2;
  }
  if (base::NumCPUs() <= 2) {
    return 4;
  }
  return 8;
}

size_t GetDefaultYSQLNumShardsPerTServer(const bool is_automatic_tablet_splitting_enabled) {
  if (is_automatic_tablet_splitting_enabled) {
    return 1;
  }
  if (IsTsan()) {
    return 2;
  }
  if (base::NumCPUs() <= 2) {
    return 2;
  }
  if (base::NumCPUs() <= 4) {
    return 4;
  }
  return 8;
}

int GetInitialNumTabletsPerTable(bool is_ysql, size_t tserver_count) {
  // It is possible to get zero value depending on how the method is triggered:
  // for example, client side may still see the value as 0 in case if tservers has not yet
  // heartbeated to a master or master has not yet handled the heartbeats while client is already
  // requesting the number (list) of tservers.
  if (tserver_count == 0) {
    return 0;
  }

  const auto num_shards_per_tserver = is_ysql ? FLAGS_ysql_num_shards_per_tserver
                                              : FLAGS_yb_num_shards_per_tserver;
  if (num_shards_per_tserver != kAutoDetectNumShardsPerTServer) {
    return yb::narrow_cast<int>(tserver_count * num_shards_per_tserver);
  }

  // Save the flag to make sure it is not changed while handling defaults.
  const bool automatic_tablet_splitting_enabled = FLAGS_enable_automatic_tablet_splitting;

  // Get default shards number per tserver.
  size_t num_tablets = tserver_count * (is_ysql ?
      GetDefaultYSQLNumShardsPerTServer(automatic_tablet_splitting_enabled) :
      GetDefaultYbNumShardsPerTServer(automatic_tablet_splitting_enabled));

  // Special handling for low core machines with automatic tablet splitting enabled:
  // limit the number of tablets in case of low number of CPU cores.
  if (automatic_tablet_splitting_enabled) {
    if (base::NumCPUs() <= 2) {
      num_tablets = 1;
    } else if (base::NumCPUs() <= 4) {
      num_tablets = std::min<size_t>(2, num_tablets);
    }
  }

  return yb::narrow_cast<int>(num_tablets);
}

} // anonymous namespace

int GetInitialNumTabletsPerTable(YQLDatabase db_type, size_t tserver_count) {
  return GetInitialNumTabletsPerTable(db_type == YQL_DATABASE_PGSQL, tserver_count);
}

int GetInitialNumTabletsPerTable(TableType table_type, size_t tserver_count) {
  return GetInitialNumTabletsPerTable(table_type == PGSQL_TABLE_TYPE, tserver_count);
}

bool YsqlDdlRollbackEnabled() {
  return FLAGS_ysql_yb_enable_ddl_atomicity_infra && FLAGS_ysql_yb_ddl_rollback_enabled;
}

} // namespace yb
