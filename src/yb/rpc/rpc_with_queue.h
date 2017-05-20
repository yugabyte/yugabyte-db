//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_RPC_RPC_WITH_QUEUE_H
#define YB_RPC_RPC_WITH_QUEUE_H

#include <functional>

#include "yb/rpc/connection.h"
#include "yb/rpc/inbound_call.h"

namespace yb {
namespace rpc {

class ConnectionContextWithQueue : public ConnectionContext {
 protected:
  ~ConnectionContextWithQueue();

  InboundCall::CallProcessedListener call_processed_listener() {
    return std::bind(&ConnectionContextWithQueue::CallProcessed, this, std::placeholders::_1);
  }

  void Enqueue(InboundCallPtr call);
 private:
  void DumpPB(const DumpRunningRpcsRequestPB& req, RpcConnectionPB* resp) override;
  bool Idle() override;
  bool ReadyToStop() override { return calls_queue_.empty(); }

  void CallProcessed(InboundCall* call);

  std::deque<InboundCallPtr> calls_queue_;
};

} // namespace rpc
} // namespace yb

#endif // YB_RPC_RPC_WITH_QUEUE_H
