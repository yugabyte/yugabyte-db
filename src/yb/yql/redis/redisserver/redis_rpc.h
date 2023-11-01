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

#ifndef YB_YQL_REDIS_REDISSERVER_REDIS_RPC_H
#define YB_YQL_REDIS_REDISSERVER_REDIS_RPC_H

#include <stdint.h>

#include <type_traits>

#include <boost/container/small_vector.hpp>
#include <boost/version.hpp>

#include "yb/common/redis_protocol.pb.h"

#include "yb/rpc/connection_context.h"
#include "yb/rpc/growable_buffer.h"
#include "yb/rpc/rpc_with_queue.h"
#include "yb/rpc/reactor_thread_role.h"

#include "yb/util/net/net_fwd.h"
#include "yb/util/size_literals.h"

#include "yb/yql/redis/redisserver/redis_fwd.h"

namespace yb {

class MemTracker;

namespace redisserver {

class RedisParser;

YB_DEFINE_ENUM(RedisClientMode, (kNormal)(kSubscribed)(kMonitoring));

class RedisConnectionContext : public rpc::ConnectionContextWithQueue {
 public:
  RedisConnectionContext(
      rpc::GrowableBufferAllocator* allocator,
      const MemTrackerPtr& call_tracker);
  ~RedisConnectionContext();
  bool is_authenticated() const {
    return authenticated_.load(std::memory_order_acquire);
  }
  void set_authenticated(bool flag) {
    authenticated_.store(flag, std::memory_order_release);
  }

  std::string redis_db_to_use() const {
    return redis_db_name_;
  }

  void use_redis_db(const std::string& name) {
    redis_db_name_ = name;
  }

  static std::string Name() { return "Redis"; }

  RedisClientMode ClientMode() { return mode_.load(std::memory_order_acquire); }

  void SetClientMode(RedisClientMode mode) { mode_.store(mode, std::memory_order_release); }

  void SetCleanupHook(std::function<void()> hook) { cleanup_hook_ = std::move(hook); }

  // Shutdown this context. Clean up the subscriptions if any.
  void Shutdown(const Status& status) ON_REACTOR_THREAD override;

  Status ReportPendingWriteBytes(size_t bytes_in_queue) override;

 private:
  Status Connected(const rpc::ConnectionPtr& connection) override { return Status::OK(); }

  rpc::RpcConnectionPB::StateType State() override {
    return rpc::RpcConnectionPB::OPEN;
  }

  Result<rpc::ProcessCallsResult> ProcessCalls(
      const rpc::ConnectionPtr& connection,
      const IoVecs& bytes_to_process,
      rpc::ReadBufferFull read_buffer_full) ON_REACTOR_THREAD override;

  rpc::StreamReadBuffer& ReadBuffer() override {
    return read_buffer_;
  }

  // Takes ownership of data content.
  Status HandleInboundCall(const rpc::ConnectionPtr& connection,
                                   size_t commands_in_batch,
                                   rpc::CallData* data) ON_REACTOR_THREAD;

  std::unique_ptr<RedisParser> parser_;
  rpc::GrowableBuffer read_buffer_;
  size_t commands_in_batch_ = 0;
  size_t end_of_batch_ = 0;
  std::atomic<bool> authenticated_{false};
  std::string redis_db_name_ = "0";
  std::atomic<RedisClientMode> mode_{RedisClientMode::kNormal};
  CoarseTimePoint soft_limit_exceeded_since_{CoarseTimePoint::max()};
  std::function<void()> cleanup_hook_;

  MemTrackerPtr call_mem_tracker_;
};

class RedisInboundCall : public rpc::QueueableInboundCall {
 public:
  explicit RedisInboundCall(
     rpc::ConnectionPtr conn,
     size_t weight_in_bytes,
     CallProcessedListener* call_processed_listener);

  ~RedisInboundCall();
  // Takes ownership of data content.
  Status ParseFrom(const MemTrackerPtr& mem_tracker, size_t commands, rpc::CallData* data);

  // Serialize the response packet for the finished call.
  // The resulting slices refer to memory in this object.
  void DoSerialize(boost::container::small_vector_base<RefCntBuffer>* output) override;
  void GetCallDetails(rpc::RpcCallInProgressPB *call_in_progress_pb) const;
  void LogTrace() const override;
  std::string ToString() const override;
  bool DumpPB(const rpc::DumpRunningRpcsRequestPB& req, rpc::RpcCallInProgressPB* resp) override;

  CoarseTimePoint GetClientDeadline() const override;

  RedisClientBatch& client_batch() { return client_batch_; }
  RedisConnectionContext& connection_context() const;

  Slice serialized_remote_method() const override;
  Slice method_name() const override;

  static Slice static_serialized_remote_method();

  void Respond(size_t idx, bool is_success, RedisResponsePB* resp);

  void RespondFailure(rpc::ErrorStatusPB::RpcErrorCodePB error_code, const Status& status) override;

  void RespondFailure(size_t idx, const Status& status);
  void RespondSuccess(size_t idx,
                      const rpc::RpcMethodMetrics& metrics,
                      RedisResponsePB* resp);
  void MarkForClose() { quit_.store(true, std::memory_order_release); }

  size_t ObjectSize() const override { return sizeof(*this); }

  size_t DynamicMemoryUsage() const override {
    return QueueableInboundCall::DynamicMemoryUsage() +
           DynamicMemoryUsageOf(responses_, ready_, client_batch_);
  }

 private:

  // The connection on which this inbound call arrived.
  static constexpr size_t batch_capacity = RedisClientBatch::static_capacity;
  boost::container::small_vector<RedisResponsePB, batch_capacity> responses_;
  boost::container::small_vector<Atomic64, batch_capacity> ready_;
  std::atomic<size_t> ready_count_{0};
  std::atomic<bool> had_failures_{false};
  RedisClientBatch client_batch_;

  // Atomic bool to indicate if the command batch has been parsed.
  std::atomic<bool> parsed_ = {false};

  // Atomic bool to indicate if the quit command is present
  std::atomic<bool> quit_ = {false};

  ScopedTrackedConsumption consumption_;
};

} // namespace redisserver
} // namespace yb

#endif // YB_YQL_REDIS_REDISSERVER_REDIS_RPC_H
