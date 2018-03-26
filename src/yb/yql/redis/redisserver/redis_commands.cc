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

#include "yb/yql/redis/redisserver/redis_commands.h"

#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/stringize.hpp>

#include "yb/client/client.h"
#include "yb/client/yb_op.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/scheduler.h"

#include "yb/util/metrics.h"
#include "yb/util/stol_utils.h"

#include "yb/yql/redis/redisserver/redis_encoding.h"
#include "yb/yql/redis/redisserver/redis_rpc.h"

using namespace std::literals;

namespace yb {
namespace redisserver {

#define REDIS_COMMANDS \
    ((get, Get, 2, READ)) \
    ((mget, MGet, -2, READ)) \
    ((hget, HGet, 3, READ)) \
    ((tsget, TsGet, 3, READ)) \
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
    ((zcard, ZCard, 2, READ)) \
    ((set, Set, -3, WRITE)) \
    ((mset, MSet, -3, WRITE)) \
    ((hset, HSet, 4, WRITE)) \
    ((hmset, HMSet, -4, WRITE)) \
    ((hincrby, HIncrBy, 4, WRITE)) \
    ((hdel, HDel, -3, WRITE)) \
    ((sadd, SAdd, -3, WRITE)) \
    ((srem, SRem, -3, WRITE)) \
    ((tsadd, TsAdd, -4, WRITE)) \
    ((tsrangebytime, TsRangeByTime, 4, READ)) \
    ((tslastn, TsLastN, 3, READ)) \
    ((tscard, TsCard, 2, READ)) \
    ((zrangebyscore, ZRangeByScore, -4, READ)) \
    ((zrevrange, ZRevRange, -4, READ)) \
    ((tsrem, TsRem, -3, WRITE)) \
    ((zrem, ZRem, -3, WRITE)) \
    ((zadd, ZAdd, -4, WRITE)) \
    ((getset, GetSet, 3, WRITE)) \
    ((append, Append, 3, WRITE)) \
    ((del, Del, 2, WRITE)) \
    ((setrange, SetRange, 4, WRITE)) \
    ((incr, Incr, 2, WRITE)) \
    ((incrby, IncrBy, 3, WRITE)) \
    ((echo, Echo, 2, LOCAL)) \
    ((auth, Auth, -1, LOCAL)) \
    ((config, Config, -1, LOCAL)) \
    ((info, Info, -1, LOCAL)) \
    ((role, Role, 1, LOCAL)) \
    ((ping, Ping, -1, LOCAL)) \
    ((command, Command, -1, LOCAL)) \
    ((quit, Quit, 1, LOCAL)) \
    ((flushdb, FlushDB, 1, LOCAL)) \
    ((flushall, FlushAll, 1, LOCAL)) \
    ((debugsleep, DebugSleep, 2, LOCAL)) \
    /**/

#define DO_DEFINE_HISTOGRAM(name, cname, arity, type) \
  DEFINE_REDIS_histogram(name, BOOST_PP_STRINGIZE(cname));
#define DEFINE_HISTOGRAM(r, data, elem) DO_DEFINE_HISTOGRAM elem

BOOST_PP_SEQ_FOR_EACH(DEFINE_HISTOGRAM, ~, REDIS_COMMANDS)

#define READ_OP yb::client::YBRedisReadOp
#define WRITE_OP yb::client::YBRedisWriteOp
#define LOCAL_OP RedisResponsePB

#define DO_PARSER_FORWARD(name, cname, arity, type) \
    CHECKED_STATUS BOOST_PP_CAT(Parse, cname)( \
        BOOST_PP_CAT(type, _OP) *op, \
        const RedisClientCommand& args);
#define PARSER_FORWARD(r, data, elem) DO_PARSER_FORWARD elem

BOOST_PP_SEQ_FOR_EACH(PARSER_FORWARD, ~, REDIS_COMMANDS)

namespace {

template<class Op>
using Parser = Status(*)(Op*, const RedisClientCommand&);

template<class Op>
void Command(
    const RedisCommandInfo& info,
    size_t idx,
    Parser<Op> parser,
    BatchContext* context) {
  VLOG(1) << "Processing " << info.name << ".";

  auto op = std::make_shared<Op>(context->table());
  const auto& command = context->command(idx);
  Status s = parser(op.get(), command);
  if (!s.ok()) {
    RespondWithFailure(context->call(), idx, s.message().ToBuffer());
    return;
  }
  context->Apply(idx, std::move(op), info.metrics);
}

#define READ_COMMAND(cname) \
    Command<yb::client::YBRedisReadOp>(info, idx, &BOOST_PP_CAT(Parse, cname), context)
#define WRITE_COMMAND(cname) \
    Command<yb::client::YBRedisWriteOp>(info, idx, &BOOST_PP_CAT(Parse, cname), context)
#define LOCAL_COMMAND(cname) \
    BOOST_PP_CAT(Handle, cname)({info, idx, context}); \

#define DO_POPULATE_HANDLER(name, cname, arity, type) \
  { \
    auto functor = [](const RedisCommandInfo& info, \
                      size_t idx, \
                      BatchContext* context) { \
      BOOST_PP_CAT(type, _COMMAND)(cname); \
    }; \
    yb::rpc::RpcMethodMetrics metrics(YB_REDIS_METRIC(name).Instantiate(metric_entity)); \
    setup_method({BOOST_PP_STRINGIZE(name), functor, arity, std::move(metrics)}); \
  } \
  /**/

#define POPULATE_HANDLER(z, data, elem) DO_POPULATE_HANDLER elem

class LocalCommandData {
 public:
  LocalCommandData(const RedisCommandInfo& info,
                   size_t idx,
                   BatchContext* context)
      : info_(info), idx_(idx), context_(context) {}

  const RedisClientCommand& command() const {
    return context_->command(idx_);
  }

  Slice arg(size_t i) const {
    return command()[i];
  }

  size_t arg_size() const {
    return command().size();
  }

  const std::shared_ptr<RedisInboundCall>& call() const {
    return context_->call();
  }

  const std::shared_ptr<client::YBClient>& client() const {
    return context_->client();
  }

  client::YBTable* table() const {
    return context_->table().get();
  }

  const BatchContextPtr& context() const {
    return context_;
  }

  template<class Functor>
  void Apply(const Functor& functor, const std::string& partition_key) {
    context_->Apply(idx_, functor, partition_key, info_.metrics);
  }

  void Respond(RedisResponsePB* response = nullptr) const {
    if (response == nullptr) {
      RedisResponsePB temp;
      Respond(&temp);
      return;
    }
    const auto& cmd = command();
    VLOG_IF(4, response->has_string_response()) << "Responding to " << cmd[0].ToBuffer()
                                                << " with " << response->string_response();
    context_->call()->RespondSuccess(idx_, info_.metrics, response);
    VLOG(4) << "Done responding to " << cmd[0].ToBuffer();
  }

 private:
  const RedisCommandInfo& info_;
  size_t idx_;
  BatchContextPtr context_;
};

void HandleEcho(LocalCommandData data) {
  RedisResponsePB response;
  response.set_code(RedisResponsePB::OK);
  response.set_string_response(data.arg(1).ToBuffer());
  data.Respond(&response);
}

void HandleAuth(LocalCommandData data) {
  data.Respond();
}

void HandleConfig(LocalCommandData data) {
  data.Respond();
}

void AddElements(const RefCntBuffer& buffer, RedisArrayPB* array) {
  array->add_elements(buffer.data(), buffer.size());
}

void HandleRole(LocalCommandData data) {
  RedisResponsePB response;
  response.set_code(RedisResponsePB::OK);
  auto array_response = response.mutable_array_response();
  AddElements(redisserver::EncodeAsBulkString("master"), array_response);
  AddElements(redisserver::EncodeAsInteger(0), array_response);
  array_response->add_elements(
      redisserver::EncodeAsArrayOfEncodedElements(std::initializer_list<std::string>()));
  array_response->set_encoded(true);
  data.Respond(&response);
}

void HandleInfo(LocalCommandData data) {
  RedisResponsePB response;
  response.set_code(RedisResponsePB::OK);
  response.set_string_response(kInfoResponse);
  data.Respond(&response);
}

void HandlePing(LocalCommandData data) {
  RedisResponsePB response;
  response.set_code(RedisResponsePB::OK);
  if (data.arg_size() > 1) {
    response.set_string_response(data.arg(1).cdata(), data.arg(1).size());
  } else {
    response.set_string_response("PONG");
  }
  data.Respond(&response);
}

void HandleCommand(LocalCommandData data) {
  data.Respond();
}

void HandleQuit(LocalCommandData data) {
  data.call()->MarkForClose();
  data.Respond();
}

void HandleFlushDB(LocalCommandData data) {
  RedisResponsePB resp;
  const Status s = data.client()->TruncateTable(data.table()->id());
  if (s.ok()) {
    resp.set_code(RedisResponsePB_RedisStatusCode_OK);
  } else {
    const Slice message = s.message();
    resp.set_code(RedisResponsePB_RedisStatusCode_SERVER_ERROR);
    resp.set_error_message(message.data(), message.size());
  }
  data.Respond(&resp);
}

void HandleFlushAll(LocalCommandData data) {
  HandleFlushDB(data);
}

void HandleDebugSleep(LocalCommandData data) {
  struct SleepWaiter {
    std::chrono::steady_clock::time_point end;
    StatusFunctor callback;
    LocalCommandData data;

    void operator()(const Status& status) const {
      if (!status.ok()) {
        callback(status);
        return;
      }
      if (data.call()->aborted()) {
        callback(STATUS(Aborted, ""));
        return;
      }
      auto now = std::chrono::steady_clock::now();
      if (now >= end) {
        callback(Status::OK());
        return;
      }
      data.context()->client()->messenger()->scheduler().Schedule(
          *this, std::min(end, now + 100ms));
    }
  };

  auto time_ms = util::CheckedStoll(data.arg(1));
  if (!time_ms.ok()) {
    RedisResponsePB resp;
    resp.set_code(RedisResponsePB::PARSING_ERROR);
    const Slice message = time_ms.status().message();
    resp.set_error_message(message.data(), message.size());
    data.Respond(&resp);
  }

  auto now = std::chrono::steady_clock::now();
  auto functor = [end = now + std::chrono::milliseconds(*time_ms),
                  data](const StatusFunctor& callback) {
    SleepWaiter waiter{ end, callback, data };
    waiter(Status::OK());
    return true;
  };

  data.Apply(functor, std::string());
}

} // namespace

void RespondWithFailure(
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
  call->RespondFailure(idx, STATUS_FORMAT(InvalidCommand, "ERR $0: $1", cmd, error));
}

void FillRedisCommands(const scoped_refptr<MetricEntity>& metric_entity,
                       const std::function<void(const RedisCommandInfo& info)>& setup_method) {
  BOOST_PP_SEQ_FOR_EACH(POPULATE_HANDLER, ~, REDIS_COMMANDS);
}

} // namespace redisserver
} // namespace yb
