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
using client::RedisWriteOpForSetKV;
using client::YBColumnSchema;
using client::YBTableCreator;
using client::YBSchemaBuilder;
using client::YBColumnSchema;
using client::YBTableType;
using client::YBSession;

std::string RedisTableTestBase::table_name() {
  return RedisConstants::kRedisTableName;
}

void RedisTableTestBase::CreateTable() {
  unique_ptr<YBTableCreator> table_creator(client_->NewTableCreator());

  ASSERT_OK(table_creator->table_name(table_name())
                .table_type(YBTableType::REDIS_TABLE_TYPE)
                .num_replicas(3)
                .Create());
  table_exists_ = true;
}

void RedisTableTestBase::PutKeyValue(YBSession* session, string key, string value) {
  ASSERT_OK(session->Apply(RedisWriteOpForSetKV(table_.get(), key, value).release()));
  ASSERT_OK(session->Flush());
}

void RedisTableTestBase::RedisSimpleSetCommands() {
  YBTableTestBase::PutKeyValue("key123", "value123");
  YBTableTestBase::PutKeyValue("key200", "value200");
  YBTableTestBase::PutKeyValue("key300", "value300");
}

}  // namespace integration_tests
}  // namespace yb
