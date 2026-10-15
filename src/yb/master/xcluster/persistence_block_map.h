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

#include <memory>
#include <shared_mutex>
#include <vector>

#include "yb/common/hybrid_time.h"

#include "yb/gutil/thread_annotations.h"

#include "yb/master/leader_epoch.h"

#include "yb/util/status_fwd.h"

namespace yb {

class PersistenceBlockMapPB;

namespace master {

class PersistenceBlockMapInfo;
class SysCatalogTable;
class SysPersistenceBlockMapEntryPB;

// Wrapper over PersistenceBlockMapInfo (see PersistenceBlockMapPB) that provides safe utils to
// access and modify it.
class PersistenceBlockMap {
 public:
  explicit PersistenceBlockMap(SysCatalogTable* sys_catalog);
  ~PersistenceBlockMap();

  void ClearState() EXCLUDES(mutex_);

  void Load(const SysPersistenceBlockMapEntryPB& metadata) EXCLUDES(mutex_);

  // Creates an empty map if none was loaded, so universes upgraded from versions without the
  // entry get one.
  Status PrepareDefault(int64_t term, bool re_create) EXCLUDES(mutex_);

  void DumpState(std::ostream* out) const EXCLUDES(mutex_);

  Status Fill(PersistenceBlockMapPB& map) const EXCLUDES(mutex_);

  // The map together with its version.
  Result<SysPersistenceBlockMapEntryPB> GetEntry() const EXCLUDES(mutex_);

  // Sets the map entry of every database in db_oids to blocked_through_ht, bumps the version, and
  // persists.
  Status SetBlockedThrough(
      const LeaderEpoch& epoch, const std::vector<uint32_t>& db_oids, HybridTime blocked_through_ht)
      EXCLUDES(mutex_);

  // Removes db_oid's entry, if any, and persists.  No-op if there is no entry.
  Status Erase(const LeaderEpoch& epoch, uint32_t db_oid) EXCLUDES(mutex_);

 private:
  SysCatalogTable* const sys_catalog_;

  mutable std::shared_mutex mutex_;

  std::unique_ptr<PersistenceBlockMapInfo> persistence_block_map_info_ GUARDED_BY(mutex_);
};

}  // namespace master
}  // namespace yb
