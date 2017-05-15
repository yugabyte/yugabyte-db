//
// Copyright (c) YugaByte, Inc.
//

#include "yb/rpc/rpc_call.h"

#include "yb/util/status.h"

namespace yb {
namespace rpc {

void RpcCall::Transferred(const Status& status) {
  CHECK(state_ == TransferState::PENDING) << "Aborted in invalid state: "
                                          << yb::rpc::ToString(state_)
                                          << ", status: " << status.ToString();
  state_ = status.ok() ? TransferState::FINISHED : TransferState::ABORTED;
  NotifyTransferred(status);
}

} // namespace rpc
} // namespace yb
