// Copyright (c) YugaByte, Inc.

#include "yb/client/redis_helpers.h"

#include <memory>
#include <string>

#include "yb/client/client.h"
#include "yb/client/write_op.h"
#include "yb/common/redis_protocol.pb.h"

namespace yb {
namespace client {

using yb::client::YBTable;
using yb::client::RedisWriteOp;

const char* RedisConstants::kRedisTableName = ".redis";
const char* RedisConstants::kRedisKeyColumnName = "key_column";

unique_ptr<RedisWriteOp> RedisWriteOpForSetKV(YBTable* table, string key, string value) {
  unique_ptr<RedisWriteOp> redis_write_to_yb(table->NewRedisWrite());
  CHECK_OK(redis_write_to_yb->mutable_row()->SetBinary(RedisConstants::kRedisKeyColumnName, key));
  RedisWriteRequestPB* write_request_pb = redis_write_to_yb->mutable_request();
  write_request_pb->set_redis_op_type(RedisWriteRequestPB::SET);
  auto mutable_key_value = write_request_pb->mutable_set_request()->mutable_key_value();
  mutable_key_value->set_key(key);
  mutable_key_value->add_value(value);
  return redis_write_to_yb;
}

}  // namespace client
}  // namespace yb
