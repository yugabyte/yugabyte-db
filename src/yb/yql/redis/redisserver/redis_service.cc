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

#include "yb/yql/redis/redisserver/redis_service.h"

#include <iostream>
#include <thread>

#include <boost/algorithm/string/case_conv.hpp>

#include <boost/lockfree/queue.hpp>

#include <boost/logic/tribool.hpp>

#include <gflags/gflags.h>

#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/client/async_rpc.h"
#include "yb/client/callbacks.h"
#include "yb/client/client.h"
#include "yb/client/client_builder-internal.h"
#include "yb/client/client-internal.h"
#include "yb/client/meta_cache.h"
#include "yb/client/yb_op.h"

#include "yb/common/redis_protocol.pb.h"

#include "yb/yql/redis/redisserver/redis_commands.h"
#include "yb/yql/redis/redisserver/redis_constants.h"
#include "yb/yql/redis/redisserver/redis_encoding.h"
#include "yb/yql/redis/redisserver/redis_parser.h"
#include "yb/yql/redis/redisserver/redis_rpc.h"

#include "yb/rpc/connection.h"
#include "yb/rpc/rpc_context.h"
#include "yb/rpc/rpc_introspection.pb.h"

#include "yb/tserver/tablet_server.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/locks.h"
#include "yb/util/logging.h"
#include "yb/util/memory/mc_types.h"
#include "yb/util/size_literals.h"
#include "yb/util/stol_utils.h"

using yb::operator"" _MB;
using namespace std::literals;
using namespace std::placeholders;

DEFINE_REDIS_histogram_EX(error,
                          "yb.redisserver.RedisServerService.AnyMethod RPC Time",
                          "yb.redisserver.RedisServerService.ErrorUnsupportedMethod()");
DEFINE_REDIS_histogram_EX(get_internal,
                          "yb.redisserver.RedisServerService.Get RPC Time",
                          "in yb.client.Get");
DEFINE_REDIS_histogram_EX(set_internal,
                          "yb.redisserver.RedisServerService.Set RPC Time",
                          "in yb.client.Set");

#define DEFINE_REDIS_SESSION_GAUGE(state) \
  METRIC_DEFINE_gauge_uint64( \
      server, \
      BOOST_PP_CAT(redis_, BOOST_PP_CAT(state, _sessions)), \
      "Number of " BOOST_PP_STRINGIZE(state) " sessions", \
      yb::MetricUnit::kUnits, \
      "Number of sessions " BOOST_PP_STRINGIZE(state) " by Redis service.") \
      /**/

DEFINE_REDIS_SESSION_GAUGE(allocated);
DEFINE_REDIS_SESSION_GAUGE(available);

METRIC_DEFINE_gauge_uint64(
    server, redis_monitoring_clients, "Number of clients running monitor", yb::MetricUnit::kUnits,
    "Number of clients running monitor ");

#if defined(THREAD_SANITIZER) || defined(ADDRESS_SANITIZER)
constexpr int32_t kDefaultRedisServiceTimeoutMs = 600000;
#else
constexpr int32_t kDefaultRedisServiceTimeoutMs = 3000;
#endif

DEFINE_int32(redis_service_yb_client_timeout_millis, kDefaultRedisServiceTimeoutMs,
             "Timeout in milliseconds for RPC calls from Redis service to master/tserver");

// In order to support up to three 64MB strings along with other strings,
// we have the total size of a redis command at 253_MB, which is less than the consensus size
// to account for the headers in the consensus layer.
DEFINE_int32(redis_max_command_size, 253_MB,
             "Maximum size of the command in redis");

// Maximum value size is 64MB
DEFINE_int32(redis_max_value_size, 64_MB,
             "Maximum size of the value in redis");
DEFINE_int32(redis_callbacks_threadpool_size, 64,
             "The maximum size for the threadpool which handles callbacks from the ybclient layer");

DEFINE_bool(redis_safe_batch, true, "Use safe batching with Redis service");
DEFINE_bool(enable_redis_auth, true, "Enable AUTH for the Redis service");

DECLARE_string(placement_cloud);
DECLARE_string(placement_region);
DECLARE_string(placement_zone);

using yb::client::YBRedisOp;
using yb::client::YBRedisReadOp;
using yb::client::YBRedisWriteOp;
using yb::client::YBClientBuilder;
using yb::client::YBSchema;
using yb::client::YBSession;
using yb::client::YBStatusCallback;
using yb::client::YBTableName;
using yb::rpc::ConnectionPtr;
using yb::rpc::ConnectionWeakPtr;
using yb::rpc::OutboundData;
using yb::rpc::OutboundDataPtr;
using yb::RedisResponsePB;

namespace yb {
namespace redisserver {

typedef boost::container::small_vector_base<Slice> RedisKeyList;

namespace {

YB_DEFINE_ENUM(OperationType, (kNone)(kRead)(kWrite)(kLocal));

// Returns opposite operation type for specified type. Write for read, read for write.
OperationType Opposite(OperationType type) {
  switch (type) {
    case OperationType::kRead:
      return OperationType::kWrite;
    case OperationType::kWrite:
      return OperationType::kRead;
    case OperationType::kNone: FALLTHROUGH_INTENDED;
    case OperationType::kLocal:
      FATAL_INVALID_ENUM_VALUE(OperationType, type);
  }
  FATAL_INVALID_ENUM_VALUE(OperationType, type);
}

class Operation {
 public:
  template <class Op>
  Operation(const std::shared_ptr<RedisInboundCall>& call,
            size_t index,
            std::shared_ptr<Op> operation,
            const rpc::RpcMethodMetrics& metrics)
    : type_(std::is_same<Op, YBRedisReadOp>::value ? OperationType::kRead : OperationType::kWrite),
      call_(call),
      index_(index),
      operation_(std::move(operation)),
      metrics_(metrics) {
    auto status = operation_->GetPartitionKey(&partition_key_);
    if (!status.ok()) {
      Respond(status);
    }
  }

  Operation(const std::shared_ptr<RedisInboundCall>& call,
            size_t index,
            std::function<bool(const StatusFunctor&)> functor,
            std::string partition_key,
            const rpc::RpcMethodMetrics& metrics)
    : type_(OperationType::kLocal),
      call_(call),
      index_(index),
      functor_(std::move(functor)),
      partition_key_(std::move(partition_key)),
      metrics_(metrics) {
  }

  bool responded() const {
    return responded_.load(std::memory_order_acquire);
  }

  size_t index() const {
    return index_;
  }

  OperationType type() const {
    return type_;
  }

  const YBRedisOp& operation() const {
    return *operation_;
  }

  bool has_operation() const {
    return operation_ != nullptr;
  }

  size_t space_used_by_request() const {
    return operation_ ? operation_->space_used_by_request() : 0;
  }

  RedisResponsePB& response() {
    switch (type_) {
      case OperationType::kRead:
        return *down_cast<YBRedisReadOp*>(operation_.get())->mutable_response();
      case OperationType::kWrite:
        return *down_cast<YBRedisWriteOp*>(operation_.get())->mutable_response();
      case OperationType::kNone: FALLTHROUGH_INTENDED;
      case OperationType::kLocal:
        FATAL_INVALID_ENUM_VALUE(OperationType, type_);
    }
    FATAL_INVALID_ENUM_VALUE(OperationType, type_);
  }

  const rpc::RpcMethodMetrics& metrics() const {
    return metrics_;
  }

  const std::string& partition_key() const {
    return partition_key_;
  }

  scoped_refptr<client::internal::RemoteTablet>& tablet() {
    return tablet_;
  }

  RedisInboundCall& call() const {
    return *call_;
  }

  void GetKeys(RedisKeyList* keys) const {
    if (FLAGS_redis_safe_batch) {
      keys->emplace_back(operation_ ? operation_->GetKey() : Slice());
    }
  }

  bool Apply(client::YBSession* session, const StatusFunctor& callback) {
    // We should destroy functor after this call.
    // Because it could hold references to other objects.
    // So we more it to temp variable.
    auto functor = std::move(functor_);

    if (call_->aborted()) {
      Respond(STATUS(Aborted, ""));
      return false;
    }

    // Used for DebugSleep
    if (functor) {
      return functor(callback);
    }

    if (tablet_) {
      operation_->SetTablet(tablet_);
    }

    auto status = session->Apply(operation_);
    if (!status.ok()) {
      Respond(status);
      return false;
    }
    return true;
  }

  void Respond(const Status& status) {
    responded_.store(true, std::memory_order_release);
    if (status.ok()) {
      if (operation_) {
        call_->RespondSuccess(index_, metrics_, &response());
      } else {
        RedisResponsePB resp;
        call_->RespondSuccess(index_, metrics_, &resp);
      }
    } else if ((type_ == OperationType::kRead || type_ == OperationType::kWrite) &&
               response().code() == RedisResponsePB_RedisStatusCode_SERVER_ERROR) {
      call_->Respond(index_, false, &response());
    } else {
      call_->RespondFailure(index_, status);
    }
  }

  std::string ToString() const {
    return Format("{ index: $0, operation: $1 }", index_, operation_);
  }

 private:
  OperationType type_;
  std::shared_ptr<RedisInboundCall> call_;
  size_t index_;
  std::shared_ptr<YBRedisOp> operation_;
  std::function<bool(const StatusFunctor&)> functor_;
  std::string partition_key_;
  rpc::RpcMethodMetrics metrics_;
  scoped_refptr<client::internal::RemoteTablet> tablet_;
  std::atomic<bool> responded_{false};
};

class SessionPool {
 public:
  void Init(const std::shared_ptr<client::YBClient>& client,
            const scoped_refptr<MetricEntity>& metric_entity) {
    client_ = client;
    auto* proto = &METRIC_redis_allocated_sessions;
    allocated_sessions_metric_ = proto->Instantiate(metric_entity, 0);
    proto = &METRIC_redis_available_sessions;
    available_sessions_metric_ = proto->Instantiate(metric_entity, 0);
  }

  std::shared_ptr<client::YBSession> Take() {
    client::YBSession* result = nullptr;
    if (!queue_.pop(result)) {
      std::lock_guard<std::mutex> lock(mutex_);
      auto session = client_->NewSession();
      session->SetTimeout(
          MonoDelta::FromMilliseconds(FLAGS_redis_service_yb_client_timeout_millis));
      CHECK_OK(session->SetFlushMode(YBSession::FlushMode::MANUAL_FLUSH));
      sessions_.push_back(session);
      allocated_sessions_metric_->IncrementBy(1);
      return session;
    }
    available_sessions_metric_->DecrementBy(1);
    return result->shared_from_this();
  }

  void Release(const std::shared_ptr<client::YBSession>& session) {
    available_sessions_metric_->IncrementBy(1);
    queue_.push(session.get());
  }
 private:
  std::shared_ptr<client::YBClient> client_;
  std::mutex mutex_;
  std::vector<std::shared_ptr<client::YBSession>> sessions_;
  boost::lockfree::queue<client::YBSession*> queue_{30};
  scoped_refptr<AtomicGauge<uint64_t>> allocated_sessions_metric_;
  scoped_refptr<AtomicGauge<uint64_t>> available_sessions_metric_;
};

class Block;
typedef std::shared_ptr<Block> BlockPtr;

class Block : public std::enable_shared_from_this<Block> {
 public:
  typedef MCVector<Operation*> Ops;

  Block(const BatchContextPtr& context,
        Ops::allocator_type allocator,
        rpc::RpcMethodMetrics metrics_internal)
      : context_(context),
        ops_(allocator),
        metrics_internal_(std::move(metrics_internal)),
        start_(MonoTime::Now()) {
  }

  Block(const Block&) = delete;
  void operator=(const Block&) = delete;

  void AddOperation(Operation* operation) {
    ops_.push_back(operation);
  }

  void Launch(SessionPool* session_pool, bool allow_local_calls_in_curr_thread = true) {
    session_pool_ = session_pool;
    session_ = session_pool->Take();
    bool has_ok = false;
    // Supposed to be called only once.
    boost::function<void(const Status&)> callback = BlockCallback(shared_from_this());
    for (auto* op : ops_) {
      has_ok = op->Apply(session_.get(), callback) || has_ok;
    }
    if (has_ok) {
      if (session_->HasPendingOperations()) {
        // Allow local calls in this thread only if no one is waiting behind us.
        session_->set_allow_local_calls_in_curr_thread(
            allow_local_calls_in_curr_thread && this->next_ == nullptr);
        session_->FlushAsync(std::move(callback));
      }
    } else {
      Processed();
    }
  }

  BlockPtr SetNext(const BlockPtr& next) {
    BlockPtr result = std::move(next_);
    next_ = next;
    return result;
  }

  std::string ToString() const {
    return Format("{ ops: $0 context: $1 next: $2 }",
                  ops_, static_cast<void*>(context_.get()), next_);
  }

 private:
  class BlockCallback {
   public:
    explicit BlockCallback(BlockPtr block) : block_(std::move(block)) {}

    void operator()(const Status& status) {
      // Block context owns the arena upon which this block is created.
      // Done is going to free up block's reference to context. So, unless we ensure that
      // the context lives beyond the block_.reset() we might get an error while updating the
      // ref-count for the block_ (in the area of arena owned by the context).
      auto context = block_->context_;
      DCHECK(context != nullptr);
      block_->Done(status);
      block_.reset();
    }
   private:
    BlockPtr block_;
  };

  friend class BlockCallback;

  void Done(const Status& status) {
    MonoTime now = MonoTime::Now();
    metrics_internal_.handler_latency->Increment(now.GetDeltaSince(start_).ToMicroseconds());
    VLOG(3) << "Received status from call " << status.ToString(true);

    std::unordered_map<const client::YBOperation*, Status> op_errors;
    if (!status.ok()) {
      if (session_ != nullptr) {
        for (const auto& error : session_->GetPendingErrors()) {
          op_errors[&error->failed_op()] = std::move(error->status());
          YB_LOG_EVERY_N_SECS(WARNING, 1) << "Explicit error while inserting: "
                                          << error->status().ToString();
        }
      }
    }

    for (auto* op : ops_) {
      if (op->has_operation() && op_errors.find(&op->operation()) != op_errors.end()) {
        op->Respond(op_errors[&op->operation()]);
      } else {
        op->Respond(Status::OK());
      }
    }

    Processed();
  }

  void Processed() {
    auto allow_local_calls_in_curr_thread = false;
    if (session_) {
      session_->allow_local_calls_in_curr_thread();
      session_pool_->Release(session_);
      session_.reset();
    }
    if (next_) {
      next_->Launch(session_pool_, allow_local_calls_in_curr_thread);
    }
    context_.reset();
  }

 private:
  BatchContextPtr context_;
  Ops ops_;
  rpc::RpcMethodMetrics metrics_internal_;
  MonoTime start_;
  SessionPool* session_pool_;
  std::shared_ptr<client::YBSession> session_;
  BlockPtr next_;
};

typedef std::array<rpc::RpcMethodMetrics, kOperationTypeMapSize> InternalMetrics;

struct BlockData {
  explicit BlockData(Arena* arena) : used_keys(UsedKeys::allocator_type(arena)) {}

  std::string ToString() const {
    return Format("{ used_keys: $0 block: $1 count: $2 }", used_keys, block, count);
  }

  typedef MCUnorderedSet<Slice, Slice::Hash> UsedKeys;
  UsedKeys used_keys;
  BlockPtr block;
  size_t count = 0;
};

class TabletOperations {
 public:
  explicit TabletOperations(Arena* arena)
      : read_data_(arena), write_data_(arena) {
  }

  BlockData& data(OperationType type) {
    switch (type) {
      case OperationType::kRead:
        return read_data_;
      case OperationType::kWrite:
        return write_data_;
      case OperationType::kNone: FALLTHROUGH_INTENDED;
      case OperationType::kLocal:
        FATAL_INVALID_ENUM_VALUE(OperationType, type);
    }
    FATAL_INVALID_ENUM_VALUE(OperationType, type);
  }

  void Done(SessionPool* session_pool, bool allow_local_calls_in_curr_thread) {
    if (flush_head_) {
      flush_head_->Launch(session_pool, allow_local_calls_in_curr_thread);
    } else {
      if (read_data_.block) {
        read_data_.block->Launch(session_pool, allow_local_calls_in_curr_thread);
      }
      if (write_data_.block) {
        write_data_.block->Launch(session_pool, allow_local_calls_in_curr_thread);
      }
    }
  }

  void Process(const BatchContextPtr& context,
               Arena* arena,
               Operation* operation,
               const InternalMetrics& metrics_internal) {
    auto type = operation->type();
    if (type == OperationType::kLocal) {
      ProcessLocalOperation(context, arena, operation, metrics_internal);
      return;
    }
    boost::container::small_vector<Slice, RedisClientCommand::static_capacity> keys;
    operation->GetKeys(&keys);
    CheckConflicts(type, keys);
    auto& data = this->data(type);
    if (!data.block) {
      ArenaAllocator<Block> alloc(arena);
      data.block = std::allocate_shared<Block>(
          alloc, context, alloc, metrics_internal[static_cast<size_t>(OperationType::kRead)]);
      if (type == last_conflict_type_) {
        auto old_value = this->data(Opposite(type)).block->SetNext(data.block);
        if (old_value) {
          LOG(DFATAL) << "Opposite already had next block: "
                      << operation->call().serialized_request().ToDebugString();
        }
      }
    }
    data.block->AddOperation(operation);
    RememberKeys(type, &keys);
  }

  std::string ToString() const {
    return Format("{ read_data: $0 write_data: $1 flush_head: $2 last_local_block: $3 "
                  "last_conflict_type: $4 }",
                  read_data_, write_data_, flush_head_, last_local_block_, last_conflict_type_);
  }

 private:
  void ProcessLocalOperation(const BatchContextPtr& context,
                             Arena* arena,
                             Operation* operation,
                             const InternalMetrics& metrics_internal) {
    ArenaAllocator<Block> alloc(arena);
    auto block = std::allocate_shared<Block>(
        alloc, context, alloc, metrics_internal[static_cast<size_t>(OperationType::kLocal)]);
    switch (last_conflict_type_) {
      case OperationType::kNone:
        if (read_data_.block) {
          flush_head_ = read_data_.block;
          if (write_data_.block) {
            read_data_.block->SetNext(write_data_.block);
            write_data_.block->SetNext(block);
          } else {
            read_data_.block->SetNext(block);
          }
        } else if (write_data_.block) {
          write_data_.block->SetNext(block);
        } else {
          flush_head_ = block;
        }
        break;
      case OperationType::kRead:
        read_data_.block->SetNext(block);
        break;
      case OperationType::kWrite:
        write_data_.block->SetNext(block);
        break;
      case OperationType::kLocal:
        last_local_block_->SetNext(block);
        break;
    }
    read_data_.block = nullptr;
    read_data_.used_keys.clear();
    write_data_.block = nullptr;
    write_data_.used_keys.clear();
    last_local_block_ = block;
    last_conflict_type_ = OperationType::kLocal;
    block->AddOperation(operation);
  }

  void ConflictFound(OperationType type) {
    auto& data = this->data(type);
    auto& opposite_data = this->data(Opposite(type));

    switch (last_conflict_type_) {
      case OperationType::kNone:
        flush_head_ = opposite_data.block;
        opposite_data.block->SetNext(data.block);
        break;
      case OperationType::kWrite:
      case OperationType::kRead:
        data.block = nullptr;
        data.used_keys.clear();
        break;
      case OperationType::kLocal:
        last_local_block_->SetNext(opposite_data.block);
        opposite_data.block->SetNext(data.block);
        break;
    }
    last_conflict_type_ = type;
  }

  void CheckConflicts(OperationType type, const RedisKeyList& keys) {
    if (last_conflict_type_ == type) {
      return;
    }
    auto& opposite = data(Opposite(type));
    bool conflict = false;
    for (const auto& key : keys) {
      if (opposite.used_keys.count(key)) {
        conflict = true;
        break;
      }
    }
    if (conflict) {
      ConflictFound(type);
    }
  }

  void RememberKeys(OperationType type, RedisKeyList* keys) {
    BlockData* dest;
    switch (type) {
      case OperationType::kRead:
        dest = &read_data_;
        break;
      case OperationType::kWrite:
        dest = &write_data_;
        break;
      case OperationType::kNone: FALLTHROUGH_INTENDED;
      case OperationType::kLocal:
        FATAL_INVALID_ENUM_VALUE(OperationType, type);
    }
    for (auto& key : *keys) {
      dest->used_keys.insert(std::move(key));
    }
  }

  BlockData read_data_;
  BlockData write_data_;
  BlockPtr flush_head_;
  BlockPtr last_local_block_;

  // Type of command that caused last conflict between reads and writes.
  OperationType last_conflict_type_ = OperationType::kNone;
};

class BatchContextImpl : public BatchContext {
 public:
  BatchContextImpl(
      const std::shared_ptr<client::YBClient>& client,
      const RedisServer* server,
      const std::shared_ptr<client::YBTable>& table,
      SessionPool* session_pool,
      const std::shared_ptr<RedisInboundCall>& call,
      const InternalMetrics& metrics_internal,
      const MemTrackerPtr& mem_tracker)
      : client_(client),
        server_(server),
        table_(table),
        session_pool_(session_pool),
        call_(call),
        metrics_internal_(metrics_internal),
        consumption_(mem_tracker, 0),
        operations_(&arena_),
        tablets_(&arena_) {}

  virtual ~BatchContextImpl() {}

  const RedisClientCommand& command(size_t idx) const override {
    return call_->client_batch()[idx];
  }

  const std::shared_ptr<RedisInboundCall>& call() const override {
    return call_;
  }

  const std::shared_ptr<client::YBClient>& client() const override {
    return client_;
  }

  const RedisServer* server() override {
    return server_;
  }

  const std::shared_ptr<client::YBTable>& table() const override {
    return table_;
  }

  void Commit() {
    if (operations_.empty()) {
      return;
    }

    MonoTime deadline = MonoTime::Now() +
                        MonoDelta::FromMilliseconds(FLAGS_redis_service_yb_client_timeout_millis);
    lookups_left_.store(operations_.size(), std::memory_order_release);
    for (auto& operation : operations_) {
      client_->LookupTabletByKey(
          table_.get(),
          operation.partition_key(),
          deadline,
          &operation.tablet(),
          Bind(&BatchContextImpl::LookupDone, this, &operation));
    }
  }

  void Apply(
      size_t index,
      std::shared_ptr<client::YBRedisReadOp> operation,
      const rpc::RpcMethodMetrics& metrics) override {
    DoApply(index, std::move(operation), metrics);
  }

  void Apply(
      size_t index,
      std::shared_ptr<client::YBRedisWriteOp> operation,
      const rpc::RpcMethodMetrics& metrics) override {
    DoApply(index, std::move(operation), metrics);
  }

  void Apply(
      size_t index,
      std::function<bool(const StatusFunctor&)> functor,
      std::string partition_key,
      const rpc::RpcMethodMetrics& metrics) override {
    DoApply(index, std::move(functor), std::move(partition_key), metrics);
  }

  std::string ToString() const {
    return Format("{ tablets: $0 }", tablets_);
  }

 private:
  template <class... Args>
  void DoApply(Args&&... args) {
    operations_.emplace_back(call_, std::forward<Args>(args)...);
    if (PREDICT_FALSE(operations_.back().responded())) {
      operations_.pop_back();
    } else {
      consumption_.Add(operations_.back().space_used_by_request());
    }
  }

  void LookupDone(Operation* operation, const Status& status) {
    if (!status.ok()) {
      operation->Respond(status);
    }
    if (lookups_left_.fetch_sub(1, std::memory_order_acq_rel) != 1) {
      return;
    }

    BatchContextPtr self(this);
    for (auto& operation : operations_) {
      if (!operation.responded()) {
        auto it = tablets_.find(operation.tablet()->tablet_id());
        if (it == tablets_.end()) {
          it = tablets_.emplace(operation.tablet()->tablet_id(), TabletOperations(&arena_)).first;
        }
        it->second.Process(self, &arena_, &operation, metrics_internal_);
      }
    }

    int idx = 0;
    for (auto& tablet : tablets_) {
      tablet.second.Done(session_pool_, ++idx == tablets_.size());
    }
    tablets_.clear();
  }

  std::shared_ptr<client::YBClient> client_;
  const RedisServer* server_ = nullptr;
  std::shared_ptr<client::YBTable> table_;
  SessionPool* session_pool_;
  std::shared_ptr<RedisInboundCall> call_;
  const InternalMetrics& metrics_internal_;
  ScopedTrackedConsumption consumption_;

  Arena arena_;
  MCDeque<Operation> operations_;
  std::atomic<size_t> lookups_left_;
  MCUnorderedMap<Slice, TabletOperations, Slice::Hash> tablets_;
};

} // namespace

class RedisServiceImpl::Impl {
 public:
  Impl(RedisServer* server, string yb_tier_master_address);

  ~Impl() {
    // Wait for DebugSleep to finish.
    // We use DebugSleep only during tests.
    // So just for long enough, giving extra 400ms for it.
    std::this_thread::sleep_for(500ms);
  }

  void Handle(yb::rpc::InboundCallPtr call_ptr);

 private:
  void SetupMethod(const RedisCommandInfo& info) {
    auto info_ptr = std::make_shared<RedisCommandInfo>(info);
    std::string lower_name = boost::to_lower_copy(info.name);
    std::string upper_name = boost::to_upper_copy(info.name);
    size_t len = info.name.size();
    std::string temp(len, 0);
    for (size_t i = 0; i != (1ULL << len); ++i) {
      for (size_t j = 0; j != len; ++j) {
        temp[j] = i & (1 << j) ? upper_name[j] : lower_name[j];
      }
      names_.push_back(temp);
      CHECK(command_name_to_info_map_.emplace(names_.back(), info_ptr).second);
    }
  }

  bool CheckArgumentSizeOK(const RedisClientCommand& cmd_args) {
    for (Slice arg : cmd_args) {
      if (arg.size() > FLAGS_redis_max_value_size) {
        return false;
      }
    }
    return true;
  }

  vector<string> GetRedisPasswords() {
    vector<string> ret;
    CHECK_OK(client_->GetRedisPasswords(&ret));
    return ret;
  }

  bool CheckAuthentication(RedisConnectionContext* conn_context) {
    if (!conn_context->is_authenticated()) {
      conn_context->set_authenticated(!FLAGS_enable_redis_auth || GetRedisPasswords().empty());
    }
    return conn_context->is_authenticated();
  }

  constexpr static int kRpcTimeoutSec = 5;

  void PopulateHandlers();
  void AppendToMonitors(ConnectionPtr conn);
  void LogToMonitors(const string& end, int db, const RedisClientCommand& cmd);
  // Fetches the appropriate handler for the command, nullptr if none exists.
  const RedisCommandInfo* FetchHandler(const RedisClientCommand& cmd_args);
  CHECKED_STATUS SetUpYBClient();

  std::deque<std::string> names_;
  std::unordered_map<Slice, RedisCommandInfoPtr, Slice::Hash> command_name_to_info_map_;
  yb::rpc::RpcMethodMetrics metrics_error_;
  InternalMetrics metrics_internal_;

  std::string yb_tier_master_addresses_;
  std::mutex yb_mutex_;  // Mutex that protects the creation of client_ and table_.
  std::atomic<bool> yb_client_initialized_;
  std::shared_ptr<client::YBClient> client_;
  SessionPool session_pool_;
  std::shared_ptr<client::YBTable> table_;

  std::vector<ConnectionWeakPtr> monitoring_clients_;
  scoped_refptr<AtomicGauge<uint64_t>> num_clients_monitoring_;
  rw_spinlock monitoring_clients_mutex_;

  RedisServer* server_;
};

void RedisServiceImpl::Impl::AppendToMonitors(ConnectionPtr conn) {
  boost::lock_guard<rw_spinlock> lock(monitoring_clients_mutex_);
  monitoring_clients_.emplace_back(conn);
  num_clients_monitoring_->IncrementBy(1);
}

void RedisServiceImpl::Impl::LogToMonitors(
    const string& end, int db, const RedisClientCommand& cmd) {
  boost::shared_lock<rw_spinlock> rlock(monitoring_clients_mutex_);

  if (monitoring_clients_.empty()) return;

  // Prepare the string to be sent to all the monitoring clients.
  int64_t now_ms = ToMicroseconds(CoarseMonoClock::Now().time_since_epoch());
  std::stringstream ss;
  ss << "+";
  ss.setf(std::ios::fixed, std::ios::floatfield);
  ss.precision(6);
  ss << (now_ms / 1000000.0) << " [" << db << " " << end << "]";
  for (auto& part : cmd) {
    ss << " \"" << part << "\"";
  }
  ss << "\r\n";

  // Send the message to all the monitoring clients.
  OutboundDataPtr out =
      std::make_shared<yb::rpc::StringOutboundData>(ss.str(), "Redis Monitor Data");
  bool should_cleanup = false;
  for (auto iter = monitoring_clients_.begin(); iter != monitoring_clients_.end(); iter++) {
    ConnectionPtr connection = iter->lock();
    if (connection) {
      connection->QueueOutboundData(out);
    } else {
      should_cleanup = true;
    }
  }

  // Clean up clients that may have disconnected.
  if (should_cleanup) {
    rlock.unlock();
    boost::lock_guard<rw_spinlock> wlock(monitoring_clients_mutex_);

    auto old_size = monitoring_clients_.size();
    monitoring_clients_.erase(std::remove_if(monitoring_clients_.begin(), monitoring_clients_.end(),
                                             [] (const ConnectionWeakPtr& e) -> bool {
                                               return e.lock() == nullptr;
                                             }),
                              monitoring_clients_.end());
    num_clients_monitoring_->DecrementBy(old_size - monitoring_clients_.size());
  }
}

void RedisServiceImpl::Impl::PopulateHandlers() {
  auto metric_entity = server_->metric_entity();
  FillRedisCommands(metric_entity, std::bind(&Impl::SetupMethod, this, _1));

  // Set up metrics for erroneous calls.
  metrics_error_.handler_latency = YB_REDIS_METRIC(error).Instantiate(metric_entity);
  metrics_internal_[static_cast<size_t>(OperationType::kWrite)].handler_latency =
      YB_REDIS_METRIC(set_internal).Instantiate(metric_entity);
  metrics_internal_[static_cast<size_t>(OperationType::kRead)].handler_latency =
      YB_REDIS_METRIC(get_internal).Instantiate(metric_entity);
  metrics_internal_[static_cast<size_t>(OperationType::kLocal)].handler_latency =
      metrics_internal_[static_cast<size_t>(OperationType::kRead)].handler_latency;

  auto* proto = &METRIC_redis_monitoring_clients;
  num_clients_monitoring_ = proto->Instantiate(metric_entity, 0);
}

const RedisCommandInfo* RedisServiceImpl::Impl::FetchHandler(const RedisClientCommand& cmd_args) {
  if (cmd_args.size() < 1) {
    return nullptr;
  }
  Slice cmd_name = cmd_args[0];
  auto iter = command_name_to_info_map_.find(cmd_args[0]);
  if (iter == command_name_to_info_map_.end()) {
    YB_LOG_EVERY_N_SECS(ERROR, 60)
        << "Command " << cmd_name << " not yet supported. "
        << "Arguments: " << ToString(cmd_args) << ". "
        << "Raw: " << Slice(cmd_args[0].data(), cmd_args.back().end()).ToDebugString();
    return nullptr;
  }
  return iter->second.get();
}

RedisServiceImpl::Impl::Impl(RedisServer* server, string yb_tier_master_addresses)
    : yb_tier_master_addresses_(std::move(yb_tier_master_addresses)),
      yb_client_initialized_(false),
      server_(server) {
  // TODO(ENG-446): Handle metrics for all the methods individually.
  PopulateHandlers();
}

Status RedisServiceImpl::Impl::SetUpYBClient() {
  std::lock_guard<std::mutex> guard(yb_mutex_);
  if (!yb_client_initialized_.load(std::memory_order_relaxed)) {
    YBClientBuilder client_builder;
    client_builder.set_client_name("redis_ybclient");
    client_builder.default_rpc_timeout(MonoDelta::FromSeconds(kRpcTimeoutSec));
    client_builder.add_master_server_addr(yb_tier_master_addresses_);
    client_builder.set_metric_entity(server_->metric_entity());
    client_builder.set_parent_mem_tracker(server_->mem_tracker());
    client_builder.set_callback_threadpool_size(FLAGS_redis_callbacks_threadpool_size);
    if (server_->tserver() != nullptr) {
      if (!server_->tserver()->permanent_uuid().empty()) {
        client_builder.set_tserver_uuid(server_->tserver()->permanent_uuid());
      }
      client_builder.use_messenger(server_->tserver()->messenger());
    }

    CloudInfoPB cloud_info_pb;
    cloud_info_pb.set_placement_cloud(FLAGS_placement_cloud);
    cloud_info_pb.set_placement_region(FLAGS_placement_region);
    cloud_info_pb.set_placement_zone(FLAGS_placement_zone);
    client_builder.set_cloud_info_pb(cloud_info_pb);

    RETURN_NOT_OK(client_builder.Build(&client_));

    // Add proxy to call local tserver if available.
    if (server_->tserver() != nullptr && server_->tserver()->proxy() != nullptr) {
      client_->AddTabletServerProxy(
          server_->tserver()->permanent_uuid(), server_->tserver()->proxy());
    }

    const YBTableName table_name(common::kRedisKeyspaceName, common::kRedisTableName);
    RETURN_NOT_OK(client_->OpenTable(table_name, &table_));

    session_pool_.Init(client_, server_->metric_entity());

    yb_client_initialized_.store(true, std::memory_order_release);
  }
  return Status::OK();
}

void RedisServiceImpl::Impl::Handle(rpc::InboundCallPtr call_ptr) {
  auto call = std::static_pointer_cast<RedisInboundCall>(call_ptr);

  DVLOG(4) << "Asked to handle a call " << call->ToString();
  if (call->serialized_request().size() > FLAGS_redis_max_command_size) {
    auto message = StrCat("Size of redis command ", call->serialized_request().size(),
                          ", but we only support up to length of ", FLAGS_redis_max_command_size);
    for (size_t idx = 0; idx != call->client_batch().size(); ++idx) {
      RespondWithFailure(call, idx, message);
    }
    return;
  }

  // Ensure that we have the required YBClient(s) initialized.
  if (!yb_client_initialized_.load(std::memory_order_acquire)) {
    auto status = SetUpYBClient();
    if (!status.ok()) {
      auto message = StrCat("Could not open .redis table. ", status.ToString());
      for (size_t idx = 0; idx != call->client_batch().size(); ++idx) {
        RespondWithFailure(call, idx, message);
      }
      return;
    }
  }

  // Call could contain several commands, i.e. batch.
  // We process them as follows:
  // Each read commands are processed individually.
  // Sequential write commands use single session and the same batcher.
  auto context = make_scoped_refptr<BatchContextImpl>(
      client_, server_, table_, &session_pool_, call, metrics_internal_,
      server_->mem_tracker());
  const auto& batch = call->client_batch();
  auto conn = call->connection();
  const string remote = yb::ToString(conn->remote());
  RedisConnectionContext* conn_context = &(call->connection_context());
  for (size_t idx = 0; idx != batch.size(); ++idx) {
    const RedisClientCommand& c = batch[idx];

    auto cmd_info = FetchHandler(c);

    // Handle the current redis command.
    if (cmd_info == nullptr) {
      RespondWithFailure(call, idx, "Unsupported call.");
      continue;
    }

    size_t arity = static_cast<size_t>(std::abs(cmd_info->arity) - 1);
    bool exact_count = cmd_info->arity > 0;
    size_t passed_arguments = c.size() - 1;
    if (!exact_count && passed_arguments < arity) {
      // -X means that the command needs >= X arguments.
      YB_LOG_EVERY_N_SECS(ERROR, 60)
          << "Requested command " << c[0] << " does not have enough arguments."
          << " At least " << arity << " expected, but " << passed_arguments << " found.";
      RespondWithFailure(call, idx, "Too few arguments.");
    } else if (exact_count && passed_arguments != arity) {
      // X (> 0) means that the command needs exactly X arguments.
      YB_LOG_EVERY_N_SECS(ERROR, 60)
          << "Requested command " << c[0] << " has wrong number of arguments. "
          << arity << " expected, but " << passed_arguments << " found.";
      RespondWithFailure(call, idx, "Wrong number of arguments.");
    } else if (!CheckArgumentSizeOK(c)) {
      RespondWithFailure(call, idx, "Redis argument too long.");
    } else if (!CheckAuthentication(conn_context) && cmd_info->name != "auth") {
      RespondWithFailure(call, idx, "Authentication required.", "NOAUTH");
    } else {
      // Handle the call.
      cmd_info->functor(*cmd_info, idx, context.get());
    }

    // Add to the appenders after the call has been handled (i.e. reponded with "OK").
    if (cmd_info->name == "monitor") {
      AppendToMonitors(conn);
    } else if (cmd_info->name != "config") {
      LogToMonitors(remote, 0, c);
    }
  }
  context->Commit();
}

RedisServiceImpl::RedisServiceImpl(RedisServer* server, string yb_tier_master_address)
    : RedisServerServiceIf(server->metric_entity()),
      impl_(new Impl(server, std::move(yb_tier_master_address))) {}

RedisServiceImpl::~RedisServiceImpl() {
}

void RedisServiceImpl::Handle(yb::rpc::InboundCallPtr call) {
  impl_->Handle(std::move(call));
}

}  // namespace redisserver
}  // namespace yb
