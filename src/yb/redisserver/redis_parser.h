// Copyright (c) YugaByte, Inc.
#ifndef YB_REDISSERVER_REDIS_PARSER_H_
#define YB_REDISSERVER_REDIS_PARSER_H_

#include <memory>
#include <string>
#include "yb/util/slice.h"
#include "yb/util/status.h"
#include "yb/client/async_rpc.h"
#include "yb/client/callbacks.h"
#include "yb/client/client.h"
#include "yb/client/client_builder-internal.h"

namespace yb {
namespace redisserver {

constexpr int64_t kNoneTtl = -1;

CHECKED_STATUS ParseSet(yb::client::YBRedisWriteOp *op, const std::vector<Slice> &args);
CHECKED_STATUS ParseHSet(yb::client::YBRedisWriteOp *op, const std::vector<Slice> &args);
CHECKED_STATUS ParseGetSet(yb::client::YBRedisWriteOp *op, const std::vector<Slice> &args);
CHECKED_STATUS ParseAppend(yb::client::YBRedisWriteOp* op, const std::vector<Slice>& args);
CHECKED_STATUS ParseDel(yb::client::YBRedisWriteOp* op, const std::vector<Slice>& args);
CHECKED_STATUS ParseSetRange(yb::client::YBRedisWriteOp* op, const std::vector<Slice>& args);
CHECKED_STATUS ParseIncr(yb::client::YBRedisWriteOp* op, const std::vector<Slice>& args);

CHECKED_STATUS ParseGet(yb::client::YBRedisReadOp* op, const std::vector<Slice>& args);
CHECKED_STATUS ParseHGet(yb::client::YBRedisReadOp *op, const std::vector<Slice> &args);
CHECKED_STATUS ParseStrLen(yb::client::YBRedisReadOp* op, const std::vector<Slice>& args);
CHECKED_STATUS ParseExists(yb::client::YBRedisReadOp* op, const std::vector<Slice>& args);
CHECKED_STATUS ParseGetRange(yb::client::YBRedisReadOp* op, const std::vector<Slice>& args);

// TODO: make additional command support here

}  // namespace redisserver
}  // namespace yb

#endif  // YB_REDISSERVER_REDIS_PARSER_H_
