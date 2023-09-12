//
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
//
#include "yb/rpc/rpc_call.h"

#include "yb/util/status.h"
#include "yb/util/logging.h"

namespace yb {
namespace rpc {

void RpcCall::Transferred(const Status& status, const ConnectionPtr& conn) {
  auto transfer_state = transfer_state_.load(std::memory_order_acquire);
  for (;;) {
    if (transfer_state != TransferState::PENDING) {
      LOG_WITH_PREFIX(DFATAL) << __PRETTY_FUNCTION__ << " executed more than once on call "
                              << static_cast<void*>(this) << ", current state: "
                              << yb::ToString(transfer_state) << " (expected to be PENDING), "
                              << "status passed to the current Transferred call: " << status << ", "
                              << "this RpcCall: " << ToString();
      return;
    }
    auto new_state = status.ok() ? TransferState::FINISHED : TransferState::ABORTED;
    if (transfer_state_.compare_exchange_strong(transfer_state, new_state)) {
      break;
    }
  }
  NotifyTransferred(status, conn);
}

} // namespace rpc
} // namespace yb
