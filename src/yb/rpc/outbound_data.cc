//
// Copyright (c) YugaByte, Inc.
//

#include "yb/rpc/outbound_data.h"

namespace yb {
namespace rpc {

void OutboundData::TransferFinished() {
  if (state_ == TransferState::PENDING) {
    state_ = TransferState::FINISHED;
    NotifyTransferFinished();
  }
}

void OutboundData::TransferAborted(const Status& status) {
  CHECK(state_ != TransferState::ABORTED) << "Already aborted";
  CHECK(state_ == TransferState::PENDING) << "Cannot abort a finished transfer";
  NotifyTransferAborted(status);
  state_ = TransferState::ABORTED;
}

} // namespace rpc
} // namespace yb
