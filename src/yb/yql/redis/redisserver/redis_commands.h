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

#pragma once

#include <functional>
#include <string>
#include <vector>
#include <unordered_set>

#include "yb/client/client_fwd.h"

#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/service_if.h"

#include "yb/yql/redis/redisserver/redis_fwd.h"
#include "yb/yql/redis/redisserver/redis_server.h"

namespace yb {
namespace redisserver {

typedef boost::function<void(const Status&)> StatusFunctor;
typedef boost::function<void(int i)> IntFunctor;

class RedisConnectionContext;

YB_STRONGLY_TYPED_BOOL(AsPattern);

class RedisServiceData {
 public:
  // Used for Monitor.
  virtual void AppendToMonitors(rpc::Connection* conn) = 0;
  virtual void RemoveFromMonitors(rpc::Connection* conn) = 0;
  virtual void LogToMonitors(
      const std::string& end, const std::string& db, const RedisClientCommand& cmd) = 0;

  // Used for PubSub.
  virtual void AppendToSubscribers(
      AsPattern type, const std::vector<std::string>& channels, rpc::Connection* conn,
      std::vector<size_t>* subs) = 0;
  virtual void RemoveFromSubscribers(
      AsPattern type, const std::vector<std::string>& channels, rpc::Connection* conn,
      std::vector<size_t>* subs) = 0;
  virtual size_t NumSubscribers(AsPattern type, const std::string& channel) = 0;
  virtual void CleanUpSubscriptions(rpc::Connection* conn) = 0;
  virtual std::unordered_set<std::string> GetSubscriptions(
      AsPattern type, rpc::Connection* conn) = 0;
  virtual std::unordered_set<std::string> GetAllSubscriptions(AsPattern type) = 0;
  virtual void ForwardToInterestedProxies(
      const std::string& channel, const std::string& message, const IntFunctor& f) = 0;

  // Used for Auth.
  virtual Status GetRedisPasswords(std::vector<std::string>* passwords) = 0;

  // Used for Select.
  virtual yb::Result<std::shared_ptr<client::YBTable>> GetYBTableForDB(
      const std::string& db_name) = 0;

  static client::YBTableName GetYBTableNameForRedisDatabase(const std::string& db_name);

  virtual ~RedisServiceData() {}
};

YB_STRONGLY_TYPED_BOOL(ManualResponse);

// Context for batch of Redis commands.
class BatchContext : public RefCountedThreadSafe<BatchContext> {
 public:
  virtual std::shared_ptr<client::YBTable> table() = 0;
  virtual const RedisClientCommand& command(size_t idx) const = 0;
  virtual const std::shared_ptr<RedisInboundCall>& call() const = 0;
  virtual client::YBClient* client() const = 0;
  virtual const RedisServer* server() = 0;
  virtual RedisServiceData* service_data() = 0;
  virtual void CleanYBTableFromCache() = 0;

  virtual void Apply(
      size_t index,
      std::shared_ptr<client::YBRedisReadOp> operation,
      const rpc::RpcMethodMetrics& metrics) = 0;

  virtual void Apply(
      size_t index,
      std::shared_ptr<client::YBRedisWriteOp> operation,
      const rpc::RpcMethodMetrics& metrics) = 0;

  virtual void Apply(
      size_t index,
      std::function<bool(client::YBSession*, const StatusFunctor&)> functor,
      std::string partition_key,
      const rpc::RpcMethodMetrics& metrics,
      ManualResponse manual_response) = 0;

  virtual ~BatchContext() {}
};

typedef scoped_refptr<BatchContext> BatchContextPtr;

// Information about RedisCommand(s) that we support.
struct RedisCommandInfo {
  std::string name;
  // The following arguments should be passed to this functor:
  // Info about its command.
  // Index of call in batch.
  // Batch context.
  std::function<void(const RedisCommandInfo&,
                     size_t,
                     BatchContext*)> functor;
  // Positive arity means that we expect exactly arity-1 arguments and negative arity means
  // that we expect at least -arity-1 arguments.
  int arity;
  yb::rpc::RpcMethodMetrics metrics;
};

typedef std::shared_ptr<RedisCommandInfo> RedisCommandInfoPtr;

void RespondWithFailure(
    std::shared_ptr<RedisInboundCall> call,
    size_t idx,
    const std::string& error,
    const char* error_code = "ERR");

void FillRedisCommands(const scoped_refptr<MetricEntity>& metric_entity,
                       const std::function<void(const RedisCommandInfo& info)>& setup_method);

#define YB_REDIS_METRIC(name) \
    BOOST_PP_CAT(METRIC_handler_latency_yb_redisserver_RedisServerService_, name)

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

} // namespace redisserver
} // namespace yb
