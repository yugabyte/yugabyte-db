// Copyright (c) YugaByte, Inc.

#include "yb/integration-tests/redis_table_test_base.h"

#include <glog/logging.h>

#include "yb/client/client.h"
#include "yb/common/redis_protocol.pb.h"
#include "yb/integration-tests/yb_table_test_base.h"
#include "yb/redisserver/redis_constants.h"
#include "yb/redisserver/redis_parser.h"

using std::string;
using std::vector;
using std::unique_ptr;

namespace yb {
namespace integration_tests {

using client::YBRedisWriteOp;
using client::YBRedisReadOp;
using client::YBColumnSchema;
using client::YBTableCreator;
using client::YBSchemaBuilder;
using client::YBColumnSchema;
using client::YBTableType;
using client::YBTableName;
using client::YBSession;

using redisserver::ParseSet;
using redisserver::ParseGet;

YBTableName RedisTableTestBase::table_name() {
  return YBTableName(kRedisTableName);
}

void RedisTableTestBase::CreateTable() {
  if (!table_exists_) {
    CreateRedisTable(client_, table_name());
    table_exists_ = true;
  }
}

vector<Slice> SlicesFromString(const vector<string>& args) {
  vector<Slice> vector_slice;
  for (const string& s : args) {
    vector_slice.emplace_back(s);
  }
  return vector_slice;
}

void RedisTableTestBase::PutKeyValue(string key, string value) {
  shared_ptr<YBRedisWriteOp> set_op(table_->NewRedisWrite());
  ASSERT_OK(ParseSet(set_op.get(), SlicesFromString({"set", key, value})));
  ASSERT_OK(session_->Apply(set_op));
  ASSERT_OK(session_->Flush());
}

void RedisTableTestBase::PutKeyValue(string key, string value, int64_t ttl_msec) {
  shared_ptr<YBRedisWriteOp> set_op(table_->NewRedisWrite());
  ASSERT_OK(ParseSet(set_op.get(),
      SlicesFromString({"set", key, value, "PX", std::to_string(ttl_msec)})));
  ASSERT_OK(session_->Apply(set_op));
  ASSERT_OK(session_->Flush());
}

void RedisTableTestBase::GetKeyValue(
    const string& key, const string& value, bool expect_not_found) {
  shared_ptr<YBRedisReadOp> get_op(table_->NewRedisRead());
  ASSERT_OK(ParseGet(get_op.get(), SlicesFromString({"get", key})));
  ASSERT_OK(session_->ReadSync(get_op));
  if (expect_not_found) {
    ASSERT_EQ(RedisResponsePB_RedisStatusCode_NOT_FOUND, get_op->response().code());
  } else {
    ASSERT_EQ(RedisResponsePB_RedisStatusCode_OK, get_op->response().code());
    ASSERT_EQ(value, get_op->response().string_response());
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
  PutKeyValue("key456", "value456", 500);
  PutKeyValue("key567", "value567", 1500);
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
