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

namespace yb {
namespace rpc {

void RpcCall::Transferred(const Status& status, Connection* conn) {
  if (state_ != TransferState::PENDING) {
    LOG(DFATAL) << __PRETTY_FUNCTION__ << " executed more than once on call "
                << static_cast<void*>(this) << ", current state: "
                << yb::ToString(state_) << " (expected to be PENDING)" << ", "
                << " status passed to the latest Transferred call: " << status << ", "
                << " this RpcCall: " << ToString();
    return;
  }
  state_ = status.ok() ? TransferState::FINISHED : TransferState::ABORTED;
  NotifyTransferred(status, conn);
}

} // namespace rpc
} // namespace yb
