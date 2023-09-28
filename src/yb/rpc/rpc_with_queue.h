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

#pragma once

#include <stdint.h>

#include <functional>
#include <mutex>
#include <type_traits>
#include <unordered_set>

#include "yb/rpc/connection_context.h"
#include "yb/rpc/inbound_call.h"
#include "yb/rpc/reactor_thread_role.h"

#include "yb/util/size_literals.h"

namespace yb {
namespace rpc {

class QueueableInboundCall : public InboundCall {
 public:
  QueueableInboundCall(ConnectionPtr conn, size_t weight_in_bytes,
                       CallProcessedListener* call_processed_listener)
      : InboundCall(std::move(conn), nullptr /* rpc_metrics */, call_processed_listener),
        weight_in_bytes_(weight_in_bytes) {}

  void SetHasReply() {
    has_reply_.store(true, std::memory_order_release);
  }

  bool has_reply() const {
    return has_reply_.load(std::memory_order_acquire);
  }

  void Abort(const Status& status) {
    aborted_.store(true, std::memory_order_release);
  }

  bool aborted() const {
    return aborted_.load(std::memory_order_acquire);
  }

  // Context with queue has limit on bytes used by queued commands.
  // `weight_in_bytes` function is used to determine how many bytes consumes this call.
  size_t weight_in_bytes() const { return weight_in_bytes_; }

 private:
  std::atomic<bool> has_reply_{false};
  std::atomic<bool> aborted_{false};
  const size_t weight_in_bytes_;
};

class ConnectionContextWithQueue : public ConnectionContextBase,
                                   public InboundCall::CallProcessedListener {
 protected:
  explicit ConnectionContextWithQueue(
      size_t max_concurrent_calls,
      size_t max_queued_bytes);

  ~ConnectionContextWithQueue();

  bool can_enqueue() const {
    return queued_bytes_ <= max_queued_bytes_;
  }

  void Enqueue(std::shared_ptr<QueueableInboundCall> call) ON_REACTOR_THREAD;

  uint64_t ProcessedCallCount() override {
    return processed_call_count_.load(std::memory_order_acquire);
  }

  void Shutdown(const Status& status) ON_REACTOR_THREAD override;

 private:
  Status AssignConnection(const ConnectionPtr& conn) override;
  void DumpPB(const DumpRunningRpcsRequestPB& req, RpcConnectionPB* resp)
      ON_REACTOR_THREAD override;
  bool Idle(std::string* reason_not_idle = nullptr)
      ON_REACTOR_THREAD override;
  Status QueueResponse(const ConnectionPtr& conn, InboundCallPtr call) override;
  void ListenIdle(IdleListener listener) override { idle_listener_ = std::move(listener); }

  void CallProcessed(InboundCall* call) ON_REACTOR_THREAD override;
  void FlushOutboundQueue(Connection* conn) ON_REACTOR_THREAD;
  void FlushOutboundQueueAborted(const Status& status);

  const size_t max_concurrent_calls_;
  const size_t max_queued_bytes_;
  size_t replies_being_sent_ = 0;
  size_t queued_bytes_ = 0;

  // Calls that are being processed by this connection/context.
  // At the top or queue there are replies_being_sent_ calls, for which we are sending reply.
  // After that there are calls that are being processed.
  // first_without_reply_ points to the first of them.
  // There are not more than max_concurrent_calls_ entries in first two groups.
  // After end of queue there are calls that we received but processing did not start for them.
  std::deque<std::shared_ptr<QueueableInboundCall>> calls_queue_
      GUARDED_BY_REACTOR_THREAD;

  std::shared_ptr<ReactorTask> flush_outbound_queue_task_;

  // First call that does not have reply yet.
  std::atomic<QueueableInboundCall*> first_without_reply_{nullptr};
  std::atomic<uint64_t> processed_call_count_{0};
  IdleListener idle_listener_;
};

} // namespace rpc
} // namespace yb
