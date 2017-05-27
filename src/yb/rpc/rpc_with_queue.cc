//
// Copyright (c) YugaByte, Inc.
//

#include "yb/rpc/rpc_with_queue.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/rpc_introspection.pb.h"

namespace yb {
namespace rpc {

ConnectionContextWithQueue::~ConnectionContextWithQueue() {
}

void ConnectionContextWithQueue::DumpPB(const DumpRunningRpcsRequestPB& req,
                                        RpcConnectionPB* resp) {
  for (auto& call : calls_queue_) {
    call->DumpPB(req, resp->add_calls_in_flight());
  }
}

bool ConnectionContextWithQueue::Idle() {
  return calls_queue_.empty();
}

void ConnectionContextWithQueue::Enqueue(InboundCallPtr call) {
  auto reactor = call->connection()->reactor();
  DCHECK(reactor->IsCurrentThread());

  calls_queue_.push_back(call);
  if (calls_queue_.size() == 1) {
    reactor->messenger()->QueueInboundCall(call);
  }
}

void ConnectionContextWithQueue::CallProcessed(InboundCall* call) {
  DCHECK(!calls_queue_.empty() && calls_queue_.front().get() == call);
  auto reactor = call->connection()->reactor();
  DCHECK(reactor->IsCurrentThread());

  calls_queue_.pop_front();
  if (!calls_queue_.empty()) {
    auto call_ptr = calls_queue_.front();
    reactor->messenger()->QueueInboundCall(call_ptr);
  }
}

} // namespace rpc
} // namespace yb
