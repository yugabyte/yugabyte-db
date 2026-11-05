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
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <vector>

#include "yb/common/common_types.pb.h"
#include "yb/common/hybrid_time.h"

#include "yb/gutil/thread_annotations.h"

#include "yb/master/leader_epoch.h"

#include "yb/util/status_fwd.h"

namespace yb::master {

class CatalogManager;
class PersistenceBlockMapInfo;
class SysCatalogTable;
class SysPersistenceBlockMapEntryPB;

// Wrapper over PersistenceBlockMapInfo (see PersistenceBlockMapPB) that provides safe utils to
// access and modify it.
class PersistenceBlockMap {
 public:
  PersistenceBlockMap(const CatalogManager& catalog_manager, SysCatalogTable* sys_catalog);
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

  // The time through which the map, as of term or later, blocks database_oid's stamped writes and
  // commits, or nullopt if it has no entry for database_oid.  Fails with LeaderNotReadyToServe
  // while a catalog load is in progress or if the last completed load was for a term before term.
  // Never waits for a sys catalog write, so it may be called under the sys catalog tablet's Raft
  // lock.
  Result<std::optional<HybridTime>> GetBlockedThrough(int64_t term, uint32_t database_oid) const
      EXCLUDES(cache_mutex_);

  // Sets the map entry of every database in db_oids to blocked_through_ht, bumps the version, and
  // persists.  On failure the change may or may not have been persisted: a timed-out sys catalog
  // write can still commit later.  Only a successful call updates the in-memory map and cache; a
  // change persisted by a failed call takes effect at the next catalog load.
  Status SetBlockedThrough(
      const LeaderEpoch& epoch, const std::vector<uint32_t>& db_oids, HybridTime blocked_through_ht)
      EXCLUDES(mutex_);

  // Removes db_oid's entry, if any, and persists.  No-op if there is no entry.  On failure the
  // removal may or may not have been persisted, as for SetBlockedThrough.
  Status Erase(const LeaderEpoch& epoch, uint32_t db_oid) EXCLUDES(mutex_);

 private:
  const CatalogManager& catalog_manager_;
  SysCatalogTable* const sys_catalog_;

  mutable std::shared_mutex mutex_;

  // nullptr iff no load has completed since the last ClearState, i.e., before the first load, or
  // while a catalog load is in progress or after one failed; the accessors then fail.  A non-null
  // value may be from a term this master no longer leads; writes based on it fail at the sys
  // catalog's term check.
  std::unique_ptr<PersistenceBlockMapInfo> persistence_block_map_info_ GUARDED_BY(mutex_);

  // Never held across a sys catalog write; see GetBlockedThrough.
  mutable std::mutex cache_mutex_;

  // A copy of the map in persistence_block_map_info_, for GetBlockedThrough.  Load sets the copy.
  // Each write (e.g., SetBlockedThrough) sets the copy after persisting and before committing while
  // the writer still holds the catalog entry's write lock so the copy changes in the order the
  // writes are persisted.
  //
  // ClearState intentionally does not touch the copy so we keep the map from the previous term
  // until the next load replaces it.  We control the resulting staleness by using
  // GetBlockedThrough's term argument to decide whether that is recent enough.
  PersistenceBlockMapPB cache_ GUARDED_BY(cache_mutex_);

  void UpdateCache(const PersistenceBlockMapPB& map) EXCLUDES(cache_mutex_);
};

}  // namespace yb::master
