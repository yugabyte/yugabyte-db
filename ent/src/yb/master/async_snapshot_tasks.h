// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_MASTER_ASYNC_SNAPSHOT_TASKS_H
#define ENT_SRC_YB_MASTER_ASYNC_SNAPSHOT_TASKS_H

#include "yb/master/async_ts_rpc_tasks.h"
#include "yb/tserver/backup.pb.h"

namespace yb {
namespace master {

// Send the "Create Tablet Snapshot" to the leader replica for the tablet.
// Keeps retrying until we get an "ok" response.
class AsyncCreateTabletSnapshot : public enterprise::RetryingTSRpcTask {
 public:
  AsyncCreateTabletSnapshot(Master *master,
                            ThreadPool* callback_pool,
                            const scoped_refptr<TabletInfo>& tablet,
                            const std::string& snapshot_id);

  Type type() const override { return ASYNC_CREATE_SNAPSHOT; }

  std::string type_name() const override { return "Create Tablet Snapshot"; }

  std::string description() const override;

 private:
  std::string tablet_id() const override;
  std::string permanent_uuid() const;

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

  scoped_refptr<TabletInfo> tablet_;
  const std::string snapshot_id_;
  tserver::CreateTabletSnapshotResponsePB resp_;
};

} // namespace master
} // namespace yb

#endif // ENT_SRC_YB_MASTER_ASYNC_SNAPSHOT_TASKS_H
