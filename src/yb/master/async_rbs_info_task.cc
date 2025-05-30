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

#include "yb/master/async_rbs_info_task.h"

#include "yb/common/wire_protocol.h"

#include "yb/master/master.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"

#include "yb/tserver/tserver_admin.proxy.h"

using namespace std::chrono_literals;

namespace yb {
namespace master {

AsyncGetActiveRbsInfoTask::AsyncGetActiveRbsInfoTask(
    Master* master, ThreadPool* callback_pool, const std::string& permanent_uuid,
    std::shared_ptr<TaskResultCollector<tserver::GetActiveRbsInfoResponsePB>> aggregator)
    : RetrySpecificTSRpcTask(master, callback_pool, permanent_uuid),
      aggregator_(std::move(aggregator)) {}

std::string AsyncGetActiveRbsInfoTask::type_name() const {
  return "Get Ongoing RBS Info";
}

std::string AsyncGetActiveRbsInfoTask::description() const {
  return "Async Get Ongoing RBS Info RPC";
}

void AsyncGetActiveRbsInfoTask::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    TransitionToFailedState(state(), StatusFromPB(resp_.error().status()));
  } else {
    TransitionToCompleteState();
  }
}

void AsyncGetActiveRbsInfoTask::Finished(const Status& status) {
  aggregator_->TrackResponse(permanent_uuid_, std::move(resp_));
}

bool AsyncGetActiveRbsInfoTask::SendRequest(int attempt) {
  ts_admin_proxy_->GetActiveRbsInfoAsync(req_, &resp_, &rpc_, BindRpcCallback());
  return true;
}

std::unordered_map<TabletServerId, tserver::GetActiveRbsInfoResponsePB>
    FetchRbsInfo(Master* master, CoarseDuration timeout) {
  TSDescriptorVector ts_descs;
  auto* catalog_manager = master->catalog_manager();
  master->ts_manager()->GetAllLiveDescriptors(&ts_descs);

  auto aggregator =
      TaskResultCollector<tserver::GetActiveRbsInfoResponsePB>::Create(ts_descs.size());
  for (const auto& ts_desc : ts_descs) {
    auto task = std::make_shared<AsyncGetActiveRbsInfoTask>(
        master, catalog_manager->AsyncTaskPool(), ts_desc->permanent_uuid(), aggregator);
    auto s = catalog_manager->ScheduleTask(task);
    if (!s.ok()) {
      LOG(WARNING) << "Failed to schedule AsyncGetActiveRbsInfoTask: " << s;
    }
  }
  return aggregator->TakeResponses(timeout);
}

} // namespace master
} // namespace yb
