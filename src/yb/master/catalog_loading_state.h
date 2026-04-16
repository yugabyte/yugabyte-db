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

#include "yb/common/entity_ids_types.h"

#include "yb/master/leader_epoch.h"

namespace yb::master {

struct SysCatalogLoadingState {
  std::unordered_map<TableId, std::vector<TableId>> parent_to_child_tables;
  std::vector<std::pair<std::function<void()>, std::string>> post_load_tasks;

  // The tables which require their memory state to write to disk.
  TableIdSet write_to_disk_tables;

  // Index tables which require backfill status validation (by checking GC delete markers state).
  // The tables are grouped by the indexed table id for performance reasons.
  std::unordered_map<TableId, TableIdSet> validate_backfill_status_index_tables;

  const LeaderEpoch epoch;

  explicit SysCatalogLoadingState(LeaderEpoch leader_epoch = {});

  void AddPostLoadTask(std::function<void()>&& func, std::string&& msg);

  void Reset();
};
}  // namespace yb::master
