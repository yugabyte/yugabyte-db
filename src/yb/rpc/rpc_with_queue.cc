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

#include "yb/gutil/casts.h"

#include "yb/rpc/connection.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/rpc_introspection.pb.h"

#include "yb/util/string_util.h"

namespace yb {
namespace rpc {

ConnectionContextWithQueue::ConnectionContextWithQueue(
    size_t max_concurrent_calls,
    size_t max_queued_bytes)
    : max_concurrent_calls_(max_concurrent_calls), max_queued_bytes_(max_queued_bytes) {
}

ConnectionContextWithQueue::~ConnectionContextWithQueue() {
}

void ConnectionContextWithQueue::DumpPB(const DumpRunningRpcsRequestPB& req,
                                        RpcConnectionPB* resp) {
  for (auto& call : calls_queue_) {
    call->DumpPB(req, resp->add_calls_in_flight());
  }
}

bool ConnectionContextWithQueue::Idle(std::string* reason_not_idle) {
  if (calls_queue_.empty()) {
    return true;
  }

  if (reason_not_idle) {
    AppendWithSeparator(Format("$0 calls", calls_queue_.size()), reason_not_idle);
  }

  return false;
}

void ConnectionContextWithQueue::Enqueue(std::shared_ptr<QueueableInboundCall> call) {
  auto reactor = call->connection()->reactor();
  DCHECK(reactor->IsCurrentThread());

  calls_queue_.push_back(call);
  queued_bytes_ += call->weight_in_bytes();

  size_t size = calls_queue_.size();
  if (size == replies_being_sent_ + 1) {
    first_without_reply_.store(call.get(), std::memory_order_release);
  }
  if (size <= max_concurrent_calls_) {
    reactor->messenger().Handle(call, Queue::kTrue);
  }
}

void ConnectionContextWithQueue::Shutdown(const Status& status) {
  // Could erase calls, that we did not start to process yet.
  if (calls_queue_.size() > max_concurrent_calls_) {
    calls_queue_.erase(calls_queue_.begin() + max_concurrent_calls_, calls_queue_.end());
  }

  for (auto& call : calls_queue_) {
    call->Abort(status);
  }
}

void ConnectionContextWithQueue::CallProcessed(InboundCall* call) {
  ++processed_call_count_;
  auto reactor = call->connection()->reactor();
  DCHECK(reactor->IsCurrentThread());

  DCHECK(!calls_queue_.empty());
  DCHECK_EQ(calls_queue_.front().get(), call);
  DCHECK_GT(replies_being_sent_, 0);

  bool could_enqueue = can_enqueue();
  auto call_weight_in_bytes = down_cast<QueueableInboundCall*>(call)->weight_in_bytes();
  queued_bytes_ -= call_weight_in_bytes;

  calls_queue_.pop_front();
  --replies_being_sent_;
  if (calls_queue_.size() >= max_concurrent_calls_) {
    auto call_ptr = calls_queue_[max_concurrent_calls_ - 1];
    reactor->messenger().Handle(call_ptr, Queue::kTrue);
  }
  if (Idle() && idle_listener_) {
    idle_listener_();
  }

  if (!could_enqueue && can_enqueue()) {
    call->connection()->ParseReceived();
  }
}

Status ConnectionContextWithQueue::QueueResponse(const ConnectionPtr& conn, InboundCallPtr call) {
  QueueableInboundCall* queueable_call = down_cast<QueueableInboundCall*>(call.get());
  queueable_call->SetHasReply();
  if (queueable_call == first_without_reply_.load(std::memory_order_acquire)) {
    return conn->reactor()->ScheduleReactorTask(flush_outbound_queue_task_);
  }
  return Status::OK();
}

void ConnectionContextWithQueue::FlushOutboundQueue(Connection* conn) {
  conn->reactor()->CheckCurrentThread();
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

Status ConnectionContextWithQueue::AssignConnection(const ConnectionPtr& conn) {
  flush_outbound_queue_task_ = MakeFunctorReactorTask(
      std::bind(&ConnectionContextWithQueue::FlushOutboundQueue, this, conn.get()), conn,
      SOURCE_LOCATION());
  return Status::OK();
}

} // namespace rpc
} // namespace yb
