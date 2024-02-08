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

#include "yb/master/test_async_rpc_manager.h"

namespace yb {
namespace master {

class Master;
class CatalogManager;

Status TestAsyncRpcManager::SendMasterTestRetryRequest(
    const consensus::RaftPeerPB& peer, const int32_t num_retries, StdStatusCallback callback) {
  auto task = std::make_shared<AsyncMasterTestRetry>(
      master_, catalog_manager_->AsyncTaskPool(), peer, num_retries, std::move(callback));
  return catalog_manager_->ScheduleTask(task);
}

Status TestAsyncRpcManager::SendTsTestRetryRequest(
    const PeerId& ts_id, int32_t num_retries, StdStatusCallback callback) {
  auto task = std::make_shared<AsyncTsTestRetry>(
      master_, catalog_manager_->AsyncTaskPool(), ts_id, num_retries, std::move(callback));
  return catalog_manager_->ScheduleTask(task);
}

Status TestAsyncRpcManager::TestRetry(
    const master::TestRetryRequestPB* req, master::TestRetryResponsePB* resp) {
  auto uuid = master_->sys_catalog().tablet_peer()->permanent_uuid();
  if (req->dest_uuid() != uuid) {
    return STATUS_FORMAT(
        InvalidArgument, "Requested uuid $0 did not match actual uuid $1", req->dest_uuid(), uuid);
  }
  auto num_calls = num_test_retry_calls_.fetch_add(1) + 1;
  if (num_calls < req->num_retries()) {
    return SetupError(
        resp->mutable_error(),
        STATUS_FORMAT(TryAgain, "Got $0 calls of $1", num_calls, req->num_retries()));
  }
  return Status::OK();
}

} // namespace master
} // namespace yb
