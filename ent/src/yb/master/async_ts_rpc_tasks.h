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
      : super(master, callback_pool, std::unique_ptr<TSPicker>(picker), table) {}

 protected:
  Status ResetTSProxy() override;

  std::shared_ptr<tserver::TabletServerBackupServiceProxy> ts_backup_proxy_;
};

} // namespace enterprise
} // namespace master
} // namespace yb

#endif // ENT_SRC_YB_MASTER_ASYNC_TS_RPC_TASKS_H
