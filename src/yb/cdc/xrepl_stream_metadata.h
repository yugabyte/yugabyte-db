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

#pragma once

#include <shared_mutex>

#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_types.h"
#include "yb/cdc/xrepl_stream_stats.h"
#include "yb/common/common.pb.h"
#include "yb/common/entity_ids_types.h"
#include "yb/common/hybrid_time.h"
#include "yb/gutil/thread_annotations.h"
#include "yb/master/catalog_entity_info.pb.h"
#include "yb/util/shared_lock.h"

namespace yb {

namespace client {
class YBClient;
}  // namespace client

namespace cdc {

// This class holds the metadata for a CDC stream on the Producer cluster. This is a cache of the
// metadata stored in the CatalogManager. Certain fields like table_ids_, state_, ... can change on
// CatalogManager requiring a partial refresh of the cache.
class StreamMetadata {
 public:
  struct StreamTabletMetadata {
    std::mutex mutex_;
    int64_t term_ GUARDED_BY(mutex_) = 0;
    int64_t apply_safe_time_checkpoint_op_id_ GUARDED_BY(mutex_) = 0;
    HybridTime last_apply_safe_time_ GUARDED_BY(mutex_);
    MonoTime last_apply_safe_time_update_time_ GUARDED_BY(mutex_);

    // Reset the cache if the term changes.
    void ResetOnTermChange(int64_t term) REQUIRES(mutex_);

    // TODO(hari): #16774 Move last_readable_index and last sent opid here, and use them to make
    // UpdateCDCTabletMetrics run asynchronously.

    xrepl::StreamTabletStatsHistory stats_history_;
    void UpdateStats(
        const MonoTime& start_time, const Status& status, int num_records, size_t bytes_received,
        int64_t sent_index, int64_t latest_wal_index);
    void PopulateStats(xrepl::StreamTabletStats* stats) const;
  };

  // Create an empty StreamMetadata object. InitOrReloadIfNeeded must be called before this can be
  // used.
  StreamMetadata() = default;

  xrepl::StreamId GetStreamId() const {
    std::lock_guard l_table(mutex_);
    DCHECK(loaded_);
    return stream_id_;
  }

  NamespaceId GetNamespaceId() const {
    std::lock_guard l_table(mutex_);
    DCHECK(loaded_);
    return namespace_id_;
  }
  CDCRecordType GetRecordType() const {
    std::lock_guard l_table(mutex_);
    DCHECK(loaded_);
    return record_type_;
  }
  CDCRecordFormat GetRecordFormat() const {
    std::lock_guard l_table(mutex_);
    DCHECK(loaded_);
    return record_format_;
  }
  CDCRequestSource GetSourceType() const {
    std::lock_guard l_table(mutex_);
    DCHECK(loaded_);
    return source_type_;
  }
  CDCCheckpointType GetCheckpointType() const {
    std::lock_guard l_table(mutex_);
    DCHECK(loaded_);
    return checkpoint_type_;
  }
  std::optional<CDCSDKSnapshotOption> GetSnapshotOption() const {
    std::lock_guard l_table(mutex_);
    DCHECK(loaded_);
    return consistent_snapshot_option_;
  }
  master::SysCDCStreamEntryPB_State GetState() const {
    DCHECK(loaded_);
    return state_.load(std::memory_order_acquire);
  }
  StreamModeTransactional IsTransactional() const {
    DCHECK(loaded_);
    return transactional_.load(std::memory_order_acquire);
  }
  std::vector<TableId> GetTableIds() const {
    DCHECK(loaded_);
    SharedLock l(table_ids_mutex_);
    return table_ids_;
  }
  std::vector<TableId> GetUnqualifiedTableIds() const {
    DCHECK(loaded_);
    SharedLock l(table_ids_mutex_);
    return unqualified_table_ids_;
  }
  std::optional<uint64_t> GetConsistentSnapshotTime() const {
    DCHECK(loaded_);
    return consistent_snapshot_time_.load(std::memory_order_acquire);
  }
  std::optional<uint64_t> GetStreamCreationTime() const {
    DCHECK(loaded_);
    return stream_creation_time_.load(std::memory_order_acquire);
  }
  std::unordered_map<std::string, PgReplicaIdentity> GetReplicaIdentities() const {
    DCHECK(loaded_);
    SharedLock l(table_ids_mutex_);
    return replica_identitity_map_;
  }
  std::optional<std::string> GetReplicationSlotName() const {
    std::lock_guard l_table(mutex_);
    DCHECK(loaded_);
    return replication_slot_name_;
  }
  std::optional<ReplicationSlotLsnType> GetReplicationSlotLsnType() const {
    std::lock_guard l_table(mutex_);
    DCHECK(loaded_);
    return replication_slot_lsn_type_;
  }
  std::optional<ReplicationSlotOrderingMode> GetReplicationSlotOrderingMode() const {
    std::lock_guard l_table(mutex_);
    DCHECK(loaded_);
    return replication_slot_ordering_mode_;
  }
  std::optional<bool> GetDetectPublicationChangesImplicitly() const {
    std::lock_guard l_table(mutex_);
    DCHECK(loaded_);
    return detect_publication_changes_implicitly_;
  }

  std::optional<uint32_t> GetDbOidToGetSequencesFor() const {
    std::lock_guard l_table(mutex_);
    DCHECK(loaded_);
    return db_oid_to_get_sequences_for_;
  }

  std::shared_ptr<StreamTabletMetadata> GetTabletMetadata(const TabletId& tablet_id)
      EXCLUDES(tablet_metadata_map_mutex_);

  std::vector<xrepl::StreamTabletStats> GetAllStreamTabletStats(
      const xrepl::StreamId& stream_id) const EXCLUDES(tablet_metadata_map_mutex_);

  Status InitOrReloadIfNeeded(
      const xrepl::StreamId& stream_id, RefreshStreamMapOption opts, client::YBClient* client)
      EXCLUDES(load_mutex_);

  std::string ToString() const;

 private:
  Status GetStreamInfoFromMaster(const xrepl::StreamId& stream_id, client::YBClient* client)
      REQUIRES(load_mutex_) EXCLUDES(table_ids_mutex_, tablet_metadata_map_mutex_);

  // Returns std::nullopt if this stream is not for a sequences_data tablet.
  Result<std::optional<uint32_t>> GetDbOidToGetSequencesForUnlocked() const REQUIRES(mutex_);

  mutable std::mutex mutex_;
  xrepl::StreamId stream_id_ GUARDED_BY(mutex_) {xrepl::StreamId::Nil()};
  NamespaceId namespace_id_ GUARDED_BY(mutex_);
  CDCRecordType record_type_ GUARDED_BY(mutex_);
  CDCRecordFormat record_format_ GUARDED_BY(mutex_);
  CDCRequestSource source_type_ GUARDED_BY(mutex_);
  CDCCheckpointType checkpoint_type_ GUARDED_BY(mutex_);
  std::optional<CDCSDKSnapshotOption> consistent_snapshot_option_ GUARDED_BY(mutex_);
  std::optional<ReplicationSlotLsnType> replication_slot_lsn_type_ GUARDED_BY(mutex_);
  std::optional<ReplicationSlotOrderingMode> replication_slot_ordering_mode_ GUARDED_BY(mutex_);
  std::optional<std::string> replication_slot_name_ GUARDED_BY(mutex_);
  std::optional<bool> detect_publication_changes_implicitly_ GUARDED_BY(mutex_);
  // xCluster: if we are a sequences_data stream, then this holds the OID of the DB we are supposed
  // to be getting sequences for.
  std::optional<uint32_t> db_oid_to_get_sequences_for_ GUARDED_BY(mutex_);

  std::atomic<master::SysCDCStreamEntryPB_State> state_;
  std::atomic<StreamModeTransactional> transactional_{StreamModeTransactional::kFalse};
  std::atomic<std::optional<uint64_t>> consistent_snapshot_time_;
  std::atomic<std::optional<uint64_t>> stream_creation_time_;

  std::mutex load_mutex_;  // Used to ensure only a single thread performs InitOrReload.
  std::atomic<bool> loaded_ = false;

  mutable std::shared_mutex table_ids_mutex_;
  std::vector<TableId> table_ids_ GUARDED_BY(table_ids_mutex_);
  std::vector<TableId> unqualified_table_ids_ GUARDED_BY(table_ids_mutex_);
  std::unordered_map<std::string, PgReplicaIdentity> replica_identitity_map_
      GUARDED_BY(table_ids_mutex_);

  mutable std::shared_mutex tablet_metadata_map_mutex_;
  std::unordered_map<TabletId, std::shared_ptr<StreamTabletMetadata>> tablet_metadata_map_
      GUARDED_BY(tablet_metadata_map_mutex_);
};

}  // namespace cdc
}  // namespace yb
