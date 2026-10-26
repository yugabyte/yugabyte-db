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

#include "yb/master/xcluster/persistence_block_map.h"

#include "yb/common/common_types.pb.h"

#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/catalog_manager-internal.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/xcluster/xcluster_catalog_entity.h"

#include "yb/util/shared_lock.h"

namespace yb::master {

namespace {

// persistence_block_map_info is null iff no catalog load has completed since the last ClearState.
Status CheckLoaded(const PersistenceBlockMapInfo* persistence_block_map_info) {
  SCHECK(
      persistence_block_map_info, IllegalState,
      "Persistence block map is not loaded: the catalog has not been loaded yet, or a load is "
      "in progress or failed");
  return Status::OK();
}

}  // namespace

PersistenceBlockMap::PersistenceBlockMap(
    const CatalogManager& catalog_manager, SysCatalogTable* sys_catalog)
    : catalog_manager_(catalog_manager), sys_catalog_(sys_catalog) {}

PersistenceBlockMap::~PersistenceBlockMap() = default;

void PersistenceBlockMap::ClearState() {
  std::lock_guard l(mutex_);
  persistence_block_map_info_.reset();
  // cache_ is not cleared on purpose: we keep the map from the previous term until the next load
  // replaces it.  We control staleness by using GetBlockedThrough's term argument to decide whether
  // that is recent enough.
}

void PersistenceBlockMap::Load(const SysPersistenceBlockMapEntryPB& metadata) {
  std::lock_guard mutex_lock(mutex_);
  DCHECK(!persistence_block_map_info_) << "Already have persistence block map data!";

  persistence_block_map_info_ = std::make_unique<PersistenceBlockMapInfo>();
  persistence_block_map_info_->Load(metadata);
  UpdateCache(metadata.persistence_block_map());
}

Status PersistenceBlockMap::PrepareDefault(int64_t term, bool re_create) {
  std::lock_guard mutex_lock(mutex_);

  if (persistence_block_map_info_ && !re_create) {
    return Status::OK();
  }

  persistence_block_map_info_ = std::make_unique<PersistenceBlockMapInfo>();
  auto l = persistence_block_map_info_->LockForWrite();
  l.mutable_data()->pb.set_version(0);
  l.mutable_data()->pb.mutable_persistence_block_map();
  RETURN_NOT_OK(sys_catalog_->Upsert(term, persistence_block_map_info_.get()));
  UpdateCache(l->pb.persistence_block_map());
  l.Commit();

  return Status::OK();
}

void PersistenceBlockMap::DumpState(std::ostream* out) const {
  SharedLock mutex_lock(mutex_);
  if (!persistence_block_map_info_) {
    return;
  }

  *out << "Persistence block map: "
       << persistence_block_map_info_->LockForRead()->pb.ShortDebugString() << "\n";
}

Status PersistenceBlockMap::Fill(PersistenceBlockMapPB& map) const {
  SharedLock mutex_lock(mutex_);
  RETURN_NOT_OK(CheckLoaded(persistence_block_map_info_.get()));
  map = persistence_block_map_info_->LockForRead()->pb.persistence_block_map();
  return Status::OK();
}

Result<SysPersistenceBlockMapEntryPB> PersistenceBlockMap::GetEntry() const {
  SharedLock mutex_lock(mutex_);
  RETURN_NOT_OK(CheckLoaded(persistence_block_map_info_.get()));
  return persistence_block_map_info_->LockForRead()->pb;
}

Result<std::optional<HybridTime>> PersistenceBlockMap::GetBlockedThrough(
    int64_t term, uint32_t database_oid) const {
  // Only cache_mutex_, never mutex_: writers hold mutex_ across their sys catalog writes, which
  // need the sys catalog tablet's Raft lock, which our caller may already hold.
  //
  // The term before the cache: a load resets the term before clearing in-memory state, and sets it
  // only after updating the cache.  A load that starts after the term is read can only replace the
  // cache with a newer map.
  const auto loaded_term = catalog_manager_.catalog_loaded_term();
  if (loaded_term < term) {
    return STATUS_FORMAT(
        LeaderNotReadyToServe, "Catalog is not yet loaded for term $0 (last loaded for term $1)",
        term, loaded_term);
  }
  std::lock_guard l(cache_mutex_);
  const auto& blocked_through_ht = cache_.blocked_through_ht();
  const auto it = blocked_through_ht.find(database_oid);
  if (it == blocked_through_ht.end()) {
    return std::nullopt;
  }
  return HybridTime::FromPB(it->second);
}

void PersistenceBlockMap::UpdateCache(const PersistenceBlockMapPB& map) {
  std::lock_guard l(cache_mutex_);
  cache_ = map;
}

Status PersistenceBlockMap::SetBlockedThrough(
    const LeaderEpoch& epoch, const std::vector<uint32_t>& db_oids, HybridTime blocked_through_ht) {
  SharedLock mutex_lock(mutex_);
  RETURN_NOT_OK(CheckLoaded(persistence_block_map_info_.get()));

  auto l = persistence_block_map_info_->LockForWrite();
  auto& catalog_entry = l.mutable_data()->pb;
  auto& block_map = *catalog_entry.mutable_persistence_block_map()->mutable_blocked_through_ht();
  for (const auto db_oid : db_oids) {
    block_map[db_oid] = blocked_through_ht.ToUint64();
  }
  catalog_entry.set_version(catalog_entry.version() + 1);
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Upsert(epoch.leader_term, persistence_block_map_info_.get()),
      "updating persistence block map in sys-catalog"));
  // Before Commit releases the write lock, so concurrent writers update cache_ in the order they
  // persist.
  UpdateCache(catalog_entry.persistence_block_map());
  l.Commit();

  return Status::OK();
}

Status PersistenceBlockMap::Erase(const LeaderEpoch& epoch, uint32_t db_oid) {
  SharedLock mutex_lock(mutex_);
  RETURN_NOT_OK(CheckLoaded(persistence_block_map_info_.get()));

  auto l = persistence_block_map_info_->LockForWrite();
  auto& catalog_entry = l.mutable_data()->pb;
  auto& block_map = *catalog_entry.mutable_persistence_block_map()->mutable_blocked_through_ht();
  if (!block_map.erase(db_oid)) {
    return Status::OK();
  }
  catalog_entry.set_version(catalog_entry.version() + 1);
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Upsert(epoch.leader_term, persistence_block_map_info_.get()),
      "updating persistence block map in sys-catalog"));
  // Before Commit releases the write lock, so concurrent writers update cache_ in the order they
  // persist.
  UpdateCache(catalog_entry.persistence_block_map());
  l.Commit();

  return Status::OK();
}

}  // namespace yb::master
