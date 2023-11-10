// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/integration-tests/redis_table_test_base.h"

#include "yb/util/logging.h"

#include "yb/client/client.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/dockv/partition.h"
#include "yb/common/redis_constants_common.h"
#include "yb/common/redis_protocol.pb.h"

#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/util/monotime.h"

#include "yb/yql/redis/redisserver/redis_parser.h"

using std::string;
using std::vector;

namespace yb {
namespace integration_tests {

using client::YBRedisWriteOp;
using client::YBRedisReadOp;
using client::YBTableName;

using redisserver::RedisClientCommand;
using redisserver::ParseSet;
using redisserver::ParseGet;

YBTableName RedisTableTestBase::table_name() {
  return YBTableName(YQL_DATABASE_REDIS, common::kRedisKeyspaceName, common::kRedisTableName);
}

void RedisTableTestBase::CreateTable() {
  if (!table_exists_) {
    CreateRedisTable(table_name());
    client::YBSchema schema;
    dockv::PartitionSchema partition_schema;
    CHECK_OK(client_->GetTableSchema(RedisTableTestBase::table_name(), &schema, &partition_schema));
    ASSERT_EQ(partition_schema.hash_schema(), dockv::YBHashSchema::kRedisHash);
    table_exists_ = true;
  }
}

RedisClientCommand SlicesFromString(const vector<string>& args) {
  RedisClientCommand vector_slice;
  for (const string& s : args) {
    vector_slice.emplace_back(s);
  }
  return vector_slice;
}

void RedisTableTestBase::PutKeyValue(const string& key, const string& value) {
  auto set_op = std::make_shared<YBRedisWriteOp>(table_->shared_from_this());
  ASSERT_OK(ParseSet(set_op.get(), SlicesFromString({"set", key, value})));
  ASSERT_OK(session_->TEST_ApplyAndFlush(set_op));
}

void RedisTableTestBase::PutKeyValueWithTtlNoFlush(
    const string& key, const string& value, int64_t ttl_msec) {
  auto set_op = std::make_shared<YBRedisWriteOp>(table_->shared_from_this());
  ASSERT_OK(ParseSet(set_op.get(),
      SlicesFromString({"set", key, value, "PX", std::to_string(ttl_msec)})));
  session_->Apply(set_op);
}

void RedisTableTestBase::GetKeyValue(
    const string& key, const string& value, bool expect_not_found) {
  auto get_op = std::make_shared<YBRedisReadOp>(table_->shared_from_this());
  ASSERT_OK(ParseGet(get_op.get(), SlicesFromString({"get", key})));
  ASSERT_OK(session_->TEST_ReadSync(get_op));
  if (expect_not_found) {
    ASSERT_EQ(RedisResponsePB_RedisStatusCode_NIL, get_op->response().code());
  } else {
    ASSERT_EQ(RedisResponsePB_RedisStatusCode_OK, get_op->response().code());
    ASSERT_EQ(value, get_op->response().string_response());
  }
}

void RedisTableTestBase::RedisSimpleSetCommands() {
  session_ = NewSession();
  PutKeyValue("key123", "value123");
  PutKeyValue("key200", "value200");
  PutKeyValue("key300", "value300");
}

void RedisTableTestBase::RedisSimpleGetCommands() {
  session_ = NewSession();
  GetKeyValue("key123", "value123");
  GetKeyValue("key200", "value200");
  GetKeyValue("key300", "value300");
  GetKeyValue("key400", "value400", true);
}

void RedisTableTestBase::RedisTtlSetCommands() {
  session_ = NewSession();
  PutKeyValueWithTtlNoFlush("key456", "value456", 10000);
  PutKeyValueWithTtlNoFlush("key567", "value567", 500);
  PutKeyValue("key678", "value678");
  // The first two commands do not flush the session, but the last command does.
}

void RedisTableTestBase::RedisTtlGetCommands() {
  session_ = NewSession();
  GetKeyValue("key456", "value456", false);
  GetKeyValue("key567", "value567", true);
  GetKeyValue("key678", "value678", false);
}

}  // namespace integration_tests
}  // namespace yb
