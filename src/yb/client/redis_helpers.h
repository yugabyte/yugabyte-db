// Copyright (c) YugaByte, Inc.
#ifndef YB_CLIENT_REDIS_HELPERS_H_
#define YB_CLIENT_REDIS_HELPERS_H_

#include <memory>
#include <string>

class YBTable;
class RedisWriteOp;

namespace yb {
namespace client {

using std::unique_ptr;
using std::string;

unique_ptr<RedisWriteOp> RedisWriteOpForSetKV(YBTable *table, string key, string value);

class RedisConstants {
 public:
  static const char* kRedisTableName;
  static const char* kRedisKeyColumnName;
};

}  // namespace client
}  // namespace yb

#endif  // YB_CLIENT_REDIS_HELPERS_H_
