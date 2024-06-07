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

#pragma once

#include "yb/common/hybrid_time.h"

#include "yb/master/async_rpc_tasks.h"
#include "yb/master/snapshot_coordinator_context.h"

#include "yb/tserver/backup.pb.h"

namespace yb {
namespace master {

// Send the "Create/Restore/.. Tablet Snapshot operation" to the leader replica for the tablet.
// Keeps retrying until we get an "ok" response.
class AsyncTabletSnapshotOp : public RetryingTSRpcTaskWithTable {
 public:
  AsyncTabletSnapshotOp(
      Master* master,
      ThreadPool* callback_pool,
      const TabletInfoPtr& tablet,
      const std::string& snapshot_id,
      tserver::TabletSnapshotOpRequestPB::Operation op,
      LeaderEpoch epoch);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kSnapshotOp;
  }

  std::string type_name() const override { return "Tablet Snapshot Operation"; }

  std::string description() const override;

  void SetSnapshotScheduleId(const SnapshotScheduleId& id) {
    snapshot_schedule_id_ = id;
  }

  void SetSnapshotHybridTime(HybridTime value) {
    snapshot_hybrid_time_ = value;
  }

  void SetMetadata(const SysTablesEntryPB& pb);

  void SetRestorationId(const TxnSnapshotRestorationId& id) {
    restoration_id_ = id;
  }

  void SetRestorationTime(HybridTime value) {
    restoration_hybrid_time_ = value;
  }

  void SetCallback(TabletSnapshotOperationCallback callback) {
    callback_ = std::move(callback);
  }

  void SetDbOid(int64_t db_oid) {
    db_oid_ = db_oid;
  }

  void SetColocatedTableMetadata(const TableId& table_id, const SysTablesEntryPB& pb);

 private:
  TabletId tablet_id() const override;
  TabletServerId permanent_uuid() const;

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;
  std::optional<std::pair<server::MonitoredTaskState, Status>> HandleReplicaLookupFailure(
      const Status& replica_lookup_status) override;
  void Finished(const Status& status) override;
  bool RetryAllowed(tserver::TabletServerErrorPB::Code code, const Status& status);

  TabletInfoPtr tablet_;
  const std::string snapshot_id_;
  tserver::TabletSnapshotOpRequestPB::Operation operation_;
  SnapshotScheduleId snapshot_schedule_id_ = SnapshotScheduleId::Nil();
  HybridTime snapshot_hybrid_time_;
  TxnSnapshotRestorationId restoration_id_ = TxnSnapshotRestorationId::Nil();
  HybridTime restoration_hybrid_time_;
  tserver::TabletSnapshotOpResponsePB resp_;
  TabletSnapshotOperationCallback callback_;
  bool has_metadata_ = false;
  uint32_t schema_version_;
  SchemaPB schema_;
  google::protobuf::RepeatedPtrField<IndexInfoPB> indexes_;
  bool hide_ = false;
  std::optional<int64_t> db_oid_ = std::nullopt;
  google::protobuf::RepeatedPtrField<tserver::TableMetadataPB> colocated_tables_metadata_;
};

} // namespace master
} // namespace yb
