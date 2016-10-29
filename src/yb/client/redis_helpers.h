// Copyright (c) YugaByte, Inc.
#ifndef YB_CLIENT_REDIS_HELPERS_H_
#define YB_CLIENT_REDIS_HELPERS_H_

#include <memory>
#include <string>

class YBTable;
class YBRedisWriteOp;
class YBRedisReadOp;

namespace yb {
namespace client {

using std::unique_ptr;
using std::string;

std::shared_ptr<YBRedisWriteOp> RedisWriteOpForSetKV(YBTable* table, string key, string value);
std::shared_ptr<YBRedisReadOp> RedisReadOpForGetKey(YBTable* table, string key);

class RedisConstants {
 public:
  static const char* kRedisTableName;
  static const char* kRedisKeyColumnName;
};

}  // namespace client
}  // namespace yb

#endif  // YB_CLIENT_REDIS_HELPERS_H_
