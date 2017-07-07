//
// Copyright (c) YugaByte, Inc.
//

#include "yb/rpc/rpc_with_call_id.h"

#include "yb/rpc/rpc_introspection.pb.h"

namespace yb {
namespace rpc {

void ConnectionContextWithCallId::DumpPB(const DumpRunningRpcsRequestPB& req,
                                         RpcConnectionPB* resp) {
  for (const auto &entry : calls_being_handled_) {
    entry.second->DumpPB(req, resp->add_calls_in_flight());
  }
}

bool ConnectionContextWithCallId::Idle() {
  return calls_being_handled_.empty();
}

Status ConnectionContextWithCallId::Store(InboundCall* call) {
  uint64_t call_id = ExtractCallId(call);
  if (!calls_being_handled_.emplace(call_id, call).second) {
    LOG(WARNING) << call->connection()->ToString() << ": received call ID " << call_id
                 << " but was already processing this ID! Ignoring";
    return STATUS_FORMAT(NetworkError, "Received duplicate call id: $0", call_id);
  }
  return Status::OK();
}

void ConnectionContextWithCallId::CallProcessed(InboundCall* call) {
  auto id = ExtractCallId(call);
  auto it = calls_being_handled_.find(id);
  if (it == calls_being_handled_.end() || it->second != call) {
    std::string existing = it == calls_being_handled_.end() ? "<NONE>" : it->second->ToString();
    LOG(DFATAL) << "Processed call with invalid id: " << id << ", call: " << call->ToString()
                << ", existing: " << existing;
    return;
  }
  calls_being_handled_.erase(it);
}

void ConnectionContextWithCallId::QueueResponse(const ConnectionPtr& conn,
                                                InboundCallPtr call) {
  conn->QueueOutboundData(std::move(call));
}

} // namespace rpc
} // namespace yb
