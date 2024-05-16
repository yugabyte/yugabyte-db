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

#pragma once

#include "yb/common/entity_ids.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/opid.h"
#include "yb/common/snapshot.h"

#include "yb/docdb/docdb.pb.h"
#include "yb/gutil/ref_counted.h"

#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/master_fwd.h"
#include "yb/master/master_heartbeat.fwd.h"
#include "yb/master/master_types.h"
#include "yb/master/master_types.pb.h"

#include "yb/tablet/snapshot_coordinator.h"
#include "yb/tablet/tablet_retention_policy.h"

#include "yb/util/status_fwd.h"

namespace yb {
namespace master {

struct SnapshotScheduleRestoration {
  TxnSnapshotId snapshot_id;
  HybridTime restore_at;
  TxnSnapshotRestorationId restoration_id;
  OpId op_id;
  HybridTime write_time;
  int64_t term;
  // DB OID of the database that is being restored.
  std::optional<int64_t> db_oid;
  std::vector<std::pair<SnapshotScheduleId, SnapshotScheduleFilterPB>> schedules;
  std::vector<std::pair<TabletId, SysTabletsEntryPB>> non_system_obsolete_tablets;
  std::vector<std::pair<TableId, SysTablesEntryPB>> non_system_obsolete_tables;
  std::unordered_map<std::string, SysRowEntryType> non_system_objects_to_restore;
  // YSQL pg_catalog tables as of the current time.
  std::unordered_map<TableId, TableName> existing_system_tables;
  // YSQL pg_catalog tables as of time in the past to which we are restoring.
  std::unordered_set<TableId> restoring_system_tables;
  // Captures split relationships between tablets.
  struct SplitTabletInfo {
    std::pair<TabletId, SysTabletsEntryPB*> parent;
    std::unordered_map<TabletId, SysTabletsEntryPB*> children;
  };
  std::unordered_map<TableId, std::vector<TableId>> parent_to_child_tables;
  // Tablets as of the restoring time with their parent-child relationships.
  // Map from parent tablet id -> information about parent and children.
  // For colocated tablets or tablets that have not been split as of restoring time,
  // only the 'parent' field of SplitTabletInfo above will be populated and 'children'
  // map of SplitTabletInfo will be empty.
  std::unordered_map<TabletId, SplitTabletInfo> non_system_tablets_to_restore;
};

// Class that coordinates transaction aware snapshots at master.
class MasterSnapshotCoordinator : public tablet::SnapshotCoordinator {
 public:
  explicit MasterSnapshotCoordinator(SnapshotCoordinatorContext* context, CatalogManager* cm);
  ~MasterSnapshotCoordinator();

  Result<TxnSnapshotId> Create(
      const SysRowEntries& entries, bool imported, int64_t leader_term, CoarseTimePoint deadline,
      int32_t retention_duration_hours);

  Result<TxnSnapshotId> CreateForSchedule(
      const SnapshotScheduleId& schedule_id, int64_t leader_term, CoarseTimePoint deadline);

  Status Delete(
      const TxnSnapshotId& snapshot_id, int64_t leader_term, CoarseTimePoint deadline);

  Status AbortRestore(
      const TxnSnapshotRestorationId& restoration_id, int64_t leader_term,
      CoarseTimePoint deadline);

  // As usual negative leader_term means that this operation was replicated at the follower.
  Status CreateReplicated(int64_t leader_term, const tablet::SnapshotOperation& operation) override;

  Status DeleteReplicated(int64_t leader_term, const tablet::SnapshotOperation& operation) override;

  Status RestoreSysCatalogReplicated(
      int64_t leader_term, const tablet::SnapshotOperation& operation,
      Status* complete_status) override;

  Status ListSnapshots(
      const TxnSnapshotId& snapshot_id, bool list_deleted,
      ListSnapshotsDetailOptionsPB options, ListSnapshotsResponsePB* resp);

  Result<TxnSnapshotRestorationId> Restore(
      const TxnSnapshotId& snapshot_id, HybridTime restore_at, int64_t leader_term);

  Status ListRestorations(
      const TxnSnapshotRestorationId& restoration_id, const TxnSnapshotId& snapshot_id,
      ListSnapshotRestorationsResponsePB* resp);

  Result<SnapshotScheduleId> CreateSchedule(
      const CreateSnapshotScheduleRequestPB& request, int64_t leader_term,
      CoarseTimePoint deadline);

  Status ListSnapshotSchedules(
      const SnapshotScheduleId& snapshot_schedule_id, ListSnapshotSchedulesResponsePB* resp);

  Status DeleteSnapshotSchedule(
      const SnapshotScheduleId& snapshot_schedule_id, int64_t leader_term,
      CoarseTimePoint deadline);

  Result<SnapshotScheduleInfoPB> EditSnapshotSchedule(
      const SnapshotScheduleId& id, const EditSnapshotScheduleRequestPB& req, int64_t leader_term,
      CoarseTimePoint deadline);

  Status RestoreSnapshotSchedule(
      const SnapshotScheduleId& schedule_id, HybridTime restore_at,
      RestoreSnapshotScheduleResponsePB* resp, int64_t leader_term, CoarseTimePoint deadline);

  // Load snapshots data from system catalog.
  Status Load(tablet::Tablet* tablet) override;

  // Check whether we have write request for snapshot while replaying write request during
  // bootstrap. And upsert snapshot from it in this case.
  // key and value are entry from the write batch.
  Status ApplyWritePair(const Slice& key, const Slice& value) override;

  Status FillHeartbeatResponse(TSHeartbeatResponsePB* resp);

  docdb::HistoryCutoff AllowedHistoryCutoffProvider(
      tablet::RaftGroupMetadata* metadata);

  void SysCatalogLoaded(int64_t leader_term);

  Result<docdb::KeyValuePairPB> UpdateRestorationAndGetWritePair(
      SnapshotScheduleRestoration* restoration);

  // For each returns map from schedule id to sorted vectors of tablets id in this schedule.
  Result<SnapshotSchedulesToObjectIdsMap> MakeSnapshotSchedulesToObjectIdsMap(
      SysRowEntryType type);

  Result<std::vector<SnapshotScheduleId>> GetSnapshotSchedules(
      SysRowEntryType type, const std::string& object_id);

  // Returns the id of a completed snapshot suitable for restoring to the given restore time.
  Result<TxnSnapshotId> GetSuitableSnapshotForRestore(
      const SnapshotScheduleId& schedule_id, HybridTime restore_at, int64_t leader_term,
      CoarseTimePoint deadline);
  Result<bool> IsTableCoveredBySomeSnapshotSchedule(const TableInfo& table_info);

  // Returns true if there are one or more non-deleted
  // snapshot schedules present.
  bool IsPitrActive();

  Result<bool> IsTableUndergoingPitrRestore(const TableInfo& table_info);

  void Start();

  void Shutdown();

  // If snapshot_id is nil then returns true if any snapshot covers the particular tablet
  // whereas if snapshot_id is not nil then returns true if that particular snapshot
  // covers the tablet.
  bool TEST_IsTabletCoveredBySnapshot(
      const TabletId& tablet_id,
      const TxnSnapshotId& snapshot_id = TxnSnapshotId(Uuid::Nil())) const;

  Status PopulateDeleteRetainerInfoForTableDrop(
      const TableInfo& table_info, const TabletInfos& tablets_to_check,
      const SnapshotSchedulesToObjectIdsMap& schedules_to_tables_map,
      TabletDeleteRetainerInfo& delete_retainer) const;
  Status PopulateDeleteRetainerInfoForTabletDrop(
      const TabletInfo& tablet_info, TabletDeleteRetainerInfo& delete_retainer) const;

  bool ShouldRetainHiddenTablet(
      const TabletInfo& tablet_info,
      const ScheduleMinRestoreTime& schedule_to_min_restore_time) const;

  bool ShouldRetainHiddenColocatedTable(
      const TableInfo& table_info, const TabletInfo& tablet_info,
      const ScheduleMinRestoreTime& schedule_to_min_restore_time) const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace master
} // namespace yb
