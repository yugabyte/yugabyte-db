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

#ifndef ENT_SRC_YB_MASTER_ASYNC_SNAPSHOT_TASKS_H
#define ENT_SRC_YB_MASTER_ASYNC_SNAPSHOT_TASKS_H

#include "yb/common/hybrid_time.h"

#include "yb/master/async_ts_rpc_tasks.h"
#include "yb/master/snapshot_coordinator_context.h"

#include "yb/tserver/backup.pb.h"

namespace yb {
namespace master {

// Send the "Create/Restore/.. Tablet Snapshot operation" to the leader replica for the tablet.
// Keeps retrying until we get an "ok" response.
class AsyncTabletSnapshotOp : public enterprise::RetryingTSRpcTask {
 public:
  AsyncTabletSnapshotOp(
      Master* master,
      ThreadPool* callback_pool,
      const scoped_refptr<TabletInfo>& tablet,
      const std::string& snapshot_id,
      tserver::TabletSnapshotOpRequestPB::Operation op);

  Type type() const override { return ASYNC_SNAPSHOT_OP; }

  std::string type_name() const override { return "Tablet Snapshot Operation"; }

  std::string description() const override;

  void SetSnapshotScheduleId(const SnapshotScheduleId& id) {
    snapshot_schedule_id_ = id;
  }

  void SetSnapshotHybridTime(HybridTime value) {
    snapshot_hybrid_time_ = value;
  }

  void SetMetadata(uint32_t schema_version, const SchemaPB& schema,
                   const google::protobuf::RepeatedPtrField<IndexInfoPB>& indexes) {
    has_metadata_ = true;
    schema_version_ = schema_version;
    schema_ = schema;
    indexes_ = indexes;
  }

  void SetCallback(TabletSnapshotOperationCallback callback) {
    callback_ = std::move(callback);
  }

 private:
  TabletId tablet_id() const override;
  TabletServerId permanent_uuid() const;

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;
  void Finished(const Status& status) override;

  scoped_refptr<TabletInfo> tablet_;
  const std::string snapshot_id_;
  tserver::TabletSnapshotOpRequestPB::Operation operation_;
  SnapshotScheduleId snapshot_schedule_id_ = SnapshotScheduleId::Nil();
  HybridTime snapshot_hybrid_time_;
  tserver::TabletSnapshotOpResponsePB resp_;
  TabletSnapshotOperationCallback callback_;
  bool has_metadata_ = false;
  uint32_t schema_version_;
  SchemaPB schema_;
  google::protobuf::RepeatedPtrField<IndexInfoPB> indexes_;
};

} // namespace master
} // namespace yb

#endif // ENT_SRC_YB_MASTER_ASYNC_SNAPSHOT_TASKS_H
