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

#include "yb/rpc/rpc_with_queue.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/rpc_introspection.pb.h"

namespace yb {
namespace rpc {

ConnectionContextWithQueue::ConnectionContextWithQueue(size_t max_concurrent_calls)
    : max_concurrent_calls_(max_concurrent_calls) {
}

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

void ConnectionContextWithQueue::Enqueue(std::shared_ptr<QueueableInboundCall> call) {
  auto reactor = call->connection()->reactor();
  DCHECK(reactor->IsCurrentThread());

  calls_queue_.push_back(call);
  size_t size = calls_queue_.size();
  if (size == replies_being_sent_ + 1) {
    first_without_reply_.store(call.get(), std::memory_order_release);
  }
  if (size <= max_concurrent_calls_) {
    reactor->messenger()->QueueInboundCall(call);
  }
}

void ConnectionContextWithQueue::CallProcessed(InboundCall* call) {
  auto reactor = call->connection()->reactor();
  DCHECK(reactor->IsCurrentThread());

  DCHECK(!calls_queue_.empty());
  DCHECK_EQ(calls_queue_.front().get(), call);
  DCHECK_GT(replies_being_sent_, 0);

  calls_queue_.pop_front();
  --replies_being_sent_;
  if (calls_queue_.size() >= max_concurrent_calls_) {
    auto call_ptr = calls_queue_[max_concurrent_calls_ - 1];
    reactor->messenger()->QueueInboundCall(call_ptr);
  }
}

void ConnectionContextWithQueue::QueueResponse(const ConnectionPtr& conn,
                                               InboundCallPtr call) {
  QueueableInboundCall* queueable_call = down_cast<QueueableInboundCall*>(call.get());
  queueable_call->SetHasReply();
  if (queueable_call == first_without_reply_.load(std::memory_order_acquire)) {
    conn->reactor()->ScheduleReactorTask(flush_outbound_queue_task_);
  }
}

void ConnectionContextWithQueue::FlushOutboundQueue(Connection* conn) {
  DCHECK(conn->reactor()->IsCurrentThread());

  const size_t begin = replies_being_sent_;
  size_t end = begin;
  for (;;) {
    size_t queue_size = calls_queue_.size();
    while (end < queue_size) {
      if (!calls_queue_[end]->has_reply()) {
        break;
      }
      ++end;
    }
    auto new_first_without_reply = end < queue_size ? calls_queue_[end].get() : nullptr;
    first_without_reply_.store(new_first_without_reply, std::memory_order_release);
    // It is usual case that we break here, but sometimes there could happen that before updating
    // first_without_reply_ we did QueueResponse for this call.
    if (!new_first_without_reply || !new_first_without_reply->has_reply()) {
      break;
    }
  }

  if (begin != end) {
    replies_being_sent_ = end;
    boost::container::small_vector<OutboundDataPtr, 64> batch(
        calls_queue_.begin() + begin,
        calls_queue_.begin() + end);
    conn->QueueOutboundDataBatch(batch);
  }
}

void ConnectionContextWithQueue::AssignConnection(const ConnectionPtr& conn) {
  flush_outbound_queue_task_ = MakeFunctorReactorTask(
      std::bind(&ConnectionContextWithQueue::FlushOutboundQueue, this, conn.get()),
      conn);
}

} // namespace rpc
} // namespace yb
