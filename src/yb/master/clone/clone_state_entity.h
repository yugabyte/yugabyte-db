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
#include "yb/common/snapshot.h"
#include "yb/master/catalog_entity_base.h"
#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/sys_catalog.h"

namespace yb::master {

struct PersistentCloneStateInfo : public Persistent<SysCloneStatePB> {};

struct CloneStateInfoHelpers {
  static bool IsDone(const SysCloneStatePB& pb) {
    return pb.aggregate_state() == SysCloneStatePB::COMPLETE ||
           pb.aggregate_state() == SysCloneStatePB::ABORTED;
  }
};

class CloneStateInfo : public MetadataCowWrapper<PersistentCloneStateInfo> {
 public:
  struct ColocatedTableData {
    TableId new_table_id;
    SysTablesEntryPB table_entry_pb;
    int new_schema_version;
  };

  struct TabletData {
    TabletId source_tablet_id;
    TabletId target_tablet_id;
    // The correct schema version and SysTablesEntryPB of every colocated table
    std::vector<ColocatedTableData> colocated_tables_data;
  };

  explicit CloneStateInfo(std::string id);

  virtual const std::string& id() const override { return clone_request_id_; };

  void Load(const SysCloneStatePB& metadata) override;

  std::vector<TabletData> GetTabletData();
  void AddTabletData(CloneStateInfo::TabletData tablet_data);

  YQLDatabase DatabaseType();
  void SetDatabaseType(YQLDatabase database_type);

  LeaderEpoch Epoch();
  void SetEpoch(const LeaderEpoch& epoch);

  const TxnSnapshotId& SourceSnapshotId();
  void SetSourceSnapshotId(const TxnSnapshotId& source_snapshot_id);

  const TxnSnapshotId& TargetSnapshotId();
  void SetTargetSnapshotId(const TxnSnapshotId& target_snapshot_id);

  const TxnSnapshotRestorationId& RestorationId();
  void SetRestorationId(const TxnSnapshotRestorationId& restoration_id);

 private:
  // The ID field is used in the sys_catalog table.
  const std::string clone_request_id_;

  LeaderEpoch epoch_ GUARDED_BY(mutex_);
  YQLDatabase database_type_ GUARDED_BY(mutex_);

  // These fields are set before the clone state is set to CREATING.
  std::vector<TabletData> tablet_data_ GUARDED_BY(mutex_);
  TxnSnapshotId source_snapshot_id_ GUARDED_BY(mutex_) = TxnSnapshotId::Nil();
  TxnSnapshotId target_snapshot_id_ GUARDED_BY(mutex_) = TxnSnapshotId::Nil();

  // This is set before the clone state is set to RESTORING.
  TxnSnapshotRestorationId restoration_id_ GUARDED_BY(mutex_) = TxnSnapshotRestorationId::Nil();

  std::mutex mutex_;

  DISALLOW_COPY_AND_ASSIGN(CloneStateInfo);
};

struct CloneStateInfoComparator {
    bool operator()(const CloneStateInfoPtr& a, const CloneStateInfoPtr& b) const {
        return a->LockForRead()->pb.clone_request_seq_no() <
               b->LockForRead()->pb.clone_request_seq_no();
    }
};

DECLARE_MULTI_INSTANCE_LOADER_CLASS(CloneState, std::string, SysCloneStatePB);

} // namespace yb::master
