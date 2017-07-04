// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_MASTER_ASYNC_TS_RPC_TASKS_H
#define ENT_SRC_YB_MASTER_ASYNC_TS_RPC_TASKS_H

#include "../../../../src/yb/master/async_rpc_tasks.h"

namespace yb {

namespace tserver {
class TabletServerBackupServiceProxy;
}

namespace master {
namespace enterprise {

class RetryingTSRpcTask : public yb::master::RetryingTSRpcTask {
  typedef yb::master::RetryingTSRpcTask super;
 public:
  RetryingTSRpcTask(Master *master,
                    ThreadPool* callback_pool,
                    TSPicker* picker,
                    const scoped_refptr<TableInfo>& table)
      : super(master, callback_pool, gscoped_ptr<TSPicker>(picker), table) {}

 protected:
  Status ResetTSProxy() override;

  std::shared_ptr<tserver::TabletServerBackupServiceProxy> ts_backup_proxy_;
};

} // namespace enterprise
} // namespace master
} // namespace yb

#endif // ENT_SRC_YB_MASTER_ASYNC_TS_RPC_TASKS_H
