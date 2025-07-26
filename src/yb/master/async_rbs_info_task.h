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

#include "yb/master/async_rpc_tasks_base.h"
#include "yb/master/async_task_result_collector.h"

#include "yb/tserver/tserver_admin.pb.h"

#include "yb/util/countdown_latch.h"

namespace yb {
namespace master {

class AsyncGetActiveRbsInfoTask : public RetrySpecificTSRpcTask {
 public:
  AsyncGetActiveRbsInfoTask(
      Master* master, ThreadPool* callback_pool, const std::string& permanent_uuid,
      std::shared_ptr<TaskResultCollector<tserver::GetActiveRbsInfoResponsePB>> aggregator);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kGetActiveRbsInfo;
  }

  std::string type_name() const override;
  std::string description() const override;

 protected:
  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;
  void Finished(const Status& status) override;
  TabletId tablet_id() const override { return TabletId(); } // Not associated with a tablet.

 private:
  tserver::GetActiveRbsInfoRequestPB req_;
  tserver::GetActiveRbsInfoResponsePB resp_;
  std::shared_ptr<TaskResultCollector<tserver::GetActiveRbsInfoResponsePB>> aggregator_;
};

std::unordered_map<TabletServerId, tserver::GetActiveRbsInfoResponsePB>
    FetchRbsInfo(Master* master, CoarseDuration timeout);

} // namespace master
} // namespace yb
