//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_RPC_RPC_WITH_CALL_ID_H
#define YB_RPC_RPC_WITH_CALL_ID_H

#include <functional>

#include "yb/rpc/connection.h"
#include "yb/rpc/inbound_call.h"

namespace yb {
namespace rpc {

class ConnectionContextWithCallId : public ConnectionContext {
 protected:
  InboundCall::CallProcessedListener call_processed_listener() {
    return std::bind(&ConnectionContextWithCallId::CallProcessed, this, std::placeholders::_1);
  }

  CHECKED_STATUS Store(InboundCall* call);
 private:
  bool ReadyToStop() override { return calls_being_handled_.empty(); }
  virtual uint64_t ExtractCallId(InboundCall* call) = 0;

  void DumpPB(const DumpRunningRpcsRequestPB& req, RpcConnectionPB* resp) override;
  bool Idle() override;

  void CallProcessed(InboundCall* call);

  // Calls which have been received on the server and are currently
  // being handled.
  std::unordered_map<uint64_t, InboundCall*> calls_being_handled_;
};

} // namespace rpc
} // namespace yb

#endif // YB_RPC_RPC_WITH_CALL_ID_H
