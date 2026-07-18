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

#pragma once

#include "yb/common/common_types.pb.h"
#include "yb/common/entity_ids_types.h"

#include "yb/master/master_replication.pb.h"

#include "yb/tablet/operations.messages.h"

namespace yb {

// These functions calculate the initial number of tablets per tablet on base of the specified
// number of tservers specified, flags (ysql_num_shards_per_tserver, yb_num_shards_per_tserver
// and enable_automatic_tablet_splitting) and number of CPU cores.
// Special case: 0 is returned if tserver_count is specified as 0; the caller is responsible to
// handle this case by itself.
int GetInitialNumTabletsPerTable(YQLDatabase db_type, size_t tserver_count);
int GetInitialNumTabletsPerTable(TableType table_type, size_t tserver_count);

// Returns true if YSQL DDL rollback is enabled.
bool YsqlDdlRollbackEnabled();

// Returns true if YSQL DDL savepoint support is enabled.
bool YsqlDdlSavepointEnabled();

// These functions help extract the tablet split parameters from the split request uniformly across
// the singular and repeated variants of the parameter fields.
template <typename RequestType>
std::vector<TabletId> GetSplitChildTabletIds(const RequestType& req);

extern template std::vector<TabletId> GetSplitChildTabletIds(
    const tablet::SplitTabletRequestPB& req);
extern template std::vector<TabletId> GetSplitChildTabletIds(
    const master::ProducerSplitTabletInfoPB& req);

std::vector<TabletId> GetSplitChildTabletIds(const tablet::LWSplitTabletRequestPB& req);

template <typename RequestType>
std::vector<std::string> GetSplitPartitionKeys(const RequestType& req);

extern template std::vector<std::string> GetSplitPartitionKeys(
    const tablet::SplitTabletRequestPB& req);
extern template std::vector<std::string> GetSplitPartitionKeys(
    const master::ProducerSplitTabletInfoPB& req);

std::vector<std::string> GetSplitEncodedKeys(const tablet::SplitTabletRequestPB& req);

// Returns the vector item at the specified index if valid (i.e., in the range [0, size)),
// otherwise returns the default value.
inline const std::string& GetVectorItemOrDefault(
    const std::vector<std::string>& vec, int idx, const std::string& default_val) {
  return (idx >= 0 && static_cast<size_t>(idx) < vec.size()) ? vec[idx] : default_val;
}

inline const Slice GetVectorItemOrDefault(
    const std::vector<std::string>& vec, int idx, const Slice default_val) {
  return (idx >= 0 && static_cast<size_t>(idx) < vec.size()) ? vec[idx] : default_val;
}

} // namespace yb
