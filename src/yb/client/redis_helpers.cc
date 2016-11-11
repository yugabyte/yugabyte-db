// Copyright (c) YugaByte, Inc.

#include "yb/client/redis_helpers.h"

#include <memory>
#include <string>

#include "yb/client/client.h"
#include "yb/client/yb_op.h"
#include "yb/common/redis_protocol.pb.h"

namespace yb {
namespace client {

using yb::client::YBTable;
using yb::client::YBRedisWriteOp;
using std::shared_ptr;
using std::string;

const char* RedisConstants::kRedisTableName = ".redis";
const char* RedisConstants::kRedisKeyColumnName = "key";

shared_ptr<YBRedisWriteOp> RedisWriteOpForSetKV(
    YBTable* table, const string& key, const string& value, int64_t ttl) {
  shared_ptr<YBRedisWriteOp> redis_write_to_yb(table->NewRedisWrite());
  CHECK_OK(redis_write_to_yb->mutable_row()->SetBinary(RedisConstants::kRedisKeyColumnName, key));
  RedisWriteRequestPB* write_request_pb = redis_write_to_yb->mutable_request();
  write_request_pb->set_redis_op_type(RedisWriteRequestPB::SET);
  auto mutable_key_value = write_request_pb->mutable_set_request()->mutable_key_value();
  mutable_key_value->set_key(key);
  mutable_key_value->add_value(value);
  if (ttl != kNoneTtl) {
    CHECK_GT(ttl, 0);
    write_request_pb->mutable_set_request()->set_ttl(ttl);
  }
  return redis_write_to_yb;
}

shared_ptr<YBRedisReadOp> RedisReadOpForGetKey(YBTable* table, const string& key) {
  shared_ptr<YBRedisReadOp> redis_read_to_yb(table->NewRedisRead());
  CHECK_OK(redis_read_to_yb->mutable_row()->SetBinary(RedisConstants::kRedisKeyColumnName, key));
  RedisReadRequestPB* read_request_pb = redis_read_to_yb->mutable_request();
  read_request_pb->set_redis_op_type(RedisReadRequestPB::GET);
  auto mutable_key_value = read_request_pb->mutable_get_request()->mutable_key_value();
  mutable_key_value->set_key(key);
  return redis_read_to_yb;
}

}  // namespace client
}  // namespace yb
