// Copyright (c) YugaByte, Inc.

#ifndef YB_REDISSERVER_REDIS_SERVICE_H_
#define YB_REDISSERVER_REDIS_SERVICE_H_

#include "yb/redisserver/redis_service.service.h"

#include <memory>
#include <vector>

#include "yb/rpc/transfer.h"
#include "yb/util/string_case.h"

using std::string;
using std::shared_ptr;

namespace yb {

namespace client {
class YBClient;
class YBTable;
class YBSession;
}  // namespace client

namespace redisserver {

class RedisServer;

class RedisServiceImpl : public RedisServerServiceIf {
 public:
  RedisServiceImpl(RedisServer* server, string yb_tier_master_address);

  void Handle(yb::rpc::InboundCall* call) override;

  void GetCommand(yb::rpc::InboundCall* call, yb::rpc::RedisClientCommand* c);

  void SetCommand(yb::rpc::InboundCall* call, yb::rpc::RedisClientCommand* c);

  void EchoCommand(yb::rpc::InboundCall* call, yb::rpc::RedisClientCommand* c);

  void DummyCommand(yb::rpc::InboundCall* call, yb::rpc::RedisClientCommand* c);

 private:
  typedef void (RedisServiceImpl::*RedisCommandFunctionPtr)(yb::rpc::InboundCall* call,
                                                            rpc::RedisClientCommand* c);

  // Information about RedisCommand(s) that we support.
  //
  // Based on "struct redisCommand" from redis/src/server.h
  //
  // The remaining fields in "struct redisCommand" from redis' implementation are
  // currently unused. They will be added and when we start using them.
  struct RedisCommandInfo {
    RedisCommandInfo(const string& name, const RedisCommandFunctionPtr& fptr, int arity)
        : name(name), function_ptr(fptr), arity(arity) {
      ToLowerCase(this->name, &this->name);
    }
    string name;
    RedisCommandFunctionPtr function_ptr;
    int arity;

   private:
    // Declare this private to ensure that we get a compiler error if kMethodCount is not the
    // size of kRedisCommandTable.
    RedisCommandInfo();
  };

  constexpr static int kRpcTimeoutSec = 5;
  constexpr static int kMethodCount = 3;

  void PopulateHandlers();
  RedisCommandFunctionPtr FetchHandler(const std::vector<Slice>& cmd_args);
  void SetUpYBClient(string yb_master_address);

  // Redis command table, for commands that we currently support.
  //
  // Based on  "redisCommandTable[]" from redis/src/server.c
  // kMethodCount has to reflect the correct number of commands in the table.
  //
  // Every entry is composed of the following fields:
  //   name: a string representing the command name.
  //   function: pointer to the member function implementing the command.
  //   arity: number of arguments expected, it is possible to use -N to say >= N.
  const struct RedisCommandInfo kRedisCommandTable[kMethodCount] = {
      {"get", &RedisServiceImpl::GetCommand, 2},
      {"set", &RedisServiceImpl::SetCommand, -3},
      {"echo", &RedisServiceImpl::EchoCommand, 2}};

  yb::rpc::RpcMethodMetrics metrics_[kMethodCount];
  std::map<string, const RedisCommandInfo*> command_name_to_info_map_;

  shared_ptr<client::YBClient> client_;
  shared_ptr<client::YBTable> table_;
  shared_ptr<client::YBSession> session_;
  shared_ptr<client::YBSession> read_only_session_;

  RedisServer* server_;
};

}  // namespace redisserver
}  // namespace yb

#endif  // YB_REDISSERVER_REDIS_SERVICE_H_
