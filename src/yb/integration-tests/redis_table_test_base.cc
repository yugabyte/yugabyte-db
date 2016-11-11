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

void RedisTableTestBase::PutKeyValue(string key, string value) {
  ASSERT_OK(session_->Apply(RedisWriteOpForSetKV(table_.get(), key, value)));
  ASSERT_OK(session_->Flush());
}

void RedisTableTestBase::PutKeyValue(string key, string value, int64_t ttl) {
  ASSERT_OK(session_->Apply(RedisWriteOpForSetKV(table_.get(), key, value, ttl)));
  ASSERT_OK(session_->Flush());
}

void RedisTableTestBase::GetKeyValue(const string& key, const string& value, bool expect_not_found) {
  shared_ptr<YBRedisReadOp> read_op = RedisReadOpForGetKey(table_.get(), key);
  ASSERT_OK(session_->ReadSync(read_op));
  if (expect_not_found) {
    ASSERT_EQ(RedisResponsePB_RedisStatusCode_NOT_FOUND, read_op->response().code());
  } else {
    ASSERT_EQ(RedisResponsePB_RedisStatusCode_OK, read_op->response().code());
    ASSERT_EQ(read_op->response().string_response(), value);
  }
}

void RedisTableTestBase::RedisSimpleSetCommands() {
  session_ = NewSession(/* read_only = */ false);
  PutKeyValue("key123", "value123");
  PutKeyValue("key200", "value200");
  PutKeyValue("key300", "value300");
}

void RedisTableTestBase::RedisSimpleGetCommands() {
  session_ = NewSession(/* read_only = */ true);
  GetKeyValue("key123", "value123");
  GetKeyValue("key200", "value200");
  GetKeyValue("key300", "value300");
  GetKeyValue("key400", "value400", true);
}

void RedisTableTestBase::RedisTtlSetCommands() {
  session_ = NewSession(/* read_only = */ false);
  PutKeyValue("key456", "value456", 500000);
  PutKeyValue("key567", "value567", 1500000);
  PutKeyValue("key678", "value678");
}

void RedisTableTestBase::RedisTtlGetCommands() {
  session_ = NewSession(/* read_only = */ true);
  GetKeyValue("key456", "value456", true);
  GetKeyValue("key567", "value567", false);
  GetKeyValue("key678", "value678", false);
}

}  // namespace integration_tests
}  // namespace yb
