// Copyright (c) YugabyteDB, Inc.
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

#include "yb/master/master_replication.pb.h"

#include "yb/util/tsan_util.h"

DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_bool(ysql_yb_ddl_rollback_enabled);
DECLARE_bool(ysql_yb_enable_ddl_atomicity_infra);
DECLARE_bool(ysql_yb_enable_ddl_savepoint_infra);
DECLARE_bool(ysql_yb_enable_ddl_savepoint_support);

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
  // Also change the validator for ysql_yb_ddl_transaction_block_enabled flag in common_flag.cc if
  // changing this logic.
  return FLAGS_ysql_yb_enable_ddl_atomicity_infra && FLAGS_ysql_yb_ddl_rollback_enabled;
}

bool YsqlDdlSavepointEnabled() {
  return FLAGS_ysql_yb_enable_ddl_savepoint_infra && FLAGS_ysql_yb_enable_ddl_savepoint_support;
}

template <typename RequestType>
std::vector<TabletId> GetSplitChildTabletIds(const RequestType& req) {
  if (req.new_tablet_ids_size() > 0) {
    return std::vector<TabletId>(req.new_tablet_ids().begin(), req.new_tablet_ids().end());
  }

  return {req.deprecated_new_tablet1_id(), req.deprecated_new_tablet2_id()};
}

template std::vector<TabletId> GetSplitChildTabletIds(const tablet::SplitTabletRequestPB& req);
template std::vector<TabletId> GetSplitChildTabletIds(const master::ProducerSplitTabletInfoPB& req);

std::vector<TabletId> GetSplitChildTabletIds(const tablet::LWSplitTabletRequestPB& req) {
  if (req.new_tablet_ids_size() > 0) {
    return std::vector<TabletId>(req.new_tablet_ids().begin(), req.new_tablet_ids().end());
  }

  return {req.deprecated_new_tablet1_id().ToBuffer(), req.deprecated_new_tablet2_id().ToBuffer()};
}

template <typename RequestType>
std::vector<std::string> GetSplitPartitionKeys(const RequestType& req) {
  if (req.split_partition_keys_size() > 0) {
    return std::vector<std::string>(
        req.split_partition_keys().begin(), req.split_partition_keys().end());
  }

  return {req.deprecated_split_partition_key()};
}

template std::vector<std::string> GetSplitPartitionKeys(const tablet::SplitTabletRequestPB& req);
template std::vector<std::string> GetSplitPartitionKeys(
    const master::ProducerSplitTabletInfoPB& req);

std::vector<std::string> GetSplitEncodedKeys(const tablet::SplitTabletRequestPB& req) {
  if (req.split_encoded_keys_size() > 0) {
    return std::vector<std::string>(
        req.split_encoded_keys().begin(), req.split_encoded_keys().end());
  }

  return {req.deprecated_split_encoded_key()};
}

} // namespace yb
