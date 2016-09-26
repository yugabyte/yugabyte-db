// Copyright (c) YugaByte, Inc.

#ifndef YB_REDISSERVER_REDIS_SERVICE_H_
#define YB_REDISSERVER_REDIS_SERVICE_H_

#include "yb/redisserver/redis_service.service.h"

#include <vector>
#include <yb/util/string_case.h>

#include "yb/rpc/transfer.h"

using std::string;

namespace yb {
namespace redisserver {

class RedisServer;

class RedisServiceImpl : public RedisServerServiceIf {
 public:
  explicit RedisServiceImpl(RedisServer* server);

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

  // Redis command table, for commands that we currently support.
  //
  // Based on  "redisCommandTable[]" from redis/src/server.c
  // kMethodCount has to reflect the correct number of commands in the table.
  //
  // Every entry is composed of the following fields:
  //   name: a string representing the command name.
  //   function: pointer to the member function implementing the command.
  //   arity: number of arguments expected, it is possible to use -N to say >= N.
  constexpr static int kMethodCount = 3;
  const struct RedisCommandInfo kRedisCommandTable[kMethodCount] = {
      {"get", &RedisServiceImpl::GetCommand, 2},
      {"set", &RedisServiceImpl::SetCommand, -3},
      {"echo", &RedisServiceImpl::EchoCommand, 2}};

  void PopulateHandlers();
  RedisCommandFunctionPtr FetchHandler(const std::vector<Slice>& cmd_args);

  yb::rpc::RpcMethodMetrics metrics_[kMethodCount];
  std::map<string, const RedisCommandInfo*> command_name_to_info_map_;
};

}  // namespace redisserver
}  // namespace yb

#endif  // YB_REDISSERVER_REDIS_SERVICE_H_
