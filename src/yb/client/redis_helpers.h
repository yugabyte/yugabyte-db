// Copyright (c) YugaByte, Inc.
#ifndef YB_CLIENT_REDIS_HELPERS_H_
#define YB_CLIENT_REDIS_HELPERS_H_

#include <memory>
#include <string>
#include "yb/util/slice.h"

namespace yb {
namespace client {

class YBTable;
class YBRedisWriteOp;
class YBRedisReadOp;

constexpr int64_t kNoneTtl = -1;

std::shared_ptr<YBRedisWriteOp> RedisWriteOpForSetKV(
    YBTable* table, const std::string& key, const std::string& value, int64_t ttl_msec = kNoneTtl);
std::shared_ptr<YBRedisWriteOp> RedisWriteOpForSetKV(YBTable* table, const std::vector<Slice> args);
std::shared_ptr<YBRedisReadOp> RedisReadOpForGetKey(YBTable* table, const std::string& key);

class RedisConstants {
 public:
  static const char* kRedisTableName;
  static const char* kRedisKeyColumnName;
};

}  // namespace client
}  // namespace yb

#endif  // YB_CLIENT_REDIS_HELPERS_H_
