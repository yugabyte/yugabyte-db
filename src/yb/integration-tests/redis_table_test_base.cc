// Copyright (c) YugaByte, Inc.

#include "yb/integration-tests/redis_table_test_base.h"

#include <glog/logging.h>

#include "yb/client/client.h"
#include "yb/common/redis_protocol.pb.h"
#include "yb/integration-tests/yb_table_test_base.h"
#include "yb/client/redis_helpers.h"

using std::string;
using std::vector;
using std::unique_ptr;

namespace yb {
namespace integration_tests {

using client::RedisConstants;
using client::YBRedisWriteOp;
using client::YBRedisReadOp;
using client::RedisWriteOpForSetKV;
using client::YBColumnSchema;
using client::YBTableCreator;
using client::YBSchemaBuilder;
using client::YBColumnSchema;
using client::YBTableType;
using client::YBSession;

std::string RedisTableTestBase::table_name() { return RedisConstants::kRedisTableName; }

void RedisTableTestBase::CreateTable() {
  if (!table_exists_) {
    CreateRedisTable(client_, table_name());
    table_exists_ = true;
  }
}

void RedisTableTestBase::PutKeyValue(YBSession* session, string key, string value) {
  ASSERT_OK(session->Apply(RedisWriteOpForSetKV(table_.get(), key, value)));
  ASSERT_OK(session->Flush());
}

void RedisTableTestBase::GetKeyValue(string key, string value) {
  shared_ptr<YBRedisReadOp> read_op = RedisReadOpForGetKey(table_.get(), key);
  ASSERT_OK(session_->ReadSync(read_op));
  ASSERT_EQ(read_op->response().string_response(), value);
}

void RedisTableTestBase::RedisSimpleSetCommands() {
  YBTableTestBase::PutKeyValue("key123", "value123");
  YBTableTestBase::PutKeyValue("key200", "value200");
  YBTableTestBase::PutKeyValue("key300", "value300");
}

void RedisTableTestBase::RedisSimpleGetCommands() {
  session_ = NewSession(true);
  RedisTableTestBase::GetKeyValue("key123", "value123");
  RedisTableTestBase::GetKeyValue("key200", "value200");
  RedisTableTestBase::GetKeyValue("key300", "value300");
}

}  // namespace integration_tests
}  // namespace yb
