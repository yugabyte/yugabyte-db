// Copyright (c) YugaByte, Inc.

#ifndef YB_RPC_RPC_CALL_H
#define YB_RPC_RPC_CALL_H

#include "yb/gutil/ref_counted.h"

#include "yb/util/enums.h"

#include "yb/rpc/outbound_data.h"

namespace yb {

class Status;

namespace rpc {

YB_DEFINE_ENUM(TransferState, (PENDING)(FINISHED)(ABORTED));

class RpcCall : public OutboundData {
 public:
  // This functions is invoked in reactor thread of appropriate connection.
  // So it doesn't require synchronization.
  void Transferred(const Status& status) override;

 private:
  virtual void NotifyTransferred(const Status& status) = 0;

  TransferState state_ = TransferState::PENDING;
};

}  // namespace rpc
}  // namespace yb

#endif // YB_RPC_RPC_CALL_H
