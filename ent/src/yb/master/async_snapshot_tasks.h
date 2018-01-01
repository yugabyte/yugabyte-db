// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_MASTER_ASYNC_SNAPSHOT_TASKS_H
#define ENT_SRC_YB_MASTER_ASYNC_SNAPSHOT_TASKS_H

#include "yb/master/async_ts_rpc_tasks.h"
#include "yb/tserver/backup.pb.h"

namespace yb {
namespace master {

// Send the "Create/Restore/.. Tablet Snapshot operation" to the leader replica for the tablet.
// Keeps retrying until we get an "ok" response.
class AsyncTabletSnapshotOp : public enterprise::RetryingTSRpcTask {
 public:
  AsyncTabletSnapshotOp(Master *master,
                        ThreadPool* callback_pool,
                        const scoped_refptr<TabletInfo>& tablet,
                        const std::string& snapshot_id,
                        tserver::TabletSnapshotOpRequestPB::Operation op);

  Type type() const override { return ASYNC_SNAPSHOT_OP; }

  std::string type_name() const override { return "Tablet Snapshot Operation"; }

  std::string description() const override;

 private:
  TabletId tablet_id() const override;
  TabletServerId permanent_uuid() const;

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

  scoped_refptr<TabletInfo> tablet_;
  const std::string snapshot_id_;
  tserver::TabletSnapshotOpRequestPB::Operation operation_;
  tserver::TabletSnapshotOpResponsePB resp_;
};

} // namespace master
} // namespace yb

#endif // ENT_SRC_YB_MASTER_ASYNC_SNAPSHOT_TASKS_H
