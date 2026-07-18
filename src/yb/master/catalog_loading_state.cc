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

#include "yb/master/catalog_loading_state.h"

namespace yb::master {

SysCatalogLoadingState::SysCatalogLoadingState(LeaderEpoch leader_epoch)
    : epoch(std::move(leader_epoch)) {}

void SysCatalogLoadingState::AddPostLoadTask(std::function<void()>&& func, std::string&& msg) {
  post_load_tasks.push_back({std::move(func), std::move(msg)});
}

void SysCatalogLoadingState::Reset() {
  parent_to_child_tables.clear();
  post_load_tasks.clear();
  write_to_disk_tables.clear();
  validate_backfill_status_index_tables.clear();
}
}  // namespace yb::master
