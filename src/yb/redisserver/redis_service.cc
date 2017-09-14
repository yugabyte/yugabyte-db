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

#include "yb/redisserver/redis_service.h"

#include <thread>

#include <boost/algorithm/string/case_conv.hpp>

#include <boost/lockfree/queue.hpp>

#include <boost/logic/tribool.hpp>

#include <boost/preprocessor/seq/for_each.hpp>

#include <gflags/gflags.h>

#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/client/async_rpc.h"
#include "yb/client/callbacks.h"
#include "yb/client/client.h"
#include "yb/client/client_builder-internal.h"

#include "yb/common/redis_protocol.pb.h"

#include "yb/redisserver/redis_constants.h"
#include "yb/redisserver/redis_encoding.h"
#include "yb/redisserver/redis_parser.h"
#include "yb/redisserver/redis_rpc.h"
#include "yb/redisserver/redis_server.h"

#include "yb/rpc/rpc_context.h"

#include "yb/tserver/tablet_server.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/memory/mc_types.h"

#define DEFINE_REDIS_histogram_EX(name_identifier, label_str, desc_str) \
  METRIC_DEFINE_histogram( \
      server, BOOST_PP_CAT(handler_latency_yb_redisserver_RedisServerService_, name_identifier), \
      (label_str), yb::MetricUnit::kMicroseconds, \
      "Microseconds spent handling " desc_str " RPC requests", \
      60000000LU, 2)

#define DEFINE_REDIS_histogram(name_identifier, capitalized_name_str) \
  DEFINE_REDIS_histogram_EX( \
      name_identifier, \
      "yb.redisserver.RedisServerService." BOOST_STRINGIZE(name_identifier) " RPC Time", \
      "yb.redisserver.RedisServerService." capitalized_name_str "Command()")

DEFINE_REDIS_histogram_EX(error,
                          "yb.redisserver.RedisServerService.AnyMethod RPC Time",
                          "yb.redisserver.RedisServerService.ErrorUnsupportedMethod()");
DEFINE_REDIS_histogram_EX(get_internal,
                          "yb.redisserver.RedisServerService.Get RPC Time",
                          "in yb.client.Get");
DEFINE_REDIS_histogram_EX(set_internal,
                          "yb.redisserver.RedisServerService.Set RPC Time",
                          "in yb.client.Set");

#define DEFINE_REDIS_SESSION_GAUGE(type, state) \
  METRIC_DEFINE_gauge_uint64( \
      server, \
      BOOST_PP_CAT(BOOST_PP_CAT(BOOST_PP_CAT(state, _), type), _sessions), \
      "Number of " BOOST_PP_STRINGIZE(state) " " BOOST_PP_STRINGIZE(type) " sessions", \
      yb::MetricUnit::kUnits, \
      "Number of " BOOST_PP_STRINGIZE(type) " sessions " BOOST_PP_STRINGIZE(type) \
      " by Redis service.") \
      /**/

DEFINE_REDIS_SESSION_GAUGE( read, allocated);
DEFINE_REDIS_SESSION_GAUGE( read, available);
DEFINE_REDIS_SESSION_GAUGE(write, allocated);
DEFINE_REDIS_SESSION_GAUGE(write, available);

#if defined(THREAD_SANITIZER) || defined(ADDRESS_SANITIZER)
constexpr int32_t kDefaultRedisServiceTimeoutMs = 600000;
#else
constexpr int32_t kDefaultRedisServiceTimeoutMs = 60000;
#endif

DEFINE_int32(redis_service_yb_client_timeout_millis, kDefaultRedisServiceTimeoutMs,
             "Timeout in milliseconds for RPC calls from Redis service to master/tserver");

DEFINE_bool(redis_safe_batch, true, "Use safe batching with Redis service");

#define REDIS_COMMANDS \
    ((get, Get, 2, READ)) \
    ((mget, MGet, -2, READ)) \
    ((hget, HGet, 3, READ)) \
    ((hmget, HMGet, -3, READ)) \
    ((hgetall, HGetAll, 2, READ)) \
    ((hkeys, HKeys, 2, READ)) \
    ((hvals, HVals, 2, READ)) \
    ((hlen, HLen, 2, READ)) \
    ((hexists, HExists, 3, READ)) \
    ((hstrlen, HStrLen, 3, READ)) \
    ((smembers, SMembers, 2, READ)) \
    ((sismember, SIsMember, 3, READ)) \
    ((scard, SCard, 2, READ)) \
    ((strlen, StrLen, 2, READ)) \
    ((exists, Exists, 2, READ)) \
    ((getrange, GetRange, 4, READ)) \
    ((set, Set, -3, WRITE)) \
    ((mset, MSet, -3, WRITE)) \
    ((hset, HSet, 4, WRITE)) \
    ((hmset, HMSet, -4, WRITE)) \
    ((hdel, HDel, -3, WRITE)) \
    ((sadd, SAdd, -3, WRITE)) \
    ((srem, SRem, -3, WRITE)) \
    ((getset, GetSet, 3, WRITE)) \
    ((append, Append, 3, WRITE)) \
    ((del, Del, 2, WRITE)) \
    ((setrange, SetRange, 4, WRITE)) \
    ((incr, Incr, 2, WRITE)) \
    ((echo, Echo, 2, LOCAL)) \
    ((auth, Auth, -1, LOCAL)) \
    ((config, Config, -1, LOCAL)) \
    ((info, Info, -1, LOCAL)) \
    ((role, Role, 1, LOCAL)) \
    ((ping, Ping, -1, LOCAL)) \
    ((command, Command, -1, LOCAL)) \
    ((quit, Quit, 1, LOCAL)) \
    ((flushdb, FlushDB, 1, LOCAL))
    /**/

#define DO_DEFINE_HISTOGRAM(name, cname, arity, type) \
  DEFINE_REDIS_histogram(name, BOOST_PP_STRINGIZE(cname));
#define DEFINE_HISTOGRAM(r, data, elem) DO_DEFINE_HISTOGRAM elem

BOOST_PP_SEQ_FOR_EACH(DEFINE_HISTOGRAM, ~, REDIS_COMMANDS)

using yb::client::YBOperation;
using yb::client::YBRedisReadOp;
using yb::client::YBRedisWriteOp;
using yb::client::YBClientBuilder;
using yb::client::YBSchema;
using yb::client::YBSession;
using yb::client::YBStatusCallback;
using yb::client::YBTableName;
using yb::RedisResponsePB;

namespace yb {
namespace redisserver {

typedef boost::container::small_vector_base<Slice> RedisKeyList;

#define READ_OP YBRedisReadOp
#define WRITE_OP YBRedisWriteOp
#define LOCAL_OP RedisResponsePB

#define DO_PARSER_FORWARD(name, cname, arity, type) \
    CHECKED_STATUS BOOST_PP_CAT(Parse, cname)( \
        BOOST_PP_CAT(type, _OP) *op, \
        const RedisClientCommand& args);
#define PARSER_FORWARD(r, data, elem) DO_PARSER_FORWARD elem

BOOST_PP_SEQ_FOR_EACH(PARSER_FORWARD, ~, REDIS_COMMANDS)

namespace {

class Operation {
 public:
  template <class Op>
  Operation(const std::shared_ptr<RedisInboundCall>& call,
            size_t index,
            std::shared_ptr<Op> operation,
            const rpc::RpcMethodMetrics& metrics)
    : read_(std::is_same<Op, YBRedisReadOp>::value),
      call_(call),
      index_(index),
      operation_(std::move(operation)),
      metrics_(metrics) {
    auto status = operation_->GetPartitionKey(&partition_key_);
    if (!status.ok()) {
      Respond(status);
    }
  }

  bool responded() const {
    return responded_.load(std::memory_order_acquire);
  }

  size_t index() const {
    return index_;
  }

  bool read() const {
    return read_;
  }

  const YBOperation& operation() const {
    return *operation_;
  }

  RedisResponsePB& response() {
    if (read_) {
      return *down_cast<YBRedisReadOp*>(operation_.get())->mutable_response();
    } else {
      return *down_cast<YBRedisWriteOp*>(operation_.get())->mutable_response();
    }
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
      Slice key;
      CHECK_OK(operation_->row().GetBinary(kRedisKeyColumnName, &key));
      keys->push_back(key);
    }
  }

  bool Apply(client::YBSession* session) {
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
      call_->RespondSuccess(index_, metrics_, &response());
    } else {
      call_->RespondFailure(index_, status);
    }
  }
 private:
  bool read_;
  std::shared_ptr<RedisInboundCall> call_;
  size_t index_;
  std::shared_ptr<YBOperation> operation_;
  rpc::RpcMethodMetrics metrics_;
  std::string partition_key_;
  scoped_refptr<client::internal::RemoteTablet> tablet_;
  std::atomic<bool> responded_{false};
};

class SessionPool {
 public:
  void Init(const std::shared_ptr<client::YBClient>& client,
            const scoped_refptr<MetricEntity>& metric_entity,
            bool read) {
    client_ = client;
    read_ = read;
    auto* proto = read ? &METRIC_allocated_read_sessions : &METRIC_allocated_write_sessions;
    allocated_sessions_metric_ = proto->Instantiate(metric_entity, 0);
    proto = read ? &METRIC_available_read_sessions : &METRIC_available_write_sessions;
    available_sessions_metric_ = proto->Instantiate(metric_entity, 0);
  }

  std::shared_ptr<client::YBSession> Take() {
    client::YBSession* result = nullptr;
    if (!queue_.pop(result)) {
      std::lock_guard<std::mutex> lock(mutex_);
      auto session = client_->NewSession(read_);
      session->SetTimeoutMillis(FLAGS_redis_service_yb_client_timeout_millis);
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
  bool read_;
  std::mutex mutex_;
  std::vector<std::shared_ptr<client::YBSession>> sessions_;
  boost::lockfree::queue<client::YBSession*> queue_{30};
  scoped_refptr<AtomicGauge<uint64_t>> allocated_sessions_metric_;
  scoped_refptr<AtomicGauge<uint64_t>> available_sessions_metric_;
};

class BatchContext;
typedef scoped_refptr<BatchContext> BatchContextPtr;

class Block : public std::enable_shared_from_this<Block> {
 public:
  typedef MCVector<Operation*> Ops;

  Block(const BatchContextPtr& context,
        Ops::allocator_type allocator,
        rpc::RpcMethodMetrics metrics_internal)
      : context_(context),
        ops_(allocator),
        metrics_internal_(std::move(metrics_internal)),
        start_(MonoTime::FineNow()) {}

  void AddOperation(Operation* operation) {
    ops_.push_back(operation);
  }

  void Launch(SessionPool* session_pools) {
    session_pools_ = session_pools;
    session_ = session_pools[ops_.front()->read()].Take();
    bool has_ok = false;
    for (auto* op : ops_) {
      has_ok = op->Apply(session_.get()) || has_ok;
    }
    if (has_ok) {
      session_->FlushAsync(new BlockCallback(shared_from_this()));
    } else {
      Processed();
    }
  }

  std::shared_ptr<Block> SetNext(const std::shared_ptr<Block>& next) {
    std::shared_ptr<Block> result = std::move(next_);
    next_ = next;
    return result;
  }
 private:
  class BlockCallback : public YBStatusCallback {
   public:
    explicit BlockCallback(std::shared_ptr<Block> block) : block_(std::move(block)) {}

    void Run(const Status& status) override {
      auto context = block_->context_;
      block_->Done(status);
      block_.reset();
      delete this;
    }
   private:
    std::shared_ptr<Block> block_;
  };
  friend class BlockCallback;

  void Done(const Status& status) {
    MonoTime now = MonoTime::Now(MonoTime::FINE);
    metrics_internal_.handler_latency->Increment(now.GetDeltaSince(start_).ToMicroseconds());
    VLOG(3) << "Received status from call " << status.ToString(true);

    if (!status.ok()) {
      client::CollectedErrors errors;
      bool overflowed;
      if (session_.get() != nullptr) {
        session_->GetPendingErrors(&errors, &overflowed);
        for (const auto& error : errors) {
          LOG(WARNING) << "Explicit error while inserting: " << error->status().ToString();
        }
      }
    }

    for (auto* op : ops_) {
      op->Respond(status);
    }

    Processed();
  }

  void Processed() {
    session_pools_[ops_.front()->read()].Release(session_);
    session_.reset();
    if (next_) {
      next_->Launch(session_pools_);
    }
    context_.reset();
  }
 private:
  BatchContextPtr context_;
  Ops ops_;
  rpc::RpcMethodMetrics metrics_internal_;
  MonoTime start_;
  SessionPool* session_pools_;
  std::shared_ptr<client::YBSession> session_;
  std::shared_ptr<Block> next_;
};

struct BlockData {
  explicit BlockData(Arena* arena) : used_keys(UsedKeys::allocator_type(arena)) {}

  typedef MCUnorderedSet<Slice, Slice::Hash> UsedKeys;
  UsedKeys used_keys;
  std::shared_ptr<Block> block;
  size_t count = 0;
};

class TabletOperations {
 public:
  explicit TabletOperations(Arena* arena)
      : read_data_(arena), write_data_(arena) {}

  BlockData& data(bool read) {
    return read ? read_data_ : write_data_;
  }

  void Done(SessionPool* session_pools) {
    if (flush_head_) {
      flush_head_->Launch(session_pools);
    } else {
      if (read_data_.block) {
        read_data_.block->Launch(session_pools);
      }
      if (write_data_.block) {
        write_data_.block->Launch(session_pools);
      }
    }
  }

  void Process(const BatchContextPtr& context,
               Arena* arena,
               Operation* operation,
               rpc::RpcMethodMetrics* metrics_internal) {
    bool read = operation->read();
    boost::container::small_vector<Slice, RedisClientCommand::static_capacity> keys;
    operation->GetKeys(&keys);
    CheckConflicts(read, keys);
    auto& data = this->data(read);
    if (!data.block) {
      ArenaAllocator<Block> alloc(arena);
      data.block = std::allocate_shared<Block>(alloc, context, alloc, metrics_internal[read]);
      if (read == last_conflict_was_read_) {
        auto old_value = this->data(!read).block->SetNext(data.block);
        if (old_value) {
          LOG(DFATAL) << "Opposite already had next block: "
                      << operation->call().serialized_request().ToDebugString();
        }
      }
    }
    data.block->AddOperation(operation);
    RememberKeys(read, &keys);
  }

 private:
  void ConflictFound(bool read) {
    auto& data = this->data(read);
    auto& opposite_data = this->data(!read);

    if (indeterminate(last_conflict_was_read_)) {
      flush_head_ = opposite_data.block;
      opposite_data.block->SetNext(data.block);
    } else {
      data.block = nullptr;
      data.used_keys.clear();
    }
    last_conflict_was_read_ = read;
  }

  void CheckConflicts(bool read, const RedisKeyList& keys) {
    if (last_conflict_was_read_ == read) {
      return;
    }
    auto& opposite = data(!read);
    bool conflict = false;
    for (const auto& key : keys) {
      if (opposite.used_keys.count(key)) {
        conflict = true;
        break;
      }
    }
    if (conflict) {
      ConflictFound(read);
    }
  }

  void RememberKeys(bool read, RedisKeyList* keys) {
    auto& dest = read ? read_data_ : write_data_;
    for (auto& key : *keys) {
      dest.used_keys.insert(std::move(key));
    }
  }

  BlockData read_data_;
  BlockData write_data_;
  std::shared_ptr<Block> flush_head_;

  // true - last conflict was read.
  // false - last conflict was write.
  // indeterminate - no conflict was found yet.
  boost::logic::tribool last_conflict_was_read_ = boost::logic::indeterminate;
};

class BatchContext : public RefCountedThreadSafe<BatchContext> {
 public:
  BatchContext(const std::shared_ptr<client::YBClient>& client,
               SessionPool* session_pools,
               const std::shared_ptr<RedisInboundCall>& call,
               rpc::RpcMethodMetrics* metrics_internal)
      : client_(client),
        session_pools_(session_pools),
        call_(call),
        metrics_internal_(metrics_internal),
        operations_(&arena_),
        tablets_(&arena_) {}

  const RedisClientCommand& command(size_t idx) const {
    return call_->client_batch()[idx];
  }

  const std::shared_ptr<RedisInboundCall>& call() const {
    return call_;
  }

  void Commit() {
    if (operations_.empty()) {
      return;
    }

    MonoTime deadline = MonoTime::FineNow() +
                        MonoDelta::FromMilliseconds(FLAGS_redis_service_yb_client_timeout_millis);
    lookups_left_.store(operations_.size(), std::memory_order_release);
    for (auto& operation : operations_) {
      client_->LookupTabletByKey(
          operation.operation().table(),
          operation.partition_key(),
          deadline,
          &operation.tablet(),
          Bind(&BatchContext::LookupDone, this, &operation));
    }
  }

  template <class Op>
  void Apply(size_t idx,
             std::shared_ptr<Op> op,
             const rpc::RpcMethodMetrics& metrics) {
    operations_.emplace_back(call_, idx, std::move(op), metrics);
    if (PREDICT_FALSE(operations_.back().responded())) {
      operations_.pop_back();
    }
  }

 private:
  void LookupDone(Operation* operation, const Status& status) {
    if (!status.ok()) {
      operation->Respond(status);
    }
    if (lookups_left_.fetch_sub(1, std::memory_order_acquire) != 1) {
      return;
    }

    BatchContextPtr self(this);
    for (auto& operation : operations_) {
      if (!operation.responded()) {
        auto it = tablets_.find(operation.tablet()->tablet_id());
        TabletOperations* operations;
        if (it == tablets_.end()) {
          it = tablets_.emplace(operation.tablet()->tablet_id(), TabletOperations(&arena_)).first;
          operations = &it->second;
        } else {
          operations = &it->second;
        }
        operations->Process(self, &arena_, &operation, metrics_internal_);
      }
    }

    for (auto& tablet : tablets_) {
      tablet.second.Done(session_pools_);
    }
  }

  std::shared_ptr<client::YBClient> client_;
  SessionPool* session_pools_;
  std::shared_ptr<RedisInboundCall> call_;
  rpc::RpcMethodMetrics* metrics_internal_;

  Arena arena_;
  MCDeque<Operation> operations_;
  std::atomic<size_t> lookups_left_;
  MCUnorderedMap<Slice, TabletOperations, Slice::Hash> tablets_;
};

template<class Op>
using Parser = Status(*)(Op*, const RedisClientCommand&);

// Information about RedisCommand(s) that we support.
//
// Based on "struct redisCommand" from redis/src/server.h
//
// The remaining fields in "struct redisCommand" from redis' implementation are
// currently unused. They will be added and when we start using them.
struct RedisCommandInfo {
  string name;
  std::function<void(const RedisCommandInfo&,
                     size_t,
                     BatchContext*)> functor;
  int arity;
  yb::rpc::RpcMethodMetrics metrics;
};

typedef std::shared_ptr<RedisCommandInfo> RedisCommandInfoPtr;

} // namespace

class RedisServiceImpl::Impl {
 public:
  Impl(RedisServer* server, string yb_tier_master_address);

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

  void LocalCommand(
      const RedisCommandInfo& info,
      size_t idx,
      RedisResponsePB (*parse)(const RedisClientCommand&),
      BatchContext* context);

  template<class Op>
  void Command(
      const RedisCommandInfo& info,
      size_t idx,
      Parser<Op> parser,
      BatchContext* context);

  constexpr static int kRpcTimeoutSec = 5;

  void PopulateHandlers();
  // Fetches the appropriate handler for the command, nullptr if none exists.
  const RedisCommandInfo* FetchHandler(const RedisClientCommand& cmd_args);
  CHECKED_STATUS SetUpYBClient();
  void RespondWithFailure(std::shared_ptr<RedisInboundCall> call,
                          size_t idx,
                          const std::string& error);

  std::deque<std::string> names_;
  std::unordered_map<Slice, RedisCommandInfoPtr, Slice::Hash> command_name_to_info_map_;
  yb::rpc::RpcMethodMetrics metrics_error_;
  std::array<yb::rpc::RpcMethodMetrics, 2> metrics_internal_;

  std::string yb_tier_master_addresses_;
  std::mutex yb_mutex_;  // Mutex that protects the creation of client_ and table_.
  std::atomic<bool> yb_client_initialized_;
  std::shared_ptr<client::YBClient> client_;
  std::array<SessionPool, 2> session_pools_;
  std::shared_ptr<client::YBTable> table_;

  RedisServer* server_;
};

RedisResponsePB ParseEcho(const RedisClientCommand& command) {
  RedisResponsePB responsePB;
  responsePB.set_code(RedisResponsePB_RedisStatusCode_OK);
  responsePB.set_string_response(command[1].ToBuffer());
  return responsePB;
}

RedisResponsePB ParseAuth(const RedisClientCommand& command) {
  return RedisResponsePB();
}

RedisResponsePB ParseConfig(const RedisClientCommand& command) {
  return RedisResponsePB();
}

void AddElements(const RefCntBuffer& buffer, RedisArrayPB* array) {
  array->add_elements(buffer.data(), buffer.size());
}

RedisResponsePB ParseRole(const RedisClientCommand& command) {
  RedisResponsePB responsePB;
  responsePB.set_code(RedisResponsePB_RedisStatusCode_OK);
  auto array_response = responsePB.mutable_array_response();
  AddElements(redisserver::EncodeAsBulkString("master"), array_response);
  AddElements(redisserver::EncodeAsInteger(0), array_response);
  array_response->add_elements(
      redisserver::EncodeAsArrayOfEncodedElements(std::initializer_list<std::string>()));
  array_response->set_encoded(true);
  return responsePB;
}

RedisResponsePB ParseInfo(const RedisClientCommand& command) {
  RedisResponsePB responsePB;
  responsePB.set_code(RedisResponsePB_RedisStatusCode_OK);
  responsePB.set_string_response(kInfoResponse);
  return responsePB;
}

RedisResponsePB ParsePing(const RedisClientCommand& command) {
  RedisResponsePB responsePB;
  responsePB.set_code(RedisResponsePB_RedisStatusCode_OK);
  if (command.size() > 1) {
    responsePB.set_string_response(command[1].cdata(), command[1].size());
  } else {
    responsePB.set_string_response("pong");
  }
  return responsePB;
}

RedisResponsePB ParseCommand(const RedisClientCommand& command) {
  return RedisResponsePB();
}

RedisResponsePB ParseQuit(const RedisClientCommand& command) {
  // TODO(hector): We need to implement its real meaning (close connection). Tracked by ENG-2107.
  return RedisResponsePB();
}

RedisResponsePB ParseFlushDB(const RedisClientCommand& command) {
  // TODO(hector): We need to implement its real meaning (delete all keys). Tracked by ENG-2107.
  return RedisResponsePB();
}

#define REDIS_METRIC(name) \
    BOOST_PP_CAT(METRIC_handler_latency_yb_redisserver_RedisServerService_, name)

#define READ_COMMAND Command<YBRedisReadOp>
#define WRITE_COMMAND Command<YBRedisWriteOp>
#define LOCAL_COMMAND LocalCommand

#define DO_POPULATE_HANDLER(name, cname, arity, type) \
  { \
    auto functor = [this](const RedisCommandInfo& info, \
                          size_t idx, \
                          BatchContext* context) { \
      BOOST_PP_CAT(type, _COMMAND)(info, idx, &BOOST_PP_CAT(Parse, cname), context); \
    }; \
    yb::rpc::RpcMethodMetrics metrics(REDIS_METRIC(name).Instantiate(metric_entity)); \
    SetupMethod({BOOST_PP_STRINGIZE(name), functor, arity, std::move(metrics)}); \
  } \
  /**/

#define POPULATE_HANDLER(z, data, elem) DO_POPULATE_HANDLER elem

void RedisServiceImpl::Impl::PopulateHandlers() {
  auto metric_entity = server_->metric_entity();
  BOOST_PP_SEQ_FOR_EACH(POPULATE_HANDLER, ~, REDIS_COMMANDS);

  // Set up metrics for erroneous calls.
  metrics_error_.handler_latency = REDIS_METRIC(error).Instantiate(metric_entity);
  metrics_internal_[false].handler_latency = REDIS_METRIC(set_internal).Instantiate(metric_entity);
  metrics_internal_[true].handler_latency = REDIS_METRIC(get_internal).Instantiate(metric_entity);
}

const RedisCommandInfo* RedisServiceImpl::Impl::FetchHandler(const RedisClientCommand& cmd_args) {
  if (cmd_args.size() < 1) {
    return nullptr;
  }
  Slice cmd_name = cmd_args[0];
  auto iter = command_name_to_info_map_.find(cmd_args[0]);
  if (iter == command_name_to_info_map_.end()) {
    LOG(ERROR) << "Command " << cmd_name << " not yet supported. "
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
    RETURN_NOT_OK(client_builder.Build(&client_));

    // Add proxy to call local tserver if available.
    if (server_->tserver() != nullptr && server_->tserver()->proxy() != nullptr) {
      client_->AddTabletServerProxy(
          server_->tserver()->permanent_uuid(), server_->tserver()->proxy());
    }

    const YBTableName table_name(kRedisKeyspaceName, kRedisTableName);
    RETURN_NOT_OK(client_->OpenTable(table_name, &table_));

    session_pools_[0].Init(client_, server_->metric_entity(), false);
    session_pools_[1].Init(client_, server_->metric_entity(), true);

    yb_client_initialized_.store(true, std::memory_order_release);
  }
  return Status::OK();
}

void RedisServiceImpl::Impl::Handle(rpc::InboundCallPtr call_ptr) {
  auto call = std::static_pointer_cast<RedisInboundCall>(call_ptr);

  DVLOG(4) << "Asked to handle a call " << call->ToString();

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
  auto context = make_scoped_refptr(new BatchContext(client_,
                                                     session_pools_.data(),
                                                     call,
                                                     metrics_internal_.data()));
  const auto& batch = call->client_batch();
  for (size_t idx = 0; idx != batch.size(); ++idx) {
    const RedisClientCommand& c = batch[idx];

    auto cmd_info = FetchHandler(c);

    // Handle the current redis command.
    if (cmd_info == nullptr) {
      RespondWithFailure(call, idx, "Unsupported call.");
    } else if (cmd_info->arity < 0 && c.size() < static_cast<size_t>(-1 * cmd_info->arity)) {
      // -X means that the command needs >= X arguments.
      LOG(ERROR) << "Requested command " << c[0] << " does not have enough arguments."
                 << " At least " << -cmd_info->arity << " expected, but " << c.size()
                 << " found.";
      RespondWithFailure(call, idx, "Too few arguments.");
    } else if (cmd_info->arity > 0 && c.size() != cmd_info->arity) {
      // X (> 0) means that the command needs exactly X arguments.
      LOG(ERROR) << "Requested command " << c[0] << " has wrong number of arguments.";
      RespondWithFailure(call, idx, "Wrong number of arguments.");
    } else {
      // Handle the call.
      cmd_info->functor(*cmd_info, idx, context.get());
    }
  }
  context->Commit();
}

void RedisServiceImpl::Impl::LocalCommand(
    const RedisCommandInfo& info,
    size_t idx,
    RedisResponsePB (*parse)(const RedisClientCommand&),
    BatchContext* context) {
  const auto& command = context->command(idx);
  RedisResponsePB local_response = parse(command);
  VLOG_IF(4, local_response.has_string_response()) << "Responding to " << command[0].ToBuffer()
                                                   << " with " << local_response.string_response();
  context->call()->RespondSuccess(idx, info.metrics, &local_response);
  VLOG(4) << "Done responding to " << command[0].ToBuffer();
}

template<class Op>
void RedisServiceImpl::Impl::Command(
    const RedisCommandInfo& info,
    size_t idx,
    Parser<Op> parser,
    BatchContext* context) {
  VLOG(1) << "Processing " << info.name << ".";

  auto op = std::make_shared<Op>(table_);
  const auto& command = context->command(idx);
  Status s = parser(op.get(), command);
  if (!s.ok()) {
    RespondWithFailure(context->call(), idx, s.message().ToBuffer());
    return;
  }
  context->Apply(idx, std::move(op), info.metrics);
}

void RedisServiceImpl::Impl::RespondWithFailure(
    std::shared_ptr<RedisInboundCall> call,
    size_t idx,
    const std::string& error) {
  // process the request
  DVLOG(4) << " Processing request from client ";
  const auto& command = call->client_batch()[idx];
  size_t size = command.size();
  for (size_t i = 0; i < size; i++) {
    DVLOG(4) << i + 1 << " / " << size << " : " << command[i].ToDebugString(8);
  }

  // Send the result.
  DVLOG(4) << "Responding to call " << call->ToString() << " with failure " << error;
  std::string cmd = command[0].ToBuffer();
  call->RespondFailure(idx, STATUS_FORMAT(RuntimeError, "$0: $1", cmd, error));
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
